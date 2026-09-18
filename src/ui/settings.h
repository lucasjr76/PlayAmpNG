#pragma once

#include <vector>

#include "core/dsp/presets.h"
#include "core/state/player.h"

// Persistencia.
//
// Vive em ui/, e nao em core/, porque a gravacao atomica exigida por IN-09 e
// feita com QSaveFile — Qt ja e dependencia, e escrever um parser de JSON
// proprio seria justamente o tipo de codigo que se decifra as tres da manha.
// core/ continua sem conhecer Qt: aqui so trafegam structs simples.
namespace pang::ui::settings {

struct AppState {
    float volume = 1.0f;
    float balance = 0.0f;
    bool shuffle = false;
    int repeat = 0;            // 0 off, 1 faixa, 2 lista
    int replaygain_mode = 0;   // 0 off, 1 faixa, 2 album
    bool autoplay_on_restore = false;  // IN-11

    core::dsp::EqState eq;
    std::vector<core::dsp::EqPreset> user_presets;  // EQ-07
};

// IN-09 — gravacao atomica (temporario + rename), via QSaveFile.
bool save(const AppState& state);

// IN-10 — arquivo invalido e renomeado para *.bad, padroes sao carregados e o
// fato vai para o log. O aplicativo nunca se recusa a abrir por configuracao
// corrompida.
AppState load();

}  // namespace pang::ui::settings
