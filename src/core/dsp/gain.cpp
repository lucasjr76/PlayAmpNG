#include "core/dsp/gain.h"

#include <algorithm>
#include <cmath>

namespace pang::core::dsp {
namespace {

float db_to_linear(float db) { return std::pow(10.0f, db / 20.0f); }

float approach(float current, float target, float step) {
    if (current < target) return std::min(target, current + step);
    if (current > target) return std::max(target, current - step);
    return current;
}

}  // namespace

void RampedGain::configure(int sample_rate, float ramp_seconds) {
    // Passo dimensionado para percorrer ganho 0..1 no tempo de rampa.
    step_ = 1.0f / (ramp_seconds * static_cast<float>(sample_rate));
    current_ = target_.load(std::memory_order_relaxed);
}

void RampedGain::set_db(float db) { set_linear(db_to_linear(db)); }

void RampedGain::set_linear(float linear) {
    target_.store(std::max(0.0f, linear), std::memory_order_relaxed);
}

void RampedGain::snap() { current_ = target_.load(std::memory_order_relaxed); }

void RampedGain::process(float* interleaved, std::uint32_t frames, int channels) noexcept {
    const float target = target_.load(std::memory_order_relaxed);
    if (current_ == target && target == 1.0f) return;  // nada a fazer

    for (std::uint32_t f = 0; f < frames; ++f) {
        current_ = approach(current_, target, step_);
        float* frame = interleaved + static_cast<std::size_t>(f) * channels;
        for (int c = 0; c < channels; ++c) frame[c] *= current_;
    }
}

void VolumeBalance::configure(int sample_rate, int channels) {
    channels_ = channels;
    volume_step_ = 1.0f / (0.020f * static_cast<float>(sample_rate));
    balance_step_ = 2.0f / (0.020f * static_cast<float>(sample_rate));
    current_volume_ = volume_.load(std::memory_order_relaxed);
    current_balance_ = balance_.load(std::memory_order_relaxed);
}

void VolumeBalance::set_volume(float linear) {
    volume_.store(std::clamp(linear, 0.0f, 1.0f), std::memory_order_relaxed);
}

void VolumeBalance::set_balance(float balance) {
    balance_.store(std::clamp(balance, -1.0f, 1.0f), std::memory_order_relaxed);
}

void VolumeBalance::process(float* interleaved, std::uint32_t frames) noexcept {
    const float volume_target = volume_.load(std::memory_order_relaxed);
    const float balance_target = balance_.load(std::memory_order_relaxed);

    for (std::uint32_t f = 0; f < frames; ++f) {
        current_volume_ = approach(current_volume_, volume_target, volume_step_);
        current_balance_ = approach(current_balance_, balance_target, balance_step_);

        // So atenua: balance -1 zera a direita, +1 zera a esquerda.
        const float left = current_balance_ > 0.0f ? 1.0f - current_balance_ : 1.0f;
        const float right = current_balance_ < 0.0f ? 1.0f + current_balance_ : 1.0f;

        float* frame = interleaved + static_cast<std::size_t>(f) * channels_;
        if (channels_ >= 2) {
            frame[0] *= current_volume_ * left;
            frame[1] *= current_volume_ * right;
            for (int c = 2; c < channels_; ++c) frame[c] *= current_volume_;
        } else {
            for (int c = 0; c < channels_; ++c) frame[c] *= current_volume_;
        }
    }
}

}  // namespace pang::core::dsp
