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

// EQ-07 — colecao de presets do usuario.
//
// Pura, sem interface: e aqui que o recurso erra — nome em branco, nome
// repetido com outra caixa, nome que colide com um integrado — e isso se
// verifica sem abrir menu nenhum.
enum class SaveResult {
    Added,        // nome novo
    Replaced,     // ja existia um do usuario com esse nome: e o "editar"
    EmptyName,    // so espacos, ou nada
    BuiltinName,  // colide com um integrado
};

// O nome e aparado nas pontas, e a comparacao ignora maiusculas: "Rock" e
// "rock " sao o mesmo nome para quem le o menu, e aceitar os dois deixaria
// duas entradas indistinguiveis.
//
// Colidir com um integrado e recusado, e nao tratado como substituicao: os
// integrados nao sao do usuario, e um "Rock" dele ao lado do "Rock" de
// fabrica seria outra entrada ambigua.
SaveResult save_user_preset(std::vector<EqPreset>& presets, EqPreset preset);

// Devolve se havia um preset com esse nome para remover.
bool remove_user_preset(std::vector<EqPreset>& presets, const std::string& name);

void apply(Equalizer& equalizer, const EqState& state);
EqState capture(const Equalizer& equalizer);
EqState from_preset(const EqPreset& preset);

}  // namespace pang::core::dsp
