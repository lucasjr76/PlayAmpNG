// Testes do M4 — espectro e osciloscopio.
//
// A especificacao proibe numero aleatorio, animacao pre-calculada e dado de
// demonstracao. O contrario disso nao se prova por inspecao visual: prova-se
// alimentando sinais conhecidos e conferindo onde a energia aparece.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "core/audio/engine.h"
#include "core/dsp/analyzer.h"
#include "core/dsp/fft.h"
#include "core/util/check.h"

using namespace std::chrono_literals;
using namespace pang::core;
using namespace pang::core::dsp;

namespace {

constexpr int kRate = 44100;
constexpr int kChannels = 2;
constexpr double kPi = 3.14159265358979323846;

std::string asset(const char* name) { return std::string(PANG_TEST_ASSETS) + "/" + name; }

// Senoide estereo; `phase_flip` inverte o canal direito (oposicao de fase).
std::vector<float> sine(double frequency, double amplitude, std::size_t frames,
                        bool phase_flip = false, bool left_only = false) {
    std::vector<float> out(frames * kChannels);
    for (std::size_t f = 0; f < frames; ++f) {
        const float v = static_cast<float>(amplitude * std::sin(2.0 * kPi * frequency * f / kRate));
        out[f * kChannels] = v;
        out[f * kChannels + 1] = left_only ? 0.0f : (phase_flip ? -v : v);
    }
    return out;
}

// Alimenta o analisador ate a suavizacao estabilizar e devolve as barras.
std::array<float, SpectrumAnalyzer::kBars> settle_bars(SpectrumAnalyzer& analyzer,
                                                       const std::vector<float>& signal) {
    for (int repeat = 0; repeat < 40; ++repeat)
        analyzer.feed(signal.data(), signal.size() / kChannels);
    return analyzer.bars();
}

int loudest_bar(const std::array<float, SpectrumAnalyzer::kBars>& bars) {
    return static_cast<int>(std::max_element(bars.begin(), bars.end()) - bars.begin());
}

// ------------------------------------------------------------ VI-04, VI-05

void test_sine_lands_on_the_right_bin() {
    // A FFT crua, sem o analisador, para conferir bin e amplitude.
    constexpr int kSize = SpectrumAnalyzer::kFftSize;
    Fft fft;
    HannWindow window;
    fft.configure(kSize);
    window.configure(kSize);

    std::vector<float> real(kSize), imaginary(kSize, 0.0f);
    for (int i = 0; i < kSize; ++i)
        real[static_cast<std::size_t>(i)] =
            static_cast<float>(std::sin(2.0 * kPi * 1000.0 * i / kRate)) *
            window.values()[static_cast<std::size_t>(i)];
    fft.forward(real.data(), imaginary.data());

    const float normalization = 2.0f / (kSize * window.coherent_gain());
    int peak_bin = 0;
    float peak_magnitude = 0.0f;
    for (int k = 1; k < kSize / 2; ++k) {
        const float magnitude =
            std::sqrt(real[static_cast<std::size_t>(k)] * real[static_cast<std::size_t>(k)] +
                      imaginary[static_cast<std::size_t>(k)] * imaginary[static_cast<std::size_t>(k)]) *
            normalization;
        if (magnitude > peak_magnitude) {
            peak_magnitude = magnitude;
            peak_bin = k;
        }
    }

    const int expected = static_cast<int>(std::lround(1000.0 * kSize / kRate));
    const double db = 20.0 * std::log10(peak_magnitude);
    std::printf("  senoide 1 kHz: pico no bin %d (esperado %d), amplitude %.3f (%.2f dBFS)\n",
                peak_bin, expected, peak_magnitude, db);

    PANG_CHECK(std::abs(peak_bin - expected) <= 1, "VI-04: pico no bin correspondente a 1 kHz");
    PANG_CHECK(std::fabs(db) <= 0.5,
               "VI-05: senoide de amplitude 1.0 resulta em 0 dBFS, com margem de 0,5 dB");
}

// -------------------------------------------------------------------- VI-12

void test_silence_is_numerically_stable() {
    SpectrumAnalyzer analyzer;
    analyzer.configure(kRate, kChannels);

    const std::vector<float> silence(4096 * kChannels, 0.0f);
    for (int i = 0; i < 20; ++i) analyzer.feed(silence.data(), 4096);
    analyzer.advance_peaks(0.016f);

    bool valid = true;
    bool at_floor = true;
    for (int bar = 0; bar < SpectrumAnalyzer::kBars; ++bar) {
        const float value = analyzer.bars()[static_cast<std::size_t>(bar)];
        valid = valid && std::isfinite(value);
        at_floor = at_floor && value < 1e-3f;
    }
    PANG_CHECK(valid, "VI-12: silencio nao produz NaN nem inf");
    PANG_CHECK(at_floor, "VI-12: silencio deixa todas as barras no piso");
}

// -------------------------------------------------------------------- VI-13

void test_antiphase_does_not_cancel() {
    auto energy = [](const std::array<float, SpectrumAnalyzer::kBars>& bars) {
        float sum = 0.0f;
        for (float v : bars) sum += v;
        return sum;
    };

    SpectrumAnalyzer in_phase, anti_phase, single;
    in_phase.configure(kRate, kChannels);
    anti_phase.configure(kRate, kChannels);
    single.configure(kRate, kChannels);

    const float a = energy(settle_bars(in_phase, sine(1000.0, 0.5, 4096)));
    const float b = energy(settle_bars(anti_phase, sine(1000.0, 0.5, 4096, true)));
    const float c = energy(settle_bars(single, sine(1000.0, 0.5, 4096, false, true)));

    std::printf("  energia do espectro: em fase %.3f · oposicao de fase %.3f · so esquerda %.3f\n",
                a, b, c);

    // Somar os canais antes da FFT faria a coluna do meio ir a zero.
    PANG_CHECK(b > 0.5f * a,
               "VI-13: canais em oposicao de fase nao fazem o espectro desaparecer");

    const double difference_db = 20.0 * std::log10(std::max(b, 1e-6f) / std::max(c, 1e-6f));
    PANG_CHECK(std::fabs(difference_db) < 3.0,
               "VI-13: a energia em anti-fase fica a menos de 3 dB da de um canal isolado");
}

// -------------------------------------------------------------- VI-06, VI-07

void test_bars_follow_frequency() {
    SpectrumAnalyzer analyzer;
    analyzer.configure(kRate, kChannels);

    std::printf("  barra mais alta por frequencia:");
    int previous = -1;
    bool monotonic = true;
    for (double hz : {100.0, 500.0, 2000.0, 8000.0, 16000.0}) {
        SpectrumAnalyzer fresh;
        fresh.configure(kRate, kChannels);
        const int bar = loudest_bar(settle_bars(fresh, sine(hz, 0.5, 4096)));
        std::printf("  %.0f Hz->%d", hz, bar);
        if (bar <= previous) monotonic = false;
        previous = bar;
    }
    std::printf("\n");
    PANG_CHECK(monotonic,
               "VI-06: frequencias crescentes acendem barras sucessivamente mais a direita");
}

void test_amplitude_maps_to_bar_height() {
    SpectrumAnalyzer loud, quiet;
    loud.configure(kRate, kChannels);
    quiet.configure(kRate, kChannels);

    const auto high = settle_bars(loud, sine(1000.0, 0.5, 4096));
    const auto low = settle_bars(quiet, sine(1000.0, 0.005, 4096));  // -40 dB
    const int bar = loudest_bar(high);

    std::printf("  barra de 1 kHz: amplitude 0,5 -> %.3f · amplitude 0,005 -> %.3f\n",
                high[static_cast<std::size_t>(bar)], low[static_cast<std::size_t>(bar)]);
    PANG_CHECK(high[static_cast<std::size_t>(bar)] > low[static_cast<std::size_t>(bar)] + 0.3f,
               "VI-07: 40 dB de diferenca aparecem como diferenca clara de altura");
    PANG_CHECK(high[static_cast<std::size_t>(bar)] <= 1.0f,
               "VI-07: a escala nao estoura acima de 1.0");
}

// -------------------------------------------------------------------- VI-11

void test_bars_above_source_nyquist_stay_empty() {
    SpectrumAnalyzer analyzer;
    analyzer.configure(kRate, kChannels);

    // Fonte de 16 kHz reamostrada para 44,1 kHz: nada acima de 8 kHz e real.
    analyzer.set_source_nyquist(8000.0f);

    int inactive = 0;
    for (int bar = 0; bar < SpectrumAnalyzer::kBars; ++bar)
        if (!analyzer.bar_active(bar)) ++inactive;
    PANG_CHECK(inactive > 0, "VI-11: ha barras inativas quando a fonte tem Nyquist menor");

    // Mesmo alimentando conteudo nessa regiao, as barras inativas nao acendem.
    settle_bars(analyzer, sine(12000.0, 0.5, 4096));
    bool quiet = true;
    for (int bar = 0; bar < SpectrumAnalyzer::kBars; ++bar)
        if (!analyzer.bar_active(bar)) quiet = quiet && analyzer.bars()[static_cast<std::size_t>(bar)] == 0.0f;
    PANG_CHECK(quiet, "VI-11: barra acima do Nyquist da fonte permanece vazia");
}

// -------------------------------------------------------------- VI-09, VI-15

void test_peaks_hold_then_fall() {
    SpectrumAnalyzer analyzer;
    analyzer.configure(kRate, kChannels);
    settle_bars(analyzer, sine(1000.0, 0.5, 4096));

    const int bar = loudest_bar(analyzer.bars());
    const float peak_after_signal = analyzer.peaks()[static_cast<std::size_t>(bar)];
    PANG_CHECK(peak_after_signal > 0.3f, "o pico subiu com o sinal");

    // Durante a retencao o pico nao pode cair.
    analyzer.advance_peaks(0.1f);
    PANG_CHECK(analyzer.peaks()[static_cast<std::size_t>(bar)] >= peak_after_signal - 1e-6f,
               "VI-09: o pico e retido antes de comecar a cair");

    // O pico nunca fica abaixo da barra, entao para ve-lo cair e preciso a
    // barra descer — ou seja, continuar alimentando, agora com silencio.
    // Sem isso o teste passaria por uma diferenca de quarta casa decimal, que e
    // o mesmo que nao testar.
    const std::vector<float> silence(4096 * kChannels, 0.0f);
    for (int i = 0; i < 60; ++i) {
        analyzer.feed(silence.data(), 4096);
        analyzer.advance_peaks(0.016f);
    }
    const float peak_after_silence = analyzer.peaks()[static_cast<std::size_t>(bar)];
    std::printf("  pico: %.3f apos o sinal, %.3f depois de ~1 s de silencio\n", peak_after_signal,
                peak_after_silence);
    PANG_CHECK(peak_after_silence < peak_after_signal - 0.2f,
               "VI-09: passada a retencao, o pico cai de forma mensuravel");

    analyzer.reset();
    bool zeroed = true;
    for (float v : analyzer.bars()) zeroed = zeroed && v == 0.0f;
    for (float v : analyzer.peaks()) zeroed = zeroed && v == 0.0f;
    PANG_CHECK(zeroed, "VI-15: parada zera barras e picos");
}

// -------------------------------------------------------------------- VI-08

void test_attack_is_faster_than_decay() {
    SpectrumAnalyzer analyzer;
    analyzer.configure(kRate, kChannels);

    const std::vector<float> tone = sine(1000.0, 0.5, 4096);
    const std::vector<float> silence(4096 * kChannels, 0.0f);

    // Um unico quadro de subida a partir do zero.
    analyzer.feed(tone.data(), SpectrumAnalyzer::kFftSize);
    const int bar = loudest_bar(analyzer.bars());
    const float after_one_rise = analyzer.bars()[static_cast<std::size_t>(bar)];

    // Satura e depois mede um unico quadro de descida.
    settle_bars(analyzer, tone);
    const float saturated = analyzer.bars()[static_cast<std::size_t>(bar)];
    analyzer.feed(silence.data(), SpectrumAnalyzer::kFftSize);
    const float after_one_fall = analyzer.bars()[static_cast<std::size_t>(bar)];

    const float rise = after_one_rise;                 // partiu de 0
    const float fall = saturated - after_one_fall;     // partiu do saturado
    std::printf("  suavizacao em um quadro: subida %.3f de %.3f, queda %.3f de %.3f\n", rise,
                saturated, fall, saturated);
    PANG_CHECK(rise > fall * 2.0f,
               "VI-08: a subida e nitidamente mais rapida que a queda");
}

// -------------------------------------------------------------------- VI-16

void test_oscilloscope_is_real_samples() {
    SpectrumAnalyzer analyzer;
    analyzer.configure(kRate, kChannels);

    const std::vector<float> signal = sine(441.0, 0.5, SpectrumAnalyzer::kFftSize);
    analyzer.feed(signal.data(), SpectrumAnalyzer::kFftSize);

    const std::vector<float>& scope = analyzer.scope();
    float maximum = 0.0f;
    for (float v : scope) maximum = std::max(maximum, std::fabs(v));
    std::printf("  osciloscopio: pico %.3f (sinal de entrada 0,5)\n", maximum);
    PANG_CHECK(std::fabs(maximum - 0.5f) < 0.01f,
               "VI-16: o osciloscopio mostra as amostras reais, sem ganho nem invencao");

    // Anti-fase na mistura mono e linha reta — ali isso e a informacao certa.
    SpectrumAnalyzer flipped;
    flipped.configure(kRate, kChannels);
    const std::vector<float> opposed =
        sine(441.0, 0.5, SpectrumAnalyzer::kFftSize, true);
    flipped.feed(opposed.data(), SpectrumAnalyzer::kFftSize);
    float flat = 0.0f;
    for (float v : flipped.scope()) flat = std::max(flat, std::fabs(v));
    PANG_CHECK(flat < 1e-6f, "VI-16: em oposicao de fase a mistura mono do osciloscopio e reta");
}

// ------------------------------------------------- VI-01, VI-17, VI-19, VI-21

void test_capture_ring_protocol() {
    Engine engine(kRate, kChannels);
    engine.load(asset("tone.wav"), State::Playing);
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (engine.snapshot().state == State::Loading &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(2ms);

    std::vector<float> block(512 * kChannels);
    std::vector<float> captured(64 * 1024);

    // VI-17 — com a captura desligada, nada entra no ring.
    engine.set_capture_enabled(false);
    for (int i = 0; i < 20; ++i) engine.render(block.data(), 512);
    PANG_CHECK(engine.read_visualization(captured.data(), captured.size()) == 0,
               "VI-17: captura desligada nao alimenta o ring");

    // VI-01 — ligada, as amostras capturadas sao o sinal real em reproducao.
    engine.set_capture_enabled(true);
    std::this_thread::sleep_for(20ms);
    for (int i = 0; i < 4; ++i) engine.render(block.data(), 512);
    const std::size_t got = engine.read_visualization(captured.data(), captured.size());
    PANG_CHECK(got > 0, "VI-01: captura ligada entrega amostras");

    float maximum = 0.0f;
    for (std::size_t i = 0; i < got; ++i) maximum = std::max(maximum, std::fabs(captured[i]));
    std::printf("  captura: %zu floats, pico %.3f (arquivo de teste tem amplitude 0,5)\n", got,
                maximum);
    PANG_CHECK(maximum > 0.4f && maximum < 0.6f,
               "VI-01: as amostras capturadas sao o audio real, nao dado inventado");

    // VI-21 — enchendo o ring sem consumir, o excesso e DESCARTADO e contado,
    // nunca sobrescrito.
    const std::uint32_t before = engine.snapshot().vis_drops;
    for (int i = 0; i < 200; ++i) engine.render(block.data(), 512);
    const std::uint32_t after = engine.snapshot().vis_drops;
    std::printf("  descartes de visualizacao apos encher o ring: %u\n", after - before);
    PANG_CHECK(after > before,
               "VI-21: bloco que nao cabe e descartado e o descarte e contado");

    // O que sobrou no ring continua integro: nada foi sobrescrito por cima.
    const std::size_t remaining = engine.read_visualization(captured.data(), captured.size());
    bool finite = true;
    for (std::size_t i = 0; i < remaining; ++i) finite = finite && std::isfinite(captured[i]);
    PANG_CHECK(finite, "VI-21: o conteudo remanescente do ring permanece valido");
}

// -------------------------------------------------------------------- VI-20

void test_no_signal_means_no_bars() {
    // Contraprova de VI-20: se o analisador inventasse animacao, as barras
    // subiriam sem entrada alguma.
    SpectrumAnalyzer analyzer;
    analyzer.configure(kRate, kChannels);
    for (int i = 0; i < 100; ++i) analyzer.advance_peaks(0.016f);

    float maximum = 0.0f;
    for (float v : analyzer.bars()) maximum = std::max(maximum, v);
    for (float v : analyzer.peaks()) maximum = std::max(maximum, v);
    PANG_CHECK(maximum == 0.0f,
               "VI-20: sem sinal nao ha barra — nada de aleatorio nem de animacao pronta");
}

}  // namespace

int main() {
    test_sine_lands_on_the_right_bin();
    test_silence_is_numerically_stable();
    test_antiphase_does_not_cancel();
    test_bars_follow_frequency();
    test_amplitude_maps_to_bar_height();
    test_bars_above_source_nyquist_stay_empty();
    test_peaks_hold_then_fall();
    test_attack_is_faster_than_decay();
    test_oscilloscope_is_real_samples();
    test_capture_ring_protocol();
    test_no_signal_means_no_bars();
    return pang::check::exit_code();
}
