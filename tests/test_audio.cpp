// Testes do M1 — reproducao real, verificada sem placa de som.
//
// Todo o caminho de audio e exercitado chamando Engine::render() diretamente,
// que e exatamente o que o callback do dispositivo faria. E esse o beneficio
// concreto de core/ nao conhecer Qt nem miniaudio.

#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "core/audio/engine.h"
#include "core/audio/ring.h"
#include "core/audio/seqlock.h"
#include "core/util/check.h"
#include "core/util/log.h"

using namespace std::chrono_literals;
using pang::core::Engine;
using pang::core::State;

namespace {

constexpr int kRate = 44100;
constexpr int kChannels = 2;
constexpr std::uint32_t kBlock = 512;
constexpr double kToneSeconds = 0.5;

std::string asset(const char* name) { return std::string(PANG_TEST_ASSETS) + "/" + name; }

// Espera o engine sair de Loading. Devolve o estado alcancado.
State settle(Engine& engine, std::chrono::milliseconds limit = 3000ms) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    for (;;) {
        const State s = engine.snapshot().state;
        if (s != State::Loading) return s;
        if (std::chrono::steady_clock::now() > deadline) return s;
        std::this_thread::sleep_for(2ms);
    }
}

struct Capture {
    std::vector<float> samples;
    std::int64_t frames = 0;

    double rms() const {
        if (samples.empty()) return 0.0;
        double sum = 0.0;
        for (float s : samples) sum += static_cast<double>(s) * s;
        return std::sqrt(sum / samples.size());
    }
};

// Renderiza ate o engine parar, com teto de seguranca.
Capture drain(Engine& engine, double max_seconds = 5.0) {
    Capture cap;
    std::vector<float> block(kBlock * kChannels);
    const std::int64_t limit = static_cast<std::int64_t>(max_seconds * kRate);

    while (cap.frames < limit) {
        engine.render(block.data(), kBlock);
        const State s = engine.snapshot().state;
        // Grava o bloco; ao parar, o ultimo bloco ja veio em silencio.
        if (s == State::Playing) {
            cap.samples.insert(cap.samples.end(), block.begin(), block.end());
            cap.frames += kBlock;
        } else if (s == State::Stopped || s == State::Error) {
            break;
        }
        std::this_thread::sleep_for(1ms);  // deixa o decodificador encher o ring
    }
    return cap;
}

// -------------------------------------------------------------- AU-02..AU-07

void test_format(const char* file, const char* label) {
    Engine engine(kRate, kChannels);
    engine.load(asset(file), true);

    const State s = settle(engine);
    PANG_CHECK(s == State::Playing, std::string("decodifica e comeca a tocar: ") + label);
    if (s != State::Playing) return;

    const auto snap = engine.snapshot();
    PANG_CHECK(snap.sample_rate > 0, std::string("taxa de amostragem informada: ") + label);
    PANG_CHECK(snap.channels == 2, std::string("dois canais: ") + label);

    const Capture cap = drain(engine);

    // O tom tem 0,5 s. Formatos com perda acrescentam quadros de padding que o
    // libavcodec apara; a tolerancia cobre o residuo, nao um erro de fator.
    const double seconds = static_cast<double>(cap.frames) / kRate;
    PANG_CHECK(seconds > kToneSeconds * 0.85 && seconds < kToneSeconds * 1.25,
               std::string("duracao reproduzida proxima de 0,5 s: ") + label);

    // Os arquivos de teste sao senoide de amplitude 0,5 exata (-6 dBFS de
    // pico), entao o RMS esperado e 0,5/raiz(2) = 0,3536. Silencio, ruido ou
    // ganho errado sairiam dessa faixa.
    const double rms = cap.rms();
    PANG_CHECK(rms > 0.32 && rms < 0.39,
               std::string("RMS 0,354 esperado da senoide de amplitude 0,5: ") + label);

    PANG_CHECK(engine.snapshot().state == State::Stopped,
               std::string("para sozinho no fim da faixa: ") + label);
}

// ------------------------------------------------------------------- PL-22

