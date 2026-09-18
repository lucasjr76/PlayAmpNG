#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "core/dsp/biquad.h"

namespace pang::core::dsp {

// Equalizador grafico de 10 bandas com pre-amplificador.
//
// Tipo de filtro e Q por banda estao em ARCHITECTURE.md secao 7. Em resumo:
// bandas 1 e 10 sao shelving, as demais peaking, e o Q sai da distancia
// geometrica aos vizinhos, limitado a 4.0 para que 12, 14 e 16 kHz — que estao
// a menos de meia oitava umas das outras — nao virem filtros ressonantes.
//
// Parametros entram pelo thread da interface (atomicos); process() roda no
// thread de audio e nao aloca, nao trava e nao espera.
class Equalizer {
public:
    static constexpr int kBands = 10;
    static constexpr float kRangeDb = 12.0f;

    // 60 Hz a 16 kHz, as frequencias de referencia da especificacao.
    static const std::array<float, kBands>& frequencies();

    // Q efetivo de cada banda peaking (shelving usa slope, nao Q).
    static const std::array<float, kBands>& quality_factors();

    void configure(int sample_rate, int channels);

    // --- interface (seguros concorrentemente com process)
    void set_band_db(int band, float db);
    void set_preamp_db(float db);
    void set_bypass(bool on);
    void reset();  // EQ-05 — resposta plana, preamp em 0 dB

    float band_db(int band) const;
    float preamp_db() const;
    bool bypass() const { return bypass_.load(std::memory_order_relaxed); }

    // EQ-11 — false quando a banda esta em Nyquist ou acima e foi substituida
    // por identidade. A interface desabilita o controle correspondente.
    bool band_active(int band) const;

    // --- thread de audio
    void process(float* interleaved, std::uint32_t frames) noexcept;

private:
    void recompute(int band);

    int sample_rate_ = 0;
    int channels_ = 0;

    std::array<std::atomic<float>, kBands> target_db_{};
    std::atomic<float> target_preamp_db_{0.0f};
    std::atomic<bool> bypass_{false};

    // Estado do thread de audio.
    std::array<float, kBands> current_db_{};
    float current_preamp_ = 0.0f;
    float current_preamp_gain_ = 1.0f;
    std::array<BiquadCoeffs, kBands> coeffs_{};
    std::array<bool, kBands> active_{};
    std::vector<BiquadState> state_;  // banda * canal

    float mix_ = 1.0f;       // 1 = processado, 0 = seco (bypass concluido)
    float mix_step_ = 0.0f;  // passo do crossfade por amostra
    float db_step_ = 0.0f;   // passo da rampa de ganho por sub-bloco
    std::vector<float> dry_;
};

}  // namespace pang::core::dsp
