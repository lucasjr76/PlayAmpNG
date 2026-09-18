#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/audio/decoder.h"
#include "core/audio/ring.h"
#include "core/audio/seqlock.h"
#include "core/state/player.h"

namespace pang::core {

// Engine de reproducao.
//
// Nao conhece Qt nem dispositivo de audio: expoe render(), que preenche um
// bloco de float32 intercalado. Quem chama render() e o callback do
// dispositivo (platform/) ou um teste (tests/). E por isso que toda a
// reproducao e verificavel headless, sem placa de som.
//
// CONTRATO DE THREADS
//
//   render()                      thread de audio. Sem lock, sem alocacao,
//                                 sem I/O, sem excecao, sem espera.
//   play/pause/set_volume         seguros concorrentemente com render().
//   load/stop/seek                PRECONDICAO: render() nao em execucao.
//
// A precondicao de load/stop/seek e satisfeita suspendendo o dispositivo antes
// de chamar. O miniaudio garante que ma_device_stop so retorna com o callback
// fora de execucao, entao a garantia vem da biblioteca, nao de codigo nosso.
//
// ponytail: suspender o dispositivo custa alguns milissegundos por seek. Se
// essa latencia incomodar, trocar por handshake de flush entre o thread de
// audio e o de decodificacao — mais rapido e bem mais facil de errar.
class Engine {
public:
    Engine(int sample_rate, int channels);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // --- controle (render() parado)
    void load(const std::string& url, bool start_playing);
    void stop();
    bool seek(double seconds);

    // --- concorrentes com render()
    void play();
    void pause();
    void set_volume(float linear);  // 0.0 a 1.0
    float volume() const { return volume_target_.load(std::memory_order_relaxed); }

    // --- leitura
    Snapshot snapshot() const;
    std::string last_error() const;

    // --- thread de audio
    void render(float* out, std::uint32_t frames) noexcept;

    int sample_rate() const { return sample_rate_; }
    int channels() const { return channels_; }

private:
    void start_decoder(const std::string& url, bool start_playing);
    void join_decoder();
    void decode_loop(std::string url, bool start_playing);

    const int sample_rate_;
    const int channels_;

    FloatRing ring_;
    Decoder decoder_;
    std::thread decoder_thread_;
    std::atomic<bool> thread_quit_{false};

    std::atomic<State> state_{State::Stopped};
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<bool> eof_{false};

    std::atomic<std::int64_t> position_frames_{0};
    std::atomic<std::uint32_t> underruns_{0};

    std::atomic<float> volume_target_{1.0f};
    std::atomic<float> volume_current_{1.0f};

    // Preenchidos pelo thread de decodificacao depois de abrir a fonte.
    std::atomic<std::int64_t> duration_frames_{-1};
    std::atomic<std::int64_t> bitrate_bps_{-1};
    std::atomic<int> source_rate_{0};
    std::atomic<int> source_channels_{0};
    std::atomic<bool> seekable_{false};

    Seqlock<AudioPublication> pub_;

    mutable std::mutex error_mutex_;
    std::string last_error_;
};

}  // namespace pang::core
