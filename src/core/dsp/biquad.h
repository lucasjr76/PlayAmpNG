#pragma once

namespace pang::core::dsp {

// Biquad RBJ ("Audio EQ Cookbook"), normalizado com a0 = 1.
struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
};

// Forma direta II transposta: e a que se comporta melhor em ponto flutuante de
// precisao simples, porque o estado guarda valores da mesma ordem de grandeza
// da saida, em vez de acumular o historico da entrada.
struct BiquadState {
    float z1 = 0.0f, z2 = 0.0f;

    void reset() { z1 = z2 = 0.0f; }
};

constexpr BiquadCoeffs identity() { return {1.0f, 0.0f, 0.0f, 0.0f, 0.0f}; }

// f0 em Hz, sample_rate em Hz, q adimensional, gain_db em dB.
BiquadCoeffs peaking(double f0, double sample_rate, double q, double gain_db);

// `slope` (S) de 1.0 da a inclinacao maxima sem ressonancia.
BiquadCoeffs low_shelf(double f0, double sample_rate, double slope, double gain_db);
BiquadCoeffs high_shelf(double f0, double sample_rate, double slope, double gain_db);

// Q equivalente a uma largura de banda dada em oitavas.
double q_from_bandwidth(double octaves);

inline float process(const BiquadCoeffs& c, BiquadState& s, float x) noexcept {
    const float y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

}  // namespace pang::core::dsp
