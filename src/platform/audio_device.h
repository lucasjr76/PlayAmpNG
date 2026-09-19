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

    // AU-11 — `device` vazio usa o padrao do sistema. Nome que nao existe mais
    // cai no padrao com aviso, em vez de falhar: o dispositivo salvo na sessao
    // anterior pode ter sido desconectado desde entao, e isso nao e erro do
    // usuario.
    bool start(int sample_rate, int channels, RenderFn render, void* user, std::string& error,
               const std::string& device = {});
    void suspend();
    void resume();
    void close();

    // AU-12, RB-05 — chamar periodicamente, do thread do dono. Quando o
    // dispositivo some, reabre ate esgotar as tentativas; depois disso
    // health() fica Failed e last_error() explica.
    enum class Health : std::uint8_t { Ok, Recovering, Failed };
    Health poll(int elapsed_ms);
    Health health() const;
    std::string last_error() const;

    // Troca de dispositivo em uso, preservando a reproducao. Tambem serve para
    // sair do estado Failed.
    bool switch_to(const std::string& device, std::string& error);

    bool is_open() const;
    int sample_rate() const;
    int channels() const;
    std::string device_name() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pang::platform
