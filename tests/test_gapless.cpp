// Testes de gapless do M3 (AU-13, AU-14).
//
// O criterio nao e auditivo: e contagem exata de amostras. Um sinal de rampa,
// em que cada amostra tem valor proprio, e cortado em dois arquivos. Tocados em
// sequencia com emenda, a saida tem de conter a sequencia original inteira —
// sem nenhuma amostra a mais nem a menos na juncao.
//
// O criterio antigo, "descontinuidade abaixo de -60 dBFS", nao servia: duas
// musicas quaisquer podem ter descontinuidade natural na juncao, e um teste que
// passa por acidente nao e teste.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "core/audio/decoder.h"
#include "core/audio/engine.h"
#include "core/state/controller.h"
#include "core/util/check.h"

using namespace std::chrono_literals;
using namespace pang::core;

namespace {

constexpr int kRate = 44100;
constexpr int kChannels = 2;
constexpr std::uint32_t kBlock = 512;

std::string asset(const char* name) { return std::string(PANG_TEST_ASSETS) + "/" + name; }

// Decodifica um arquivo inteiro para memoria, sem passar pelo engine.
std::vector<float> decode_all(const std::string& path) {
    Decoder decoder;
    std::string error;
    if (!decoder.open(path, kRate, kChannels, error)) return {};

    std::vector<float> out;
    std::vector<float> chunk(4096 * kChannels);
    for (;;) {
        const std::size_t got = decoder.read(chunk.data(), 4096);
        if (got == 0) break;
        out.insert(out.end(), chunk.begin(), chunk.begin() + got * kChannels);
    }
    return out;
}

void settle(Engine& engine) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (engine.snapshot().state == State::Loading &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(2ms);
}

// Renderiza ate o engine parar, guardando tudo que saiu.
std::vector<float> capture(Engine& engine, Controller& controller, double max_seconds) {
    std::vector<float> out;
    std::vector<float> block(kBlock * kChannels);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(int(max_seconds * 1000));

    while (std::chrono::steady_clock::now() < deadline) {
        engine.render(block.data(), kBlock);
        controller.poll();
        const State s = engine.snapshot().state;
        if (s == State::Playing) {
            out.insert(out.end(), block.begin(), block.end());
        } else if (s == State::Stopped || s == State::Error) {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    return out;
}

void test_gapless_sample_exact() {
    const std::vector<float> a = decode_all(asset("ramp_a.wav"));
    const std::vector<float> b = decode_all(asset("ramp_b.wav"));
    PANG_CHECK(!a.empty() && !b.empty(), "arquivos de rampa decodificados");

    std::vector<float> expected = a;
    expected.insert(expected.end(), b.begin(), b.end());

    Engine engine(kRate, kChannels);
    Controller controller(engine);
    controller.playlist().add(asset("ramp_a.wav"));
    controller.playlist().add(asset("ramp_b.wav"));
    controller.playlist_changed();
    controller.play_index(0);
    settle(engine);

    const std::vector<float> produced = capture(engine, controller, 6.0);

    // O limitador atrasa a saida pelo seu lookahead. Isso desloca, nao insere
    // nem remove amostras — o alinhamento e feito de proposito, e nao com
    // correlacao cruzada, que mascararia justamente o defeito procurado.
    const std::size_t delay = static_cast<std::size_t>(engine.latency_frames()) * kChannels;
    PANG_CHECK(produced.size() >= expected.size() + delay,
               "a saida cobre as duas faixas inteiras mais o atraso do limitador");
    if (produced.size() < expected.size() + delay) return;

    std::size_t mismatches = 0;
    std::size_t first_mismatch = 0;
    float worst = 0.0f;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const float difference = std::fabs(produced[delay + i] - expected[i]);
        if (difference > 1e-6f) {
            if (mismatches == 0) first_mismatch = i;
            ++mismatches;
            worst = std::max(worst, difference);
        }
    }

    std::printf("  gapless: esperadas %zu amostras, produzidas %zu, divergentes %zu\n",
                expected.size(), produced.size(), mismatches);
    if (mismatches > 0)
        std::printf("    primeira divergencia no indice %zu (juncao em %zu), maior erro %.6f\n",
                    first_mismatch, a.size(), worst);

    // AU-13 — nenhuma amostra a mais nem a menos: a sequencia bate posicao a
    // posicao atraves da juncao.
    PANG_CHECK(mismatches == 0,
               "AU-13: a emenda nao insere nem remove amostra alguma na juncao");
}

// Contraprova: sem informar a proxima faixa, a emenda nao acontece e a
// reproducao para no fim da primeira. Se este teste passasse junto com o
// anterior sem diferenca, o primeiro nao estaria medindo nada.
void test_without_next_there_is_no_splice() {
    Engine engine(kRate, kChannels);
    engine.load(asset("ramp_a.wav"), State::Playing);
    settle(engine);

    std::vector<float> block(kBlock * kChannels);
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    std::size_t frames = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        engine.render(block.data(), kBlock);
        if (engine.snapshot().state != State::Playing) break;
        frames += kBlock;
        std::this_thread::sleep_for(1ms);
    }

    const std::vector<float> a = decode_all(asset("ramp_a.wav"));
    const std::size_t a_frames = a.size() / kChannels;
    PANG_CHECK(engine.ended(), "sem proxima faixa, o engine sinaliza fim");
    PANG_CHECK(frames < a_frames + kRate / 2,
               "sem proxima faixa, a reproducao para no fim da primeira");
}

void test_position_restarts_after_splice() {
    Engine engine(kRate, kChannels);
    Controller controller(engine);
    controller.playlist().add(asset("ramp_a.wav"));
    controller.playlist().add(asset("ramp_b.wav"));
    controller.playlist_changed();
    controller.play_index(0);
    settle(engine);

    std::vector<float> block(kBlock * kChannels);
    const auto deadline = std::chrono::steady_clock::now() + 6s;
    std::int64_t last_position = 0;
    bool restarted = false;

    while (std::chrono::steady_clock::now() < deadline) {
        engine.render(block.data(), kBlock);
        controller.poll();
        const auto snap = engine.snapshot();
        if (snap.position_frames < last_position) restarted = true;
        last_position = snap.position_frames;
        if (snap.state == State::Stopped && restarted) break;
        std::this_thread::sleep_for(1ms);
    }

    PANG_CHECK(restarted, "a posicao reinicia quando a faixa emendada assume");
    PANG_CHECK(controller.current_index() == 1,
               "o Controller acompanha a emenda e passa a apontar a segunda faixa");
}

// AU-14 — disponibilidade de gapless por formato, MEDIDA.
//
// Nao mede a nossa emenda (isso e AU-13): mede se o formato entrega, depois de
// cortado em dois, a mesma contagem de amostras do original. Quando o
// codificador insere delay e padding e o conteiner nao carrega a informacao
// para apara-los, a soma das partes fica maior que o todo — e ai havera lacuna
// audivel, coisa que nenhum player corrige.
void test_format_gapless_matrix() {
    struct Case {
        const char* extension;
        const char* label;
    };
    const Case cases[] = {
        {"wav", "WAV"},   {"flac", "FLAC"},     {"ogg", "Ogg Vorbis"},
        {"opus", "Opus"}, {"mp3", "MP3"},       {"m4a", "AAC/M4A"},
    };

    const std::size_t original_frames = decode_all(asset("ramp_full.wav")).size() / kChannels;
    PANG_CHECK(original_frames == 44100, "o sinal de referencia tem 44100 quadros");

    std::printf("  AU-14 — soma das partes contra o original (%zu quadros):\n", original_frames);
    for (const Case& c : cases) {
        const std::string first = std::string("ramp_a.") + c.extension;
        const std::string second = std::string("ramp_b.") + c.extension;
        const std::size_t a = decode_all(asset(first.c_str())).size() / kChannels;
        const std::size_t b = decode_all(asset(second.c_str())).size() / kChannels;
        if (a == 0 || b == 0) {
            std::printf("    %-12s indisponivel neste build\n", c.label);
            continue;
        }
        const long long delta =
            static_cast<long long>(a + b) - static_cast<long long>(original_frames);
        std::printf("    %-12s %zu + %zu = %zu  (%+lld quadros, %+.1f ms)\n", c.label, a, b,
                    a + b, delta, 1000.0 * delta / kRate);
    }

    // Os sem perda tem de bater exatamente; sao a referencia de que o
    // mecanismo esta correto antes de julgar os demais.
    for (const char* extension : {"wav", "flac"}) {
        const std::size_t a =
            decode_all(asset((std::string("ramp_a.") + extension).c_str())).size() / kChannels;
        const std::size_t b =
            decode_all(asset((std::string("ramp_b.") + extension).c_str())).size() / kChannels;
        PANG_CHECK(a + b == original_frames,
                   "AU-14: formato sem perda soma exatamente o original");
    }
}

}  // namespace

int main() {
    test_gapless_sample_exact();
    test_format_gapless_matrix();
    test_without_next_there_is_no_splice();
    test_position_restarts_after_splice();
    return pang::check::exit_code();
}
