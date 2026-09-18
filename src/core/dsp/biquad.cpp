#include "core/dsp/biquad.h"

#include <cmath>

namespace pang::core::dsp {
namespace {

constexpr double kPi = 3.14159265358979323846;

BiquadCoeffs normalize(double b0, double b1, double b2, double a0, double a1, double a2) {
    return {static_cast<float>(b0 / a0), static_cast<float>(b1 / a0), static_cast<float>(b2 / a0),
            static_cast<float>(a1 / a0), static_cast<float>(a2 / a0)};
}

}  // namespace

double q_from_bandwidth(double octaves) {
    // Relacao classica entre largura de banda em oitavas e Q para um filtro
    // peaking. Derivada de BW = log2(f_sup/f_inf) com f0 = sqrt(f_inf*f_sup).
    const double p = std::pow(2.0, octaves);
    return std::sqrt(p) / (p - 1.0);
}

BiquadCoeffs peaking(double f0, double sample_rate, double q, double gain_db) {
    if (gain_db == 0.0) return identity();

    const double A = std::pow(10.0, gain_db / 40.0);
    const double w0 = 2.0 * kPi * f0 / sample_rate;
    const double cos_w0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);

    return normalize(1.0 + alpha * A, -2.0 * cos_w0, 1.0 - alpha * A,
                     1.0 + alpha / A, -2.0 * cos_w0, 1.0 - alpha / A);
}

BiquadCoeffs low_shelf(double f0, double sample_rate, double slope, double gain_db) {
    if (gain_db == 0.0) return identity();

    const double A = std::pow(10.0, gain_db / 40.0);
    const double w0 = 2.0 * kPi * f0 / sample_rate;
    const double cos_w0 = std::cos(w0);
    const double alpha = std::sin(w0) / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
    const double two_sqrt_a_alpha = 2.0 * std::sqrt(A) * alpha;

    return normalize(A * ((A + 1.0) - (A - 1.0) * cos_w0 + two_sqrt_a_alpha),
                     2.0 * A * ((A - 1.0) - (A + 1.0) * cos_w0),
                     A * ((A + 1.0) - (A - 1.0) * cos_w0 - two_sqrt_a_alpha),
                     (A + 1.0) + (A - 1.0) * cos_w0 + two_sqrt_a_alpha,
                     -2.0 * ((A - 1.0) + (A + 1.0) * cos_w0),
                     (A + 1.0) + (A - 1.0) * cos_w0 - two_sqrt_a_alpha);
}

BiquadCoeffs high_shelf(double f0, double sample_rate, double slope, double gain_db) {
    if (gain_db == 0.0) return identity();

    const double A = std::pow(10.0, gain_db / 40.0);
    const double w0 = 2.0 * kPi * f0 / sample_rate;
    const double cos_w0 = std::cos(w0);
    const double alpha = std::sin(w0) / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
    const double two_sqrt_a_alpha = 2.0 * std::sqrt(A) * alpha;

    return normalize(A * ((A + 1.0) + (A - 1.0) * cos_w0 + two_sqrt_a_alpha),
                     -2.0 * A * ((A - 1.0) + (A + 1.0) * cos_w0),
                     A * ((A + 1.0) + (A - 1.0) * cos_w0 - two_sqrt_a_alpha),
                     (A + 1.0) - (A - 1.0) * cos_w0 + two_sqrt_a_alpha,
                     2.0 * ((A - 1.0) - (A + 1.0) * cos_w0),
                     (A + 1.0) - (A - 1.0) * cos_w0 - two_sqrt_a_alpha);
}

}  // namespace pang::core::dsp
