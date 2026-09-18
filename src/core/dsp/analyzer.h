#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "core/dsp/fft.h"

namespace pang::core::dsp {

// Analisador de espectro e osciloscopio.
//
// Parametros documentados em ARCHITECTURE.md secao 6: FFT de 1024, janela de
// Hann, normalizacao 2/(N*ganho_coerente), piso de -70 dBFS, 19 barras em
// distribuicao logaritmica.
//
// Roda no thread da interface, consumindo amostras que o thread de audio
// deixou no ring de captura. Nao e chamado do callback de audio.
class SpectrumAnalyzer {
public:
    static constexpr int kBars = 19;
    static constexpr int kFftSize = 1024;
    static constexpr float kFloorDb = -70.0f;

    void configure(int sample_rate, int channels);

    // VI-11 — Nyquist DA FONTE, que pode ser menor que o do dispositivo quando
    // a faixa foi reamostrada. Barras inteiramente acima disso ficam inativas,
    // em vez de exibir o que a fonte nao tem.
    void set_source_nyquist(float hz);

    // Consome amostras intercaladas. A cada kFftSize quadros acumulados,
    // calcula um quadro de espectro e aplica a suavizacao temporal.
    void feed(const float* interleaved, std::size_t frames);

    // Retencao e queda de picos, em tempo de parede.
    void advance_peaks(float dt_seconds);

    // VI-15 — silencio, pausa e parada.
    void reset();       // tudo a zero (parado)
    void hold();        // congela o quadro atual (pausado)

    const std::array<float, kBars>& bars() const { return bars_; }
    const std::array<float, kBars>& peaks() const { return peaks_; }
    bool bar_active(int bar) const;

    // Faixa de frequencias de cada barra, para rotulos e diagnostico.
    float bar_low_hz(int bar) const;
    float bar_high_hz(int bar) const;

    // Ultimo bloco de amostras em mistura mono, para o osciloscopio.
    // VI-16 — representacao temporal real; anti-fase aparece como linha reta,
    // que ali e a informacao correta e nao um defeito.
    const std::vector<float>& scope() const { return scope_; }

private:
    void compute_frame();

    int sample_rate_ = 0;
    int channels_ = 0;
    float source_nyquist_ = 0.0f;

    Fft fft_;
    HannWindow window_;
    float normalization_ = 1.0f;

    std::vector<float> real_;
    std::vector<float> imaginary_;
    std::vector<float> left_;
    std::vector<float> right_;
    std::size_t filled_ = 0;

    std::array<int, kBars + 1> edges_{};   // bins de corte
    std::array<float, kBars> bars_{};
    std::array<float, kBars> peaks_{};
    std::array<float, kBars> peak_hold_{};
    std::array<float, kBars> peak_fall_{};
    std::array<bool, kBars> active_{};

    std::vector<float> scope_;
};

}  // namespace pang::core::dsp
