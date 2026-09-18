#pragma once

#include <cstddef>
#include <vector>

namespace pang::core::dsp {

// FFT complexa radix-2, iterativa, no lugar.
//
// Tabelas de rotacao e de inversao de bits sao montadas em configure();
// forward() nao aloca.
class Fft {
public:
    void configure(int size);  // potencia de 2
    int size() const { return size_; }

    void forward(float* real, float* imaginary) const noexcept;

private:
    int size_ = 0;
    std::vector<int> reversed_;
    std::vector<float> cosines_;
    std::vector<float> sines_;
};

// Janela de Hann e seu ganho coerente.
//
// O ganho coerente e sum(w)/N, e e o que entra na normalizacao: sem ele uma
// senoide de amplitude 1.0 nao daria 0 dBFS no bin, e a escala do espectro
// seria arbitraria em vez de verificavel.
class HannWindow {
public:
    void configure(int size);
    const std::vector<float>& values() const { return values_; }
    float coherent_gain() const { return coherent_gain_; }

private:
    std::vector<float> values_;
    float coherent_gain_ = 0.5f;
};

}  // namespace pang::core::dsp