void test_seek() {
    Engine engine(kRate, kChannels);
    engine.load(asset("tone.wav"), true);
    if (settle(engine) != State::Playing) {
        PANG_CHECK(false, "seek: faixa nao carregou");
        return;
    }

    PANG_CHECK(engine.snapshot().seekable, "arquivo local e pesquisavel");

    const bool ok = engine.seek(0.25);
    PANG_CHECK(ok, "seek aceito em fonte pesquisavel");

    const std::int64_t pos = engine.snapshot().position_frames;
    const std::int64_t expected = static_cast<std::int64_t>(0.25 * kRate);
    PANG_CHECK(std::llabs(pos - expected) < kRate / 10,
               "posicao apos o seek corresponde ao alvo (tolerancia 100 ms)");

    // Restam ~0,25 s: metade do arquivo.
    const Capture cap = drain(engine);
    const double seconds = static_cast<double>(cap.frames) / kRate;
    PANG_CHECK(seconds > 0.15 && seconds < 0.35, "apos seek para a metade, resta ~0,25 s");
}

// --------------------------------------------------- PL-02, PL-03, AR-02, AR-04

void test_transport_semantics() {
    Engine engine(kRate, kChannels);
    engine.load(asset("tone.wav"), true);
    if (settle(engine) != State::Playing) {
        PANG_CHECK(false, "transporte: faixa nao carregou");
        return;
    }

    std::vector<float> block(kBlock * kChannels);
    for (int i = 0; i < 4; ++i) engine.render(block.data(), kBlock);
    const std::int64_t advanced = engine.snapshot().position_frames;
    PANG_CHECK(advanced > 0, "a posicao avanca durante a reproducao");

    // PL-02 — pausado, o render nao consome nem avanca.
    engine.pause();
    PANG_CHECK(engine.snapshot().state == State::Paused, "pause leva a Paused");
    for (int i = 0; i < 4; ++i) engine.render(block.data(), kBlock);
    PANG_CHECK(engine.snapshot().position_frames == advanced,
               "pausado, a posicao nao avanca");

    bool silent = true;
    for (float s : block) silent = silent && s == 0.0f;
    PANG_CHECK(silent, "pausado, o render entrega silencio");

    // Retomar continua de onde parou, nao do inicio.
    engine.play();
    PANG_CHECK(engine.snapshot().state == State::Playing, "play retoma de Paused");
    std::this_thread::sleep_for(20ms);
    for (int i = 0; i < 4; ++i) engine.render(block.data(), kBlock);
    PANG_CHECK(engine.snapshot().position_frames > advanced,
               "retomado, a posicao volta a avancar a partir do ponto de pausa");

    // PL-03 — parar zera a posicao e mantem a faixa carregada.
    engine.stop();
    const auto snap = engine.snapshot();
    PANG_CHECK(snap.state == State::Stopped, "stop leva a Stopped");
    PANG_CHECK(snap.position_frames == 0, "stop zera a posicao");
}

// ------------------------------------------------------------ RB-01, AR-04

void test_error_paths() {
    Engine engine(kRate, kChannels);
    engine.load(asset("nao-existe.flac"), true);
    const State s = settle(engine);
    PANG_CHECK(s == State::Error, "arquivo ausente leva a Error, nao a Playing");
    PANG_CHECK(!engine.last_error().empty(), "Error traz mensagem de diagnostico");

    // A interface reflete o estado confirmado: render em Error e silencio.
    std::vector<float> block(kBlock * kChannels, 1.0f);
    engine.render(block.data(), kBlock);
    bool silent = true;
    for (float v : block) silent = silent && v == 0.0f;
    PANG_CHECK(silent, "em Error o render entrega silencio");
}

// ------------------------------------------------------------------- AR-09

void test_cancellation_is_prompt() {
    Engine engine(kRate, kChannels);
    engine.load(asset("tone.wav"), true);
    settle(engine);

    // stop() aciona cancel() no decodificador e faz join. Se o cancelamento
    // nao funcionasse, isto esperaria o fim da decodificacao.
    const auto start = std::chrono::steady_clock::now();
    engine.stop();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    PANG_CHECK(elapsed < 500ms, "parar interrompe a decodificacao em menos de 500 ms");
}

// ------------------------------------------------------------------ FloatRing

