#pragma once

#include <string>
#include <vector>

// Camada de plataforma. O miniaudio ja abstrai PipeWire/PulseAudio/ALSA no
// Linux e WASAPI no Windows, entao nao ha nada a duplicar por sistema aqui —
// o que este cabecalho faz e impedir que o tipo ma_* vaze para ui/ e core/.
namespace pang::platform {

struct PlaybackDevice {
    std::string name;
    bool is_default = false;
};

// Em falha devolve lista vazia e preenche error.
std::vector<PlaybackDevice> list_playback_devices(std::string& error);

// Backend de audio efetivamente em uso (pipewire, pulseaudio, alsa, wasapi...).
std::string backend_name();

}  // namespace pang::platform
