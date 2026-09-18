#include "core/dsp/equalizer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pang::core::dsp {
namespace {

// Sub-bloco de recalculo de coeficientes. 64 amostras a 44,1 kHz sao 1,45 ms:
// fino o bastante para a rampa soar continua, grosso o bastante para o custo do
// recalculo desaparecer.
constexpr std::uint32_t kSubBlock = 64;

constexpr float kRampSeconds = 0.030f;       // EQ-08
constexpr float kCrossfadeSeconds = 0.010f;  // bypass sem estalo

// EQ-11 — acima disto o filtro nao tem onde operar e vira identidade.
constexpr double kNyquistFraction = 0.45;

// Slope dos filtros shelving das extremidades.
constexpr double kShelfSlope = 0.7;

// Limite superior do Q. 12, 14 e 16 kHz estao a menos de meia oitava umas das
// outras; sem o limite, o Q calculado para 14 kHz seria 6,94, audivelmente
// ressonante. O preco e sobreposicao entre as bandas agudas, que e o
// comportamento normal de um equalizador grafico.
constexpr double kMaxQ = 4.0;
constexpr double kMinQ = 0.7;

float db_to_linear(float db) { return std::pow(10.0f, db / 20.0f); }

}  // namespace

const std::array<float, Equalizer::kBands>& Equalizer::frequencies() {
    static const std::array<float, kBands> kFrequencies{60.0f,   170.0f,  310.0f,   600.0f,
                                                        1000.0f, 3000.0f, 6000.0f,  12000.0f,
                                                        14000.0f, 16000.0f};
    return kFrequencies;
}

const std::array<float, Equalizer::kBands>& Equalizer::quality_factors() {
    static const std::array<float, kBands> kQ = [] {
        const auto& f = frequencies();
        std::array<float, kBands> q{};
        for (int i = 0; i < kBands; ++i) {
            if (i == 0 || i == kBands - 1) {
                q[static_cast<std::size_t>(i)] = static_cast<float>(kShelfSlope);
                continue;
            }
            // Largura de banda pela distancia geometrica aos vizinhos.
            const double inf = std::sqrt(double(f[std::size_t(i - 1)]) * f[std::size_t(i)]);
            const double sup = std::sqrt(double(f[std::size_t(i)]) * f[std::size_t(i + 1)]);
            const double octaves = std::log2(sup / inf);
            q[static_cast<std::size_t>(i)] =
                static_cast<float>(std::clamp(q_from_bandwidth(octaves), kMinQ, kMaxQ));
        }
        return q;
    }();
    return kQ;
}

void Equalizer::configure(int sample_rate, int channels) {
    sample_rate_ = sample_rate;
    channels_ = channels;

    state_.assign(static_cast<std::size_t>(kBands) * channels, BiquadState{});
    dry_.assign(static_cast<std::size_t>(kSubBlock) * channels, 0.0f);

    const auto& f = frequencies();
    for (int b = 0; b < kBands; ++b)
        active_[static_cast<std::size_t>(b)] = f[static_cast<std::size_t>(b)] <
                                               kNyquistFraction * sample_rate;

    // Faixa util de 24 dB (de -12 a +12) percorrida em kRampSeconds.
    db_step_ = (2.0f * kRangeDb) * static_cast<float>(kSubBlock) /
               (kRampSeconds * static_cast<float>(sample_rate));
    mix_step_ = 1.0f / (kCrossfadeSeconds * static_cast<float>(sample_rate));

    for (int b = 0; b < kBands; ++b) {
        current_db_[static_cast<std::size_t>(b)] =
            target_db_[static_cast<std::size_t>(b)].load(std::memory_order_relaxed);
        recompute(b);
    }
    current_preamp_ = target_preamp_db_.load(std::memory_order_relaxed);
    current_preamp_gain_ = db_to_linear(current_preamp_);
    mix_ = bypass_.load(std::memory_order_relaxed) ? 0.0f : 1.0f;
}

void Equalizer::recompute(int band) {
    const std::size_t b = static_cast<std::size_t>(band);
    if (!active_[b]) {
        coeffs_[b] = identity();
        return;
    }

    const double f0 = frequencies()[b];
    const double gain = current_db_[b];
    const double q = quality_factors()[b];

    if (band == 0)
        coeffs_[b] = low_shelf(f0, sample_rate_, kShelfSlope, gain);
    else if (band == kBands - 1)
        coeffs_[b] = high_shelf(f0, sample_rate_, kShelfSlope, gain);
    else
        coeffs_[b] = peaking(f0, sample_rate_, q, gain);
}

void Equalizer::set_band_db(int band, float db) {
    if (band < 0 || band >= kBands) return;
    target_db_[static_cast<std::size_t>(band)].store(std::clamp(db, -kRangeDb, kRangeDb),
                                                     std::memory_order_relaxed);
}

