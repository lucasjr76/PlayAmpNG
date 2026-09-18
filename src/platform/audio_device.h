#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Camada de plataforma. O miniaudio ja abstrai PipeWire/PulseAudio/ALSA,
// WASAPI e CoreAudio, entao nao ha nada a duplicar por sistema aqui — o que
// este cabecalho faz e impedir que o tipo ma_* vaze para ui/ e core/.
namespace pang::platform {

struct PlaybackDevice {
    std::string name;
    bool is_default = false;
};

// Em falha devolve lista vazia e preenche error.
std::vector<PlaybackDevice> list_playback_devices(std::string& error);

// Backend de audio efetivamente em uso (pipewire, pulseaudio, alsa, wasapi...).
std::string backend_name();

// Dispositivo de saida.
//
// suspend() so retorna com o callback fora de execucao — garantia do proprio
// miniaudio (ma_device_stop). E isso que satisfaz a precondicao de
// Engine::load/stop/seek sem nenhum handshake escrito por nos.
class AudioOutput {
public:
    using RenderFn = void (*)(void* user, float* out, std::uint32_t frames);

    AudioOutput();
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    bool start(int sample_rate, int channels, RenderFn render, void* user, std::string& error);
    void suspend();
    void resume();
    void close();

    bool is_open() const;
    int sample_rate() const;
    int channels() const;
    std::string device_name() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pang::platform
