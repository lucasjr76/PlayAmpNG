#include "core/dsp/limiter.h"

#include <algorithm>
#include <cmath>

namespace pang::core::dsp {

void Limiter::configure(int sample_rate, int channels) {
    channels_ = channels;
    lookahead_ = std::max(1, static_cast<int>(kLookaheadSeconds * sample_rate));
    ceiling_ = std::pow(10.0f, kCeilingDb / 20.0f);

    delay_.assign(static_cast<std::size_t>(lookahead_) * channels, 0.0f);
    delay_pos_ = 0;

    // A janela tem lookahead_+1 entradas; o wedge nunca guarda mais que isso.
    const std::size_t capacity = static_cast<std::size_t>(lookahead_) + 2;
    wedge_value_.assign(capacity, 1.0f);
    wedge_index_.assign(capacity, 0);
    wedge_front_ = wedge_back_ = 0;
    sample_counter_ = 0;

    // Passo de ataque: 1/lookahead por amostra garante alcancar QUALQUER alvo
    // dentro da janela, porque o ganho vive em [0, 1]. E essa a garantia dura
    // que o ataque exponencial de um limitador sem lookahead nao da.
    attack_step_ = 1.0f / static_cast<float>(lookahead_);
    release_step_ = 1.0f / (kReleaseSeconds * static_cast<float>(sample_rate));

    gain_ = 1.0f;
    clamp_hits_.store(0, std::memory_order_relaxed);
    gain_reduction_.store(1.0f, std::memory_order_relaxed);
}

// Insere o ganho exigido pelo quadro atual e devolve o minimo da janela.
float Limiter::window_min_push(float value) noexcept {
    const std::size_t capacity = wedge_value_.size();

    // Remove do fim tudo que e maior ou igual: nunca sera o minimo enquanto
    // este valor estiver na janela.
    while (wedge_back_ != wedge_front_) {
        const std::size_t last = (wedge_back_ + capacity - 1) % capacity;
        if (wedge_value_[last] >= value)
            wedge_back_ = last;
        else
            break;
    }
    wedge_value_[wedge_back_] = value;
    wedge_index_[wedge_back_] = sample_counter_;
    wedge_back_ = (wedge_back_ + 1) % capacity;

    // Expira o que saiu da janela pela frente.
    const std::uint64_t oldest =
        sample_counter_ >= static_cast<std::uint64_t>(lookahead_)
            ? sample_counter_ - static_cast<std::uint64_t>(lookahead_)
            : 0;
    while (wedge_index_[wedge_front_] < oldest) wedge_front_ = (wedge_front_ + 1) % capacity;

    ++sample_counter_;
    return wedge_value_[wedge_front_];
}

void Limiter::process(float* interleaved, std::uint32_t frames) noexcept {
    if (lookahead_ <= 0 || channels_ <= 0) return;

    float min_gain_seen = 1.0f;

    for (std::uint32_t f = 0; f < frames; ++f) {
        float* frame = interleaved + static_cast<std::size_t>(f) * channels_;

        // Pico do quadro que ACABA de entrar — ainda nao sai na saida.
        float peak = 0.0f;
        for (int c = 0; c < channels_; ++c) peak = std::max(peak, std::fabs(frame[c]));

        const float required = peak > ceiling_ ? ceiling_ / peak : 1.0f;
        const float target = window_min_push(required);

        // Ataque limitado pelo passo, nunca instantaneo: uma queda abrupta do
        // ganho seria ela propria uma descontinuidade. O passo de 1/lookahead
        // garante chegar ao alvo antes de o pico alcancar a saida.
        if (target < gain_)
            gain_ = std::max(target, gain_ - attack_step_);
        else
            gain_ = std::min(target, gain_ + release_step_);

        min_gain_seen = std::min(min_gain_seen, gain_);

        // Troca o quadro de entrada pelo quadro atrasado, aplicando o ganho.
        float* slot = delay_.data() + delay_pos_ * channels_;
        for (int c = 0; c < channels_; ++c) {
            const float delayed = slot[c];
            slot[c] = frame[c];

            float out = delayed * gain_;

            // Clamp rigido: a garantia dura de que o teto vale bit a bit. Se o
            // lookahead estiver correto, nunca atua.
            if (out > ceiling_) {
                out = ceiling_;
                clamp_hits_.fetch_add(1, std::memory_order_relaxed);
            } else if (out < -ceiling_) {
                out = -ceiling_;
                clamp_hits_.fetch_add(1, std::memory_order_relaxed);
            }
            frame[c] = out;
        }
        delay_pos_ = (delay_pos_ + 1) % static_cast<std::size_t>(lookahead_);
    }

    gain_reduction_.store(min_gain_seen, std::memory_order_relaxed);
}

}  // namespace pang::core::dsp
