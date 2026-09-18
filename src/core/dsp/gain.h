#pragma once

#include <atomic>
#include <cstdint>

namespace pang::core::dsp {

// Ganho escalar com rampa. AU-10 — mudanca de ganho aplicada de uma vez e um
// degrau no sinal, e degrau estala.
class RampedGain {
public:
    void configure(int sample_rate, float ramp_seconds = 0.020f);
    void set_db(float db);
    void set_linear(float linear);
    float target_linear() const { return target_.load(std::memory_order_relaxed); }

    // Salta para o alvo sem rampa. Usar so na troca de faixa, antes de haver
    // audio, onde nao ha nada para estalar.
    void snap();

    void process(float* interleaved, std::uint32_t frames, int channels) noexcept;

private:
    std::atomic<float> target_{1.0f};
    float current_ = 1.0f;
    float step_ = 1.0f;
};

// Volume e balanco estereo.
//
// PL-10 — o balanco atenua o canal oposto em vez de amplificar o pedido, para
// que mover o controle nunca aumente o nivel de pico e estrague a margem que o
// limitador espera encontrar.
class VolumeBalance {
public:
    void configure(int sample_rate, int channels);

    void set_volume(float linear);
    void set_balance(float balance);  // -1 = esquerda, 0 = centro, +1 = direita
    float volume() const { return volume_.load(std::memory_order_relaxed); }
    float balance() const { return balance_.load(std::memory_order_relaxed); }

    void process(float* interleaved, std::uint32_t frames) noexcept;

private:
    int channels_ = 2;
    std::atomic<float> volume_{1.0f};
    std::atomic<float> balance_{0.0f};
    float current_volume_ = 1.0f;
    float current_balance_ = 0.0f;
    float volume_step_ = 1.0f;
    float balance_step_ = 1.0f;
};

}  // namespace pang::core::dsp