void Equalizer::set_preamp_db(float db) {
    target_preamp_db_.store(std::clamp(db, -kRangeDb, kRangeDb), std::memory_order_relaxed);
}

void Equalizer::set_bypass(bool on) { bypass_.store(on, std::memory_order_relaxed); }

void Equalizer::reset() {
    for (auto& t : target_db_) t.store(0.0f, std::memory_order_relaxed);
    target_preamp_db_.store(0.0f, std::memory_order_relaxed);
}

float Equalizer::band_db(int band) const {
    if (band < 0 || band >= kBands) return 0.0f;
    return target_db_[static_cast<std::size_t>(band)].load(std::memory_order_relaxed);
}

float Equalizer::preamp_db() const { return target_preamp_db_.load(std::memory_order_relaxed); }

bool Equalizer::band_active(int band) const {
    if (band < 0 || band >= kBands) return false;
    return active_[static_cast<std::size_t>(band)];
}

void Equalizer::process(float* interleaved, std::uint32_t frames) noexcept {
    if (channels_ == 0) return;

    const bool want_bypass = bypass_.load(std::memory_order_relaxed);

    // EQ-04 / EQ-14 — com o bypass plenamente engatado o estagio inteiro sai do
    // caminho, preamp incluido: o buffer nao e tocado, entao a saida e igual a
    // entrada bit a bit. Nao "multiplicada por 1.0": intocada.
    if (want_bypass && mix_ == 0.0f) return;

    const float mix_target = want_bypass ? 0.0f : 1.0f;

    for (std::uint32_t offset = 0; offset < frames; offset += kSubBlock) {
        // O crossfade pode terminar NO MEIO desta chamada. A partir dai o
        // estagio esta fora do caminho e os sub-blocos restantes tem de passar
        // intactos — sem isso, so o primeiro sub-bloco saia seco e o resto
        // voltava a sair processado.
        if (want_bypass && mix_ == 0.0f) return;

        const std::uint32_t count = std::min<std::uint32_t>(kSubBlock, frames - offset);
        float* block = interleaved + static_cast<std::size_t>(offset) * channels_;
        const std::size_t samples = static_cast<std::size_t>(count) * channels_;

        // EQ-08 — os ganhos caminham para o alvo; coeficientes so sao
        // recalculados quando a banda de fato se moveu.
        for (int b = 0; b < kBands; ++b) {
            const std::size_t bi = static_cast<std::size_t>(b);
            const float target = target_db_[bi].load(std::memory_order_relaxed);
            if (current_db_[bi] == target) continue;
            if (current_db_[bi] < target)
                current_db_[bi] = std::min(target, current_db_[bi] + db_step_);
            else
                current_db_[bi] = std::max(target, current_db_[bi] - db_step_);
            recompute(b);
        }
        const float preamp_target = target_preamp_db_.load(std::memory_order_relaxed);
        if (current_preamp_ != preamp_target) {
            if (current_preamp_ < preamp_target)
                current_preamp_ = std::min(preamp_target, current_preamp_ + db_step_);
            else
                current_preamp_ = std::max(preamp_target, current_preamp_ - db_step_);
            current_preamp_gain_ = db_to_linear(current_preamp_);
        }

        const bool crossfading = mix_ != mix_target;
        if (crossfading) std::memcpy(dry_.data(), block, samples * sizeof(float));

        // Preamp e cascata, por canal.
        for (std::uint32_t f = 0; f < count; ++f) {
            float* frame = block + static_cast<std::size_t>(f) * channels_;
            for (int c = 0; c < channels_; ++c) {
                float x = frame[c] * current_preamp_gain_;
                for (int b = 0; b < kBands; ++b) {
                    const std::size_t si = static_cast<std::size_t>(b) * channels_ + c;
                    x = dsp::process(coeffs_[static_cast<std::size_t>(b)], state_[si], x);
                }
                frame[c] = x;
            }
        }

        if (crossfading) {
            float mix = mix_;
            for (std::uint32_t f = 0; f < count; ++f) {
                if (mix < mix_target)
                    mix = std::min(mix_target, mix + mix_step_);
                else if (mix > mix_target)
                    mix = std::max(mix_target, mix - mix_step_);

                float* frame = block + static_cast<std::size_t>(f) * channels_;
                const float* dry = dry_.data() + static_cast<std::size_t>(f) * channels_;
                for (int c = 0; c < channels_; ++c)
                    frame[c] = frame[c] * mix + dry[c] * (1.0f - mix);
            }
            mix_ = mix;

            // Ao concluir o bypass, zera o estado: a proxima ativacao comeca
            // limpa em vez de soltar o rabo do sinal anterior.
            if (mix_ == 0.0f)
                for (BiquadState& s : state_) s.reset();
        }
    }
}

}  // namespace pang::core::dsp
