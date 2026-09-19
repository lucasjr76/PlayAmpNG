// AU-12, RB-05 — perda REAL do dispositivo de saida.
//
// A politica de reabertura ja e verificada isolada em test_device.cpp. Este
// teste cobre o que so aparece contra o sistema de audio de verdade, e que a
// primeira implementacao errou em dois pontos:
//
//   1. O callback de NOTIFICACAO do miniaudio nao dispara quando um sink do
//      PulseAudio em uso e removido. Medido: os quadros param e o dispositivo
//      segue se declarando saudavel por mais de 25 s. Detectar perda so pela
//      notificacao deixava o player mudo sem nada acusar.
//   2. ma_device_uninit num dispositivo cujo sink sumiu NAO RETORNA. Destruir
//      o morto antes de abrir o vivo travava a recuperacao inteira, e, no
//      caminho original, congelava a janela junto.
//
// O sink e criado e removido pelo script que lanca este binario.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <algorithm>
#include <vector>

#include "core/audio/engine.h"
#include "core/util/check.h"
#include "platform/audio_device.h"

namespace {

// Um WAV longo o bastante para atravessar a perda e a recuperacao. Os arquivos
// de teste do repositorio tem meio segundo; gerar aqui evita acrescentar um
// asset de megabytes so para este caso.
bool write_long_wav(const std::string& path, int seconds) {
    const int rate = 44100, channels = 2;
    const std::uint32_t samples = static_cast<std::uint32_t>(rate * seconds) * channels;
    const std::uint32_t data_bytes = samples * 2;

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    auto u32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);  u32(36 + data_bytes);  std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);  u32(16);  u16(1);  u16(channels);
    u32(rate);  u32(rate * channels * 2);  u16(channels * 2);  u16(16);
    std::fwrite("data", 1, 4, f);  u32(data_bytes);

    std::vector<std::int16_t> block(channels * 1024);
    std::uint32_t written = 0;
    double phase = 0.0;
    while (written < samples) {
        for (std::size_t i = 0; i < block.size(); i += channels) {
            const auto v = static_cast<std::int16_t>(8000.0 * std::sin(phase));
            phase += 2.0 * 3.14159265358979 * 440.0 / rate;
            for (int c = 0; c < channels; ++c) block[i + c] = v;
        }
        const std::size_t n = std::min<std::size_t>(block.size(), samples - written);
        std::fwrite(block.data(), 2, n, f);
        written += static_cast<std::uint32_t>(n);
    }
    std::fclose(f);
    return true;
}

struct Bridge {
    pang::core::Engine* engine = nullptr;
};

void render(void* user, float* out, std::uint32_t frames) {
    auto* b = static_cast<Bridge*>(user);
    if (b && b->engine)
        b->engine->render(out, frames);
    else
        std::memset(out, 0, static_cast<std::size_t>(frames) * 2 * sizeof(float));
}

}  // namespace

int main(int argc, char** argv) {
    using pang::platform::AudioOutput;

    if (argc < 3) {
        std::printf("uso: %s <nome-do-dispositivo> <arquivo-temporario>\n", argv[0]);
        return 1;
    }
    const std::string device = argv[1];
    const std::string media = argv[2];

    PANG_CHECK(write_long_wav(media, 30), "arquivo de teste gerado");

    pang::core::Engine engine(44100, 2);
    Bridge bridge{&engine};

    AudioOutput output;
    std::string error;
    PANG_CHECK(output.start(44100, 2, render, &bridge, error, device),
               ("dispositivo de teste abre: " + error).c_str());
    if (!output.is_open()) return pang::check::exit_code();

    // AU-11 — abriu no dispositivo PEDIDO, nao em outro qualquer.
    PANG_CHECK(output.device_name() == device,
               ("abriu no dispositivo pedido, e nao em \"" + output.device_name() + "\"").c_str());

    engine.load(media, pang::core::State::Playing);

    bool played_before_loss = false;
    bool saw_recovering = false;
    bool recovered = false;
    std::int64_t position_at_loss = 0;
    std::int64_t previous_position = 0;
    bool position_went_backwards = false;
    std::string device_after;

    // 12 s cobrem a parada (1 s), a espera (0,5 s), a reabertura e folga.
    for (int step = 0; step < 120; ++step) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const AudioOutput::Health health = output.poll(100);
        const std::int64_t position = engine.snapshot().position_frames;

        if (position < previous_position) position_went_backwards = true;
        previous_position = position;

        if (health == AudioOutput::Health::Ok && !saw_recovering && position > 4410)
            played_before_loss = true;

        if (health == AudioOutput::Health::Recovering && !saw_recovering) {
            saw_recovering = true;
            position_at_loss = position;
        }
        if (saw_recovering && health == AudioOutput::Health::Ok && !recovered) {
            recovered = true;
            device_after = output.device_name();
        }
        if (recovered && position > position_at_loss + 4410) break;
    }

    PANG_CHECK(played_before_loss, "tocou antes da perda");
    // O ponto 1 do cabecalho: sem o detector de parada isto nunca acontece.
    PANG_CHECK(saw_recovering, "a perda do dispositivo foi detectada");
    // O ponto 2: sem abandonar o dispositivo morto, isto nunca acontece.
    PANG_CHECK(recovered, "a saida foi restabelecida dentro do limite de tentativas");
    PANG_CHECK(!position_went_backwards, "a posicao nunca retrocedeu");

    if (recovered) {
        PANG_CHECK(device_after != device,
                   "com o dispositivo pedido removido, a saida caiu em outro");
        const std::int64_t position = engine.snapshot().position_frames;
        PANG_CHECK(position > position_at_loss,
                   "a reproducao seguiu do ponto em que estava, e nao do inicio");
        std::printf("  perda em %lld quadros, retomada em %lld, saida \"%s\"\n",
                    static_cast<long long>(position_at_loss), static_cast<long long>(position),
                    device_after.c_str());
    }

    engine.stop();
    output.close();
    std::remove(media.c_str());
    return pang::check::exit_code();
}