void test_ring() {
    pang::core::FloatRing ring(8);
    PANG_CHECK(ring.capacity() == 8, "capacidade util e a pedida");
    PANG_CHECK(ring.readable() == 0, "ring novo esta vazio");

    const float in[5] = {1, 2, 3, 4, 5};
    PANG_CHECK(ring.write(in, 5) == 5, "escreve 5 em ring vazio de 8");
    PANG_CHECK(ring.readable() == 5, "5 legiveis apos a escrita");
    PANG_CHECK(ring.writable() == 3, "3 posicoes livres restantes");

    float out[5] = {};
    PANG_CHECK(ring.read(out, 5) == 5, "le os 5 de volta");
    PANG_CHECK(out[0] == 1 && out[4] == 5, "valores preservados na ordem");

    // Escrita que atravessa o fim do buffer, que e onde um ring costuma errar.
    const float more[6] = {10, 20, 30, 40, 50, 60};
    PANG_CHECK(ring.write(more, 6) == 6, "escrita com volta ao inicio");
    float back[6] = {};
    PANG_CHECK(ring.read(back, 6) == 6, "leitura com volta ao inicio");
    bool same = true;
    for (int i = 0; i < 6; ++i) same = same && back[i] == more[i];
    PANG_CHECK(same, "valores preservados atraves da borda do buffer");

    PANG_CHECK(ring.write(in, 5) == 5 && ring.write(in, 5) == 3,
               "escrita em ring cheio grava so o que cabe, sem sobrescrever");
}

// -------------------------------------------------------------------- AR-11

void test_seqlock_consistency() {
    // Campos que precisam concordar: se o leitor enxergar um bloco rasgado,
    // b deixa de ser a + 1.
    struct Pair {
        std::uint64_t a;
        std::uint64_t b;
        std::uint64_t padding[6];  // grande o bastante para nao ser atomico
    };
    static_assert(sizeof(Pair) > 16);

    pang::core::Seqlock<Pair> lock;
    std::atomic<bool> quit{false};
    std::atomic<int> torn{0};
    std::atomic<long> reads{0};

    std::thread writer([&] {
        for (std::uint64_t i = 1; !quit.load(); ++i) lock.store(Pair{i, i + 1, {}});
    });

    // Antes da primeira publicacao o conteudo e o valor inicial, que nao
    // satisfaz b == a + 1. Contar isso como bloco rasgado seria falso positivo.
    while (lock.versions() == 0) std::this_thread::yield();

    const auto deadline = std::chrono::steady_clock::now() + 200ms;
    while (std::chrono::steady_clock::now() < deadline) {
        const Pair p = lock.load();
        if (p.b != p.a + 1) torn.fetch_add(1);
        reads.fetch_add(1);
    }
    quit.store(true);
    writer.join();

    PANG_CHECK(reads.load() > 1000, "o teste executou leituras suficientes para valer");
    PANG_CHECK(torn.load() == 0, "o leitor nunca observa um bloco rasgado");
}

// -------------------------------------------------------------------- AR-06

void test_redaction() {
    using pang::core::log::redact;

    const std::string a = redact("http://usuario:segredo@radio.example/stream");
    PANG_CHECK(a.find("segredo") == std::string::npos, "senha em URL nao aparece no log");
    PANG_CHECK(a.find("radio.example") != std::string::npos, "o host continua legivel");

    const std::string b = redact("https://api.example/x?token=abc123&fmt=mp3");
    PANG_CHECK(b.find("abc123") == std::string::npos, "token em parametro nao aparece no log");
    PANG_CHECK(b.find("fmt=mp3") != std::string::npos, "parametros inofensivos sao preservados");

    const std::string c = redact("/home/usuario/musica/faixa.mp3");
    PANG_CHECK(c == "/home/usuario/musica/faixa.mp3",
               "caminho local sem credencial passa intacto");
}

}  // namespace

int main() {
    test_format("tone.wav", "WAV");
    test_format("tone.mp3", "MP3");
    test_format("tone.flac", "FLAC");
    test_format("tone.ogg", "Ogg Vorbis");
    test_format("tone.opus", "Opus");
    test_format("tone.m4a", "AAC/M4A");

    test_seek();
    test_transport_semantics();
    test_error_paths();
    test_cancellation_is_prompt();
    test_ring();
    test_seqlock_consistency();
    test_redaction();

    return pang::check::exit_code();
}
