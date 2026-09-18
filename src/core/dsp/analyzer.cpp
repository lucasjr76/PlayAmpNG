#include "core/dsp/analyzer.h"

#include <algorithm>
#include <cmath>

namespace pang::core::dsp {
namespace {

// Frequencia mais grave representada. Abaixo disso a resolucao de 43 Hz por bin
// nao distingue nada util.
constexpr float kLowestHz = 60.0f;

// Suavizacao temporal: sobe rapido para nao perder transiente, desce devagar
// para a leitura nao piscar.
constexpr float kAttack = 0.6f;
constexpr float kDecay = 0.15f;

constexpr float kPeakHoldSeconds = 0.5f;
constexpr float kPeakFallPerSecond = 0.9f;
constexpr float kPeakFallAcceleration = 2.2f;

// VI-12 — piso rigido antes do logaritmo. Em silencio absoluto a magnitude e
// exatamente zero, e log(0) e -inf; sem esta trava o espectro inteiro viraria
// NaN na primeira pausa.
constexpr float kMagnitudeFloor = 1e-10f;

float to_normalized_db(float magnitude) {
    const float db = 20.0f * std::log10(std::max(magnitude, kMagnitudeFloor));
    return std::clamp((db - SpectrumAnalyzer::kFloorDb) / -SpectrumAnalyzer::kFloorDb, 0.0f, 1.0f);
}

}  // namespace

void SpectrumAnalyzer::configure(int sample_rate, int channels) {
    sample_rate_ = sample_rate;
    channels_ = channels;
    source_nyquist_ = static_cast<float>(sample_rate) / 2.0f;

    fft_.configure(kFftSize);
    window_.configure(kFftSize);

    // Normalizacao 2/(N * ganho_coerente): faz uma senoide de amplitude 1.0
    // resultar em 0 dBFS no bin correspondente — condicao diretamente testavel.
    normalization_ = 2.0f / (static_cast<float>(kFftSize) * window_.coherent_gain());

    real_.assign(kFftSize, 0.0f);
    imaginary_.assign(kFftSize, 0.0f);
    left_.assign(kFftSize, 0.0f);
    right_.assign(kFftSize, 0.0f);
    scope_.assign(kFftSize, 0.0f);
    filled_ = 0;

    // Distribuicao logaritmica de kLowestHz ate Nyquist do dispositivo.
    const float top = static_cast<float>(sample_rate) / 2.0f;
    const float ratio = top / kLowestHz;
    for (int i = 0; i <= kBars; ++i) {
        const float hz = kLowestHz * std::pow(ratio, static_cast<float>(i) / kBars);
        int bin = static_cast<int>(std::lround(hz * kFftSize / sample_rate));
        edges_[static_cast<std::size_t>(i)] = std::clamp(bin, 1, kFftSize / 2);
    }
    // Garante que nenhuma barra fique sem bin proprio nas oitavas graves.
    for (int i = 1; i <= kBars; ++i)
        edges_[static_cast<std::size_t>(i)] =
            std::max(edges_[static_cast<std::size_t>(i)],
                     edges_[static_cast<std::size_t>(i - 1)] + 1);

    set_source_nyquist(top);
    reset();
}

void SpectrumAnalyzer::set_source_nyquist(float hz) {
    source_nyquist_ = hz;
    for (int bar = 0; bar < kBars; ++bar) {
        // Inativa quando toda a faixa da barra esta acima do que a fonte pode
        // conter. A barra fica vazia em vez de mostrar residuo de reamostragem.
        active_[static_cast<std::size_t>(bar)] = bar_low_hz(bar) < hz;
        if (!active_[static_cast<std::size_t>(bar)]) {
            bars_[static_cast<std::size_t>(bar)] = 0.0f;
            peaks_[static_cast<std::size_t>(bar)] = 0.0f;
        }
    }
}

bool SpectrumAnalyzer::bar_active(int bar) const {
    if (bar < 0 || bar >= kBars) return false;
    return active_[static_cast<std::size_t>(bar)];
}

float SpectrumAnalyzer::bar_low_hz(int bar) const {
    if (bar < 0 || bar >= kBars || sample_rate_ == 0) return 0.0f;
    return static_cast<float>(edges_[static_cast<std::size_t>(bar)]) * sample_rate_ / kFftSize;
}

float SpectrumAnalyzer::bar_high_hz(int bar) const {
    if (bar < 0 || bar >= kBars || sample_rate_ == 0) return 0.0f;
    return static_cast<float>(edges_[static_cast<std::size_t>(bar + 1)]) * sample_rate_ / kFftSize;
}

void SpectrumAnalyzer::feed(const float* interleaved, std::size_t frames) {
    if (channels_ == 0) return;

    for (std::size_t f = 0; f < frames; ++f) {
        const float* frame = interleaved + f * channels_;
        left_[filled_] = frame[0];
        right_[filled_] = channels_ > 1 ? frame[1] : frame[0];
        ++filled_;
        if (filled_ == kFftSize) {
            compute_frame();
            filled_ = 0;
        }
    }
}

void SpectrumAnalyzer::compute_frame() {
    // Osciloscopio: mistura mono do bloco recem-completado.
    for (int i = 0; i < kFftSize; ++i)
        scope_[static_cast<std::size_t>(i)] =
            0.5f * (left_[static_cast<std::size_t>(i)] + right_[static_cast<std::size_t>(i)]);

    // Truque das duas FFTs reais pelo preco de uma: L entra na parte real, R na
    // imaginaria. Somar os canais ANTES da transformada faria o espectro
    // desaparecer em material com fases opostas, que e exatamente o defeito que
    // a especificacao manda evitar.
    const std::vector<float>& w = window_.values();
    for (int i = 0; i < kFftSize; ++i) {
        const std::size_t n = static_cast<std::size_t>(i);
        real_[n] = left_[n] * w[n];
        imaginary_[n] = right_[n] * w[n];
    }
    fft_.forward(real_.data(), imaginary_.data());

    std::array<float, kBars> frame{};
    for (int bar = 0; bar < kBars; ++bar) {
        if (!active_[static_cast<std::size_t>(bar)]) continue;

        const int from = edges_[static_cast<std::size_t>(bar)];
        const int to = std::min(edges_[static_cast<std::size_t>(bar + 1)], kFftSize / 2);

        float magnitude = 0.0f;
        for (int k = from; k < to; ++k) {
            // Separacao dos dois espectros a partir do resultado empacotado.
            const int mirror = (kFftSize - k) % kFftSize;
            const float lr = 0.5f * (real_[static_cast<std::size_t>(k)] +
                                     real_[static_cast<std::size_t>(mirror)]);
            const float li = 0.5f * (imaginary_[static_cast<std::size_t>(k)] -
                                     imaginary_[static_cast<std::size_t>(mirror)]);
            const float rr = 0.5f * (imaginary_[static_cast<std::size_t>(k)] +
                                     imaginary_[static_cast<std::size_t>(mirror)]);
            const float ri = -0.5f * (real_[static_cast<std::size_t>(k)] -
                                      real_[static_cast<std::size_t>(mirror)]);

            const float left_power = lr * lr + li * li;
            const float right_power = rr * rr + ri * ri;

            // Combinacao DEPOIS da transformada: magnitudes, nao amostras.
            const float combined = std::sqrt(0.5f * (left_power + right_power)) * normalization_;
            magnitude = std::max(magnitude, combined);
        }
        frame[static_cast<std::size_t>(bar)] = to_normalized_db(magnitude);
    }

    for (int bar = 0; bar < kBars; ++bar) {
        const std::size_t b = static_cast<std::size_t>(bar);
        const float target = frame[b];
        const float alpha = target > bars_[b] ? kAttack : kDecay;
        bars_[b] += alpha * (target - bars_[b]);

        if (bars_[b] >= peaks_[b]) {
            peaks_[b] = bars_[b];
            peak_hold_[b] = kPeakHoldSeconds;
            peak_fall_[b] = 0.0f;
        }
    }
}

void SpectrumAnalyzer::advance_peaks(float dt_seconds) {
    for (int bar = 0; bar < kBars; ++bar) {
        const std::size_t b = static_cast<std::size_t>(bar);
        if (peak_hold_[b] > 0.0f) {
            peak_hold_[b] -= dt_seconds;
            continue;
        }
        peak_fall_[b] += kPeakFallAcceleration * dt_seconds;
        peaks_[b] = std::max(bars_[b],
                             peaks_[b] - (kPeakFallPerSecond + peak_fall_[b]) * dt_seconds);
    }
}

void SpectrumAnalyzer::reset() {
    bars_.fill(0.0f);
    peaks_.fill(0.0f);
    peak_hold_.fill(0.0f);
    peak_fall_.fill(0.0f);
    std::fill(scope_.begin(), scope_.end(), 0.0f);
    filled_ = 0;
}

void SpectrumAnalyzer::hold() { filled_ = 0; }

}  // namespace pang::core::dsp
