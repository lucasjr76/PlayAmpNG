#pragma once

#include <array>
#include <string>
#include <vector>

#include "core/dsp/equalizer.h"

namespace pang::core::dsp {

// Estado completo do equalizador, para persistir e para presets.
struct EqState {
    bool bypass = false;
    float preamp_db = 0.0f;
    std::array<float, Equalizer::kBands> bands{};
};

struct EqPreset {
    std::string name;
    float preamp_db = 0.0f;
    std::array<float, Equalizer::kBands> bands{};
};

// EQ-06 — presets integrados.
//
// As curvas sao proprias, nao copiadas do Winamp: os valores exatos dos presets
// originais nunca foram publicados como especificacao, e a secao 1 pede que
// aproximacoes sejam documentadas em vez de presumidas. Estas foram montadas
// para as dez frequencias de referencia e conferidas com os testes de resposta.
const std::vector<EqPreset>& builtin_presets();

void apply(Equalizer& equalizer, const EqState& state);
EqState capture(const Equalizer& equalizer);
EqState from_preset(const EqPreset& preset);

}  // namespace pang::core::dsp
