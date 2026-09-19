#pragma once

#include <array>
#include <string>
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

    // AU-11 — nome do dispositivo de saida. Vazio significa "o padrao do
    // sistema", que e diferente de "o dispositivo que por acaso era o padrao
    // quando isto foi salvo": guardar o nome do padrao congelaria a escolha do
    // sistema operacional.
    std::string audio_device;

    core::dsp::EqState eq;
    std::vector<core::dsp::EqPreset> user_presets;  // EQ-07

    // AP-10 — disposicao dos paineis. Geometria como x,y,w,h; w/h em zero
    // significa "ainda nao salvo", e o padrao vale.
    int scale = 2;              // AP-12 — 1x e caso de teste, 2x e o util
    int visualization = 0;      // 0 espectro, 1 osciloscopio, 2 desligado
    bool playlist_visible = true;
    bool detached = false;   // AP-08 — melhor esforco, por plataforma
    bool compact = false;    // AP-11
    bool equalizer_visible = false;
    bool always_on_top = false;   // IN-06
    std::array<int, 4> main_geometry{0, 0, 0, 0};
    std::array<int, 4> playlist_geometry{0, 0, 0, 0};
    std::array<int, 4> equalizer_geometry{0, 0, 0, 0};
};

// IN-09 — gravacao atomica (temporario + rename), via QSaveFile.
bool save(const AppState& state);

// IN-10 — arquivo invalido e renomeado para *.bad, padroes sao carregados e o
// fato vai para o log. O aplicativo nunca se recusa a abrir por configuracao
// corrompida.
AppState load();

}  // namespace pang::ui::settings
