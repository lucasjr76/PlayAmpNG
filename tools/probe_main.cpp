// pang_probe — ferramenta headless do M0.
//
// Existe por dois motivos concretos:
//   1. Prova que core/ e platform/ compilam, ligam e rodam sem Qt e sem janela,
//      que e a premissa de toda a estrategia de testes (ARCHITECTURE.md 11).
//   2. Resolve no M0 o risco que so apareceria no M1: se FFmpeg e miniaudio
//      tiverem alguma surpresa de integracao, ela aparece aqui.
//
// Uso: pang_probe [arquivo-ou-url ...]

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "core/util/log.h"
#include "core/audio/engine.h"
#include "core/audio/probe.h"
#include "platform/audio_device.h"

namespace {

void print_environment() {
    std::printf("FFmpeg   %s  (licenca: %s)\n", pang::core::ffmpeg_version().c_str(),
                pang::core::ffmpeg_license().c_str());
    std::printf("Backend  %s\n\n", pang::platform::backend_name().c_str());

    std::string error;
    const auto devices = pang::platform::list_playback_devices(error);
    if (!error.empty()) {
        std::printf("dispositivos de saida: erro — %s\n", error.c_str());
        return;
    }
    std::printf("dispositivos de saida (%zu):\n", devices.size());
    for (const auto& d : devices)
        std::printf("  %s %s\n", d.is_default ? "*" : " ", d.name.c_str());
}

// Campo ausente imprime "-", nunca um numero plausivel (PL-26).
void print_probe(const std::string& url) {
    std::string error;
    const auto r = pang::core::probe(url, error);
    // AR-06 — a ferramenta tambem nao ecoa credencial: a saida dela vai parar
    // em relato de defeito do mesmo jeito que o log do player.
    std::printf("\n%s\n", pang::core::log::redact(url).c_str());
    if (!r) {
        std::printf("  erro: %s\n", error.c_str());
        return;
    }
    std::printf("  formato    %s\n", r->format.c_str());
    std::printf("  codec      %s\n", r->codec.c_str());
    std::printf("  taxa       %d Hz\n", r->sample_rate);
    std::printf("  canais     %d (%s)\n", r->channels, r->channels == 1 ? "mono" : "estereo");
    if (r->duration_us)
        std::printf("  duracao    %.3f s\n", static_cast<double>(*r->duration_us) / 1e6);
    else
        std::printf("  duracao    -  (fonte nao informa)\n");
    if (r->bitrate_bps)
        std::printf("  bitrate    %lld bps\n", static_cast<long long>(*r->bitrate_bps));
    else
        std::printf("  bitrate    -  (fonte nao informa)\n");
    std::printf("  busca      %s\n", r->seekable ? "suportada" : "nao suportada");
}

// Reproducao pelo dispositivo real, sem Qt e sem janela.
//
// Os testes chamam Engine::render() diretamente, o que cobre o engine mas nao
// o caminho pela camada de plataforma. Este modo exercita dispositivo, callback
// e engine juntos — que e o que de fato toca som.
int play(const std::string& url, double seconds) {
    pang::platform::AudioOutput output;
    std::unique_ptr<pang::core::Engine> engine;

    struct Bridge {
        pang::core::Engine* engine = nullptr;
        int channels = 2;
    } bridge;

    auto render = [](void* user, float* out, std::uint32_t frames) {
        auto* b = static_cast<Bridge*>(user);
        if (b->engine)
            b->engine->render(out, frames);
        else
            std::memset(out, 0, static_cast<std::size_t>(frames) * b->channels * sizeof(float));
    };

    std::string error;
    if (!output.start(44100, 2, render, &bridge, error)) {
        std::printf("erro: %s\n", error.c_str());
        return 1;
    }
    engine = std::make_unique<pang::core::Engine>(output.sample_rate(), output.channels());
    bridge.engine = engine.get();
    bridge.channels = output.channels();

    std::printf("\ndispositivo: %s  %d Hz  %d canais\n", output.device_name().c_str(),
                output.sample_rate(), output.channels());

    output.suspend();  // precondicao de load(): render() parado
    engine->load(url, pang::core::State::Playing);
    output.resume();

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(static_cast<int>(seconds * 1000));
    pang::core::Snapshot snap;
    while (std::chrono::steady_clock::now() < deadline) {
        snap = engine->snapshot();
        if (snap.state == pang::core::State::Stopped && snap.position_frames > 0) break;
        if (snap.state == pang::core::State::Error) {
            std::printf("erro: %s\n", engine->last_error().c_str());
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    output.suspend();
    std::printf("estado=%s  posicao=%.3f s  underruns=%u\n", pang::core::to_string(snap.state),
                static_cast<double>(snap.position_frames) / output.sample_rate(), snap.underruns);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // pang_probe --play <arquivo> [segundos]
    if (argc >= 3 && std::string(argv[1]) == "--play") {
        print_environment();
        const double seconds = argc >= 4 ? std::atof(argv[3]) : 10.0;
        return play(argv[2], seconds);
    }

    print_environment();
    for (int i = 1; i < argc; ++i) print_probe(argv[i]);
    return 0;
}
