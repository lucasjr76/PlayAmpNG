#pragma once

#include <cstdint>

namespace pang::core {

// AR-02 — estados explicitos. A interface reflete o estado confirmado pelo
// engine, nunca o estado otimista do clique.
enum class State : std::uint8_t {
    Stopped,    // posicao 0, faixa carregada e destacada
    Loading,    // abrindo a fonte e enchendo o buffer
    Playing,
    Paused,
    Buffering,  // tocando, mas o buffer esvaziou (usado a partir do M6)
    Error,
};

constexpr const char* to_string(State s) {
    switch (s) {
        case State::Stopped:   return "parado";
        case State::Loading:   return "carregando";
        case State::Playing:   return "reproduzindo";
        case State::Paused:    return "pausado";
        case State::Buffering: return "buffering";
        case State::Error:     return "erro";
    }
    return "?";
}

// AU-15 — modos de ReplayGain.
enum class ReplayGainMode : std::uint8_t { Off, Track, Album };

// Grupo publicado pelo thread de audio via Seqlock. Trivialmente copiavel.
//
// Os campos precisam ser mutuamente consistentes: posicao e contadores lidos
// pela interface tem que vir do mesmo bloco de render, senao a barra de
// progresso e os diagnosticos discordam entre si.
struct AudioPublication {
    std::int64_t position_frames = 0;  // na taxa do dispositivo
    float peak = 0.0f;                 // pico absoluto do ultimo bloco
    std::uint32_t underruns = 0;       // blocos em que o ring nao tinha dados
    std::uint32_t clamp_hits = 0;      // atuacoes do clamp rigido (M3)
    std::uint32_t vis_drops = 0;       // blocos de visualizacao descartados (M4)
};

// O que a interface le a cada repintura.
struct Snapshot {
    State state = State::Stopped;
    std::uint64_t generation = 0;
    std::int64_t position_frames = 0;
    std::int64_t duration_frames = -1;  // -1 = desconhecida (PL-26, MD-09)
    bool seekable = false;
    int sample_rate = 0;
    int channels = 0;
    std::int64_t bitrate_bps = -1;  // -1 = a fonte nao informa
    float peak = 0.0f;
    std::uint32_t underruns = 0;
    float replaygain_db = 0.0f;   // 0 quando desligado ou ausente na fonte
    bool replaygain_present = false;
    std::uint32_t clamp_hits = 0;
    std::uint32_t vis_drops = 0;
};

}  // namespace pang::core
