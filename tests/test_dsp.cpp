// Testes do M3 — medicoes de DSP.
//
// Tudo aqui e medicao com limite definido antes, nao inspecao. Os estagios sao
// exercitados isoladamente, com sinais conhecidos, sem dispositivo de audio.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/dsp/biquad.h"
#include "core/dsp/equalizer.h"
#include "core/dsp/gain.h"
#include "core/dsp/limiter.h"
#include "core/util/check.h"

using namespace pang::core;
using namespace pang::core::dsp;

namespace {

constexpr int kRate = 44100;
constexpr int kChannels = 2;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine(double frequency, double amplitude, double seconds, int rate = kRate,
                        int channels = kChannels) {
    const std::size_t frames = static_cast<std::size_t>(seconds * rate);
    std::vector<float> out(frames * channels);
    for (std::size_t f = 0; f < frames; ++f) {
        const float v = static_cast<float>(amplitude * std::sin(2.0 * kPi * frequency * f / rate));
        for (int c = 0; c < channels; ++c) out[f * channels + c] = v;
    }
    return out;
}

// RMS da segunda metade: descarta o transitorio de rampa e de filtro.
double rms_tail(const std::vector<float>& samples) {
    const std::size_t start = samples.size() / 2;
    double sum = 0.0;
    for (std::size_t i = start; i < samples.size(); ++i)
        sum += static_cast<double>(samples[i]) * samples[i];
    return std::sqrt(sum / (samples.size() - start));
}

float peak_of(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (float v : samples) peak = std::max(peak, std::fabs(v));
    return peak;
}

double to_db(double linear) { return 20.0 * std::log10(std::max(linear, 1e-12)); }

// ---------------------------------------------------------------- EQ-03

// Mede o ganho real de uma banda com uma senoide na frequencia de medicao.
double measure_band_gain_db(int band, float requested_db, double measure_hz) {
    Equalizer eq;
    eq.set_band_db(band, requested_db);
    // configure() depois de ajustar: os ganhos partem ja no alvo, sem rampa,
    // que e tambem o caminho de restaurar ajustes salvos.
    eq.configure(kRate, kChannels);

    std::vector<float> signal = sine(measure_hz, 0.25, 1.0);
    const double reference = rms_tail(signal);
    eq.process(signal.data(), static_cast<std::uint32_t>(signal.size() / kChannels));
    return to_db(rms_tail(signal) / reference);
}

void test_equalizer_response() {
    const auto& f = Equalizer::frequencies();

    std::printf("  resposta do equalizador (pedido -> medido):\n");
    for (int band = 0; band < Equalizer::kBands; ++band) {
        // Nas bandas shelving o ganho pedido e o do PLATO, nao o do ponto f0:
        // num shelf RBJ a resposta em f0 e metade do ganho. Medir em 60 Hz um
        // low-shelf de +12 dB daria +6 dB e reprovaria por engano.
        double measure_hz = f[static_cast<std::size_t>(band)];
        const char* note = "";
        if (band == 0) {
            measure_hz = 20.0;
            note = " (plato do shelf)";
        } else if (band == Equalizer::kBands - 1) {
            measure_hz = 20000.0;
            note = " (plato do shelf)";
        }

        for (float requested : {-12.0f, -6.0f, 6.0f, 12.0f}) {
            const double measured = measure_band_gain_db(band, requested, measure_hz);
            std::printf("    %6.0f Hz%s  %+6.1f dB -> %+6.2f dB\n", f[std::size_t(band)], note,
                        requested, measured);
            PANG_CHECK(std::fabs(measured - requested) <= 1.0,
                       "EQ-03: resposta da banda dentro de +/-1 dB do ganho pedido");
        }
    }
}

// ---------------------------------------------------------- EQ-04, EQ-14

void test_bypass_is_bit_exact() {
    Equalizer eq;
    eq.set_preamp_db(9.0f);  // EQ-14 — o preamp tambem tem de sair do caminho
    for (int b = 0; b < Equalizer::kBands; ++b) eq.set_band_db(b, 8.0f);
    eq.set_bypass(true);
    eq.configure(kRate, kChannels);

    // Passa audio suficiente para concluir o crossfade de 10 ms.
    std::vector<float> warmup = sine(1000.0, 0.5, 0.2);
    eq.process(warmup.data(), static_cast<std::uint32_t>(warmup.size() / kChannels));

    const std::vector<float> input = sine(1000.0, 0.5, 0.1);
    std::vector<float> output = input;
    eq.process(output.data(), static_cast<std::uint32_t>(output.size() / kChannels));

    bool identical = true;
    for (std::size_t i = 0; i < input.size(); ++i) identical = identical && input[i] == output[i];
    PANG_CHECK(identical,
               "EQ-04/EQ-14: concluido o crossfade, a saida e igual a entrada bit a bit, "
               "preamp incluido");
}

void test_bypass_transition_is_smooth() {
    // Medir o salto entre amostras contra um limite absoluto nao funciona: com
    // dez bandas em +12 dB o sinal processado fica varias vezes maior que a
    // entrada, e o proprio declive da senoide amplificada ja produz saltos
    // grandes. O limite tem de sair do SINAL, nao de um numero escolhido.
    //
    // O controle e a mesma passagem sem trocar o bypass. Se o crossfade for
    // suave, o maior salto durante a transicao nao passa do maior salto do
    // controle — o bypass so reduz amplitude, entao os saltos so encolhem.
    auto max_jump = [](const std::vector<float>& signal, std::size_t from) {
        float maximum = 0.0f;
        for (std::size_t i = std::max(from, std::size_t(kChannels)); i < signal.size();
             i += kChannels)
            maximum = std::max(maximum, std::fabs(signal[i] - signal[i - kChannels]));
        return maximum;
    };

    auto run = [](bool toggle_bypass) {
        Equalizer eq;
        for (int b = 0; b < Equalizer::kBands; ++b) eq.set_band_db(b, 12.0f);
        eq.configure(kRate, kChannels);

        std::vector<float> signal = sine(1000.0, 0.3, 0.3);
        const std::uint32_t frames = static_cast<std::uint32_t>(signal.size() / kChannels);
        eq.process(signal.data(), frames / 2);
        if (toggle_bypass) eq.set_bypass(true);
        eq.process(signal.data() + static_cast<std::size_t>(frames / 2) * kChannels, frames / 2);
        return signal;
    };

    const std::vector<float> control = run(false);
    const std::vector<float> switched = run(true);
    const std::size_t transition = control.size() / 2;

    const float control_jump = max_jump(control, 0);
    const float switched_jump = max_jump(switched, transition);

    std::printf("  maior salto por amostra: controle %.4f, durante o bypass %.4f\n", control_jump,
                switched_jump);
    PANG_CHECK(switched_jump <= control_jump * 1.05f + 1e-6f,
               "EQ-15: a transicao de bypass nao introduz salto maior que o do proprio sinal");

    // E o bypass de fato chegou ao fim: a cauda tem de estar no nivel seco.
    const std::size_t tail_start = switched.size() - switched.size() / 8;
    float tail_peak = 0.0f;
    for (std::size_t i = tail_start; i < switched.size(); ++i)
        tail_peak = std::max(tail_peak, std::fabs(switched[i]));
    std::printf("  pico da cauda apos o bypass: %.4f (entrada era 0,3)\n", tail_peak);
    PANG_CHECK(tail_peak < 0.35f, "apos o crossfade o sinal volta ao nivel seco");
}

// ---------------------------------------------------------------- EQ-11

void test_nyquist_bands() {
    Equalizer eq;
    eq.configure(22050, kChannels);  // Nyquist 11025 Hz

    PANG_CHECK(eq.band_active(6), "6 kHz continua ativa a 22050 Hz");
    PANG_CHECK(!eq.band_active(7) && !eq.band_active(8) && !eq.band_active(9),
               "EQ-11: 12, 14 e 16 kHz ficam inativas a 22050 Hz");

    // Bandas inativas nao podem alterar o sinal nem instabilizar.
    for (int b = 7; b < Equalizer::kBands; ++b) eq.set_band_db(b, 12.0f);
    eq.configure(22050, kChannels);

    const std::vector<float> input = sine(1000.0, 0.4, 0.2, 22050);
    std::vector<float> output = input;
    eq.process(output.data(), static_cast<std::uint32_t>(output.size() / kChannels));

    double difference = 0.0;
    bool finite = true;
    for (std::size_t i = 0; i < input.size(); ++i) {
        difference = std::max(difference, std::fabs(double(output[i]) - input[i]));
        finite = finite && std::isfinite(output[i]);
    }
    PANG_CHECK(finite, "EQ-11: banda acima de Nyquist nao gera valor invalido");
    PANG_CHECK(difference < 1e-6, "EQ-11: banda acima de Nyquist vira identidade");
}

void test_equalizer_reset() {
    Equalizer eq;
    for (int b = 0; b < Equalizer::kBands; ++b) eq.set_band_db(b, 10.0f);
    eq.set_preamp_db(5.0f);
    eq.reset();
    bool flat = eq.preamp_db() == 0.0f;
    for (int b = 0; b < Equalizer::kBands; ++b) flat = flat && eq.band_db(b) == 0.0f;
    PANG_CHECK(flat, "EQ-05: reset devolve resposta plana e preamp em 0 dB");
}

void test_q_table() {
    const auto& q = Equalizer::quality_factors();
    std::printf("  Q por banda:");
    for (int b = 1; b < Equalizer::kBands - 1; ++b) std::printf(" %.2f", q[std::size_t(b)]);
    std::printf("\n");

    PANG_CHECK(q[8] <= 4.0f + 1e-6f,
               "EQ-13: o Q de 14 kHz fica no teto de 4.0, e nao no 6.94 calculado");
    bool in_range = true;
    for (int b = 1; b < Equalizer::kBands - 1; ++b)
        in_range = in_range && q[std::size_t(b)] >= 0.7f && q[std::size_t(b)] <= 4.0f;
    PANG_CHECK(in_range, "EQ-13: todo Q fica na faixa documentada [0.7, 4.0]");
}

// -------------------------------------------------------- AU-16, AU-22, AU-23

void test_limiter_ceiling() {
    Limiter limiter;
    limiter.configure(kRate, kChannels);

    const float ceiling = std::pow(10.0f, Limiter::kCeilingDb / 20.0f);

    // Senoide a +12 dBFS: quatro vezes acima do teto.
    std::vector<float> signal = sine(440.0, 4.0, 1.0);
    limiter.process(signal.data(), static_cast<std::uint32_t>(signal.size() / kChannels));

    const float peak = peak_of(signal);
    std::printf("  limitador, senoide +12 dBFS: pico de saida %.4f (%.2f dBFS), teto %.4f\n", peak,
                to_db(peak), ceiling);

    PANG_CHECK(peak <= ceiling + 1e-6f,
               "AU-16: nenhuma amostra da saida acima de -1.0 dBFS");
    PANG_CHECK(limiter.clamp_hits() == 0,
               "AU-22: o lookahead conteve tudo — o clamp rigido nao precisou atuar");
}

void test_limiter_single_sample_transient() {
    Limiter limiter;
    limiter.configure(kRate, kChannels);
    const float ceiling = std::pow(10.0f, Limiter::kCeilingDb / 20.0f);

    // Silencio com um unico estouro: o caso que derruba limitador sem lookahead.
    std::vector<float> signal(static_cast<std::size_t>(kRate) * kChannels, 0.0f);
    signal[5000 * kChannels] = 1.0f;
    signal[5000 * kChannels + 1] = 1.0f;

    limiter.process(signal.data(), static_cast<std::uint32_t>(signal.size() / kChannels));

    const float peak = peak_of(signal);
    std::printf("  limitador, transiente de 1 amostra a 0 dBFS: pico de saida %.4f\n", peak);
    PANG_CHECK(peak <= ceiling + 1e-6f, "AU-22: transiente de uma amostra fica sob o teto");
    PANG_CHECK(limiter.clamp_hits() == 0,
               "AU-22: contador do clamp rigido permanece zero");
}

void test_limiter_latency() {
    Limiter limiter;
    limiter.configure(kRate, kChannels);
    const int expected = static_cast<int>(Limiter::kLookaheadSeconds * kRate);
    std::printf("  latencia do limitador: %d quadros (%.2f ms)\n", limiter.latency_frames(),
                1000.0 * limiter.latency_frames() / kRate);
    PANG_CHECK(limiter.latency_frames() == expected,
               "AU-23: latencia reportada e exatamente o lookahead configurado");
}

void test_limiter_is_transparent_below_ceiling() {
    Limiter limiter;
    limiter.configure(kRate, kChannels);

    // Sinal confortavelmente abaixo do teto: o limitador nao deve mexer no
    // nivel, so atrasar.
    std::vector<float> signal = sine(440.0, 0.5, 0.5);
    const double before = rms_tail(signal);
    limiter.process(signal.data(), static_cast<std::uint32_t>(signal.size() / kChannels));
    const double after = rms_tail(signal);

    PANG_CHECK(std::fabs(to_db(after / before)) < 0.05,
               "abaixo do teto o limitador e transparente (menos de 0,05 dB)");
    PANG_CHECK(limiter.clamp_hits() == 0, "sem clamp em sinal abaixo do teto");
}

// ------------------------------------------------------------ PL-09, PL-10

void test_volume_and_balance() {
    auto measure_channels = [](float volume, float balance) {
        VolumeBalance stage;
        stage.set_volume(volume);
        stage.set_balance(balance);
        stage.configure(kRate, kChannels);

        std::vector<float> signal = sine(440.0, 0.5, 0.5);
        stage.process(signal.data(), static_cast<std::uint32_t>(signal.size() / kChannels));

        double left = 0.0, right = 0.0;
        const std::size_t start = signal.size() / 2 / kChannels * kChannels;
        std::size_t count = 0;
        for (std::size_t i = start; i + 1 < signal.size(); i += kChannels) {
            left += double(signal[i]) * signal[i];
            right += double(signal[i + 1]) * signal[i + 1];
            ++count;
        }
        return std::pair<double, double>{std::sqrt(left / count), std::sqrt(right / count)};
    };

    const auto centered = measure_channels(1.0f, 0.0f);
    PANG_CHECK(std::fabs(to_db(centered.first / centered.second)) < 0.1,
               "PL-10: no centro, os dois canais ficam a menos de 0,1 dB um do outro");

    const auto half = measure_channels(0.5f, 0.0f);
    std::printf("  volume 0,5: %.2f dB em relacao a 1,0\n", to_db(half.first / centered.first));
    PANG_CHECK(std::fabs(to_db(half.first / centered.first) + 6.02) < 0.1,
               "PL-09: volume 0,5 atenua 6,02 dB");

    const auto left_only = measure_channels(1.0f, -1.0f);
    PANG_CHECK(left_only.second < 1e-5, "PL-10: balanco todo a esquerda silencia a direita");
    PANG_CHECK(std::fabs(to_db(left_only.first / centered.first)) < 0.1,
               "PL-10: o canal escolhido nao e amplificado, so o oposto e atenuado");

    const auto right_only = measure_channels(1.0f, 1.0f);
    PANG_CHECK(right_only.first < 1e-5, "PL-10: balanco todo a direita silencia a esquerda");
}

// ------------------------------------------------------------------- AU-10

void test_gain_ramp_has_no_step() {
    VolumeBalance stage;
    stage.set_volume(1.0f);
    stage.configure(kRate, kChannels);

    // Sinal constante deixa o degrau de ganho visivel sem o sinal mascarar.
    std::vector<float> signal(static_cast<std::size_t>(kRate / 10) * kChannels, 0.5f);
    stage.process(signal.data(), 100);  // estabiliza em 1.0
    stage.set_volume(0.0f);             // mudanca abrupta pedida pela interface
    stage.process(signal.data() + 100 * kChannels,
                  static_cast<std::uint32_t>(signal.size() / kChannels) - 100);

    float max_jump = 0.0f;
    for (std::size_t i = 100 * kChannels + kChannels; i < signal.size(); i += kChannels)
        max_jump = std::max(max_jump, std::fabs(signal[i] - signal[i - kChannels]));

    // Rampa de 20 ms sobre amplitude 0,5: o salto por amostra nao pode passar
    // de 0,5 / (0,020 * 44100) = 0,00057. Margem de duas vezes.
    const float limit = 0.5f / (0.020f * kRate) * 2.0f;
    std::printf("  maior salto por amostra na rampa de volume: %.6f (limite %.6f)\n", max_jump,
                limit);
    PANG_CHECK(max_jump < limit, "AU-10: mudanca de volume vira rampa, nao degrau");
}

// ------------------------------------------------------------- biquad

void test_biquad_identity_and_stability() {
    // Ganho zero tem de devolver identidade exata, nao "quase".
    const BiquadCoeffs flat = peaking(1000.0, kRate, 1.0, 0.0);
    BiquadState state;
    bool exact = true;
    for (int i = 0; i < 100; ++i) {
        const float x = static_cast<float>(std::sin(i * 0.1));
        exact = exact && pang::core::dsp::process(flat, state, x) == x;
    }
    PANG_CHECK(exact, "banda em 0 dB e identidade exata, sem residuo de arredondamento");

    // Q extremo nao pode divergir.
    const BiquadCoeffs sharp = peaking(16000.0, kRate, 4.0, 12.0);
    BiquadState sharp_state;
    bool finite = true;
    float maximum = 0.0f;
    for (int i = 0; i < 100000; ++i) {
        const float y = pang::core::dsp::process(
            sharp, sharp_state, static_cast<float>(std::sin(2.0 * kPi * 16000.0 * i / kRate)));
        finite = finite && std::isfinite(y);
        maximum = std::max(maximum, std::fabs(y));
    }
    PANG_CHECK(finite, "filtro com Q no teto permanece numericamente estavel");
    PANG_CHECK(maximum < 10.0f, "filtro com Q no teto nao diverge em regime permanente");
}

}  // namespace

int main() {
    test_q_table();
    test_biquad_identity_and_stability();
    test_equalizer_response();
    test_bypass_is_bit_exact();
    test_bypass_transition_is_smooth();
    test_nyquist_bands();
    test_equalizer_reset();
    test_limiter_ceiling();
    test_limiter_single_sample_transient();
    test_limiter_latency();
    test_limiter_is_transparent_below_ceiling();
    test_volume_and_balance();
    test_gain_ramp_has_no_step();
    return pang::check::exit_code();
}
