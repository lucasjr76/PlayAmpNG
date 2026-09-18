#include "core/dsp/fft.h"

#include <algorithm>
#include <cmath>

namespace pang::core::dsp {
namespace {
constexpr double kPi = 3.14159265358979323846;
}

void Fft::configure(int size) {
    size_ = size;
    reversed_.assign(static_cast<std::size_t>(size), 0);
    cosines_.assign(static_cast<std::size_t>(size / 2), 0.0f);
    sines_.assign(static_cast<std::size_t>(size / 2), 0.0f);

    int bits = 0;
    while ((1 << bits) < size) ++bits;
    for (int i = 0; i < size; ++i) {
        int value = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) value |= 1 << (bits - 1 - b);
        reversed_[static_cast<std::size_t>(i)] = value;
    }

    for (int i = 0; i < size / 2; ++i) {
        const double angle = -2.0 * kPi * i / size;
        cosines_[static_cast<std::size_t>(i)] = static_cast<float>(std::cos(angle));
        sines_[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(angle));
    }
}

void Fft::forward(float* real, float* imaginary) const noexcept {
    const int n = size_;

    for (int i = 0; i < n; ++i) {
        const int j = reversed_[static_cast<std::size_t>(i)];
        if (j > i) {
            std::swap(real[i], real[j]);
            std::swap(imaginary[i], imaginary[j]);
        }
    }

    for (int length = 2; length <= n; length <<= 1) {
        const int half = length / 2;
        const int step = n / length;
        for (int start = 0; start < n; start += length) {
            for (int k = 0; k < half; ++k) {
                const std::size_t twiddle = static_cast<std::size_t>(k * step);
                const float wr = cosines_[twiddle];
                const float wi = sines_[twiddle];

                const int even = start + k;
                const int odd = even + half;
                const float tr = real[odd] * wr - imaginary[odd] * wi;
                const float ti = real[odd] * wi + imaginary[odd] * wr;

                real[odd] = real[even] - tr;
                imaginary[odd] = imaginary[even] - ti;
                real[even] += tr;
                imaginary[even] += ti;
            }
        }
    }
}

void HannWindow::configure(int size) {
    values_.assign(static_cast<std::size_t>(size), 0.0f);
    double sum = 0.0;
    for (int i = 0; i < size; ++i) {
        const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * i / (size - 1)));
        values_[static_cast<std::size_t>(i)] = static_cast<float>(w);
        sum += w;
    }
    coherent_gain_ = static_cast<float>(sum / size);
}

}  // namespace pang::core::dsp
