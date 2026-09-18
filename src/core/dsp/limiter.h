#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace pang::core::dsp {

// Limitador com lookahead, seguido de clamp rigido.
//
// AU-16 / AU-22 — a garantia e "nenhuma amostra da saida acima do teto". Um
// limitador com ataque de 5 ms e sem lookahead nao entrega isso: durante o
// ataque o pico ja passou. Aqui o detector enxerga o pico antes de ele alcancar
// a saida, porque o audio sai atrasado de kLookaheadSeconds.
//
// O teto e de PICO DE AMOSTRA, em -1.0 dBFS. Nao ha promessa de true peak: a
// reconstrucao no conversor pode ultrapassar a maior amostra em picos
// inter-amostra, e o decibel de margem cobre a maioria dos casos sem o custo de
// deteccao com sobreamostragem 4x.
class Limiter {
public:
    static constexpr float kCeilingDb = -1.0f;
    static constexpr float kLookaheadSeconds = 0.0015f;
    static constexpr float kReleaseSeconds = 0.100f;

    void configure(int sample_rate, int channels);
    void process(float* interleaved, std::uint32_t frames) noexcept;

    // Latencia introduzida, em quadros. Entra no numero reportado pela
    // interface, somada ao buffer do dispositivo.
    int latency_frames() const { return lookahead_; }

    // Atuacoes do clamp rigido. Se o lookahead estiver correto, fica em zero —
    // um valor diferente de zero em teste e falha, nao estatistica.
    std::uint32_t clamp_hits() const { return clamp_hits_.load(std::memory_order_relaxed); }
    void reset_clamp_hits() { clamp_hits_.store(0, std::memory_order_relaxed); }

    float gain_reduction() const { return gain_reduction_.load(std::memory_order_relaxed); }

private:
    float window_min_push(float value) noexcept;

    int channels_ = 2;
    int lookahead_ = 0;
    float ceiling_ = 1.0f;
    float report_threshold_ = 1.0f;
    float attack_step_ = 1.0f;
    float release_step_ = 0.0f;

    std::vector<float> delay_;  // audio atrasado, intercalado
    std::size_t delay_pos_ = 0;

    // Minimo deslizante sobre os ganhos exigidos na janela de lookahead,
    // mantido com uma fila monotonica (wedge) de custo amortizado O(1).
    std::vector<float> wedge_value_;
    std::vector<std::uint64_t> wedge_index_;
    std::size_t wedge_front_ = 0, wedge_back_ = 0;
    std::uint64_t sample_counter_ = 0;

    float gain_ = 1.0f;
    std::atomic<std::uint32_t> clamp_hits_{0};
    std::atomic<float> gain_reduction_{1.0f};
};

}  // namespace pang::core::dsp
