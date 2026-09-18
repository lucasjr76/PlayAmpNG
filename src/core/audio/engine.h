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
#include "core/dsp/equalizer.h"
#include "core/dsp/gain.h"
#include "core/dsp/limiter.h"
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
    //
    // `desired` e o estado em que a faixa deve ficar assim que houver audio no
    // buffer: Playing, Paused ou Stopped. Os tres casos existem porque PL-25
    // exige que trocar de faixa durante a pausa continue pausado, e o fim de
    // playlist com repeat=off (PL-24) precisa parar sem comecar a tocar.
    void load(const std::string& url, State desired);
    void stop();
    bool seek(double seconds);

    // --- concorrentes com render()
    void play();
    void pause();
    void set_volume(float linear);  // 0.0 a 1.0
    float volume() const { return volume_balance_.volume(); }
    void set_balance(float balance);  // -1 esquerda, 0 centro, +1 direita
    float balance() const { return volume_balance_.balance(); }

    dsp::Equalizer& equalizer() { return equalizer_; }
    const dsp::Equalizer& equalizer() const { return equalizer_; }

    // AU-15 — modo de ReplayGain. A alteracao vale para a proxima carga e, se
    // ja houver faixa aberta, tambem para ela.
    void set_replaygain_mode(ReplayGainMode mode);
    ReplayGainMode replaygain_mode() const { return replaygain_mode_; }

    // Ganho positivo do ReplayGain e limitado a isto. O padrao 0 dB significa
    // "so atenua", que e a primeira das tres defesas contra clipping.
    void set_replaygain_max_boost_db(float db) { replaygain_max_boost_db_ = db; }

    // AU-22/AU-23 — atuacoes do clamp rigido e latencia introduzida pelo
    // lookahead do limitador, em quadros.
    std::uint32_t clamp_hits() const { return limiter_.clamp_hits(); }
    int latency_frames() const { return limiter_.latency_frames(); }

    // --- leitura
    Snapshot snapshot() const;
    std::string last_error() const;

    // true quando a faixa terminou sozinha, distinguindo fim natural de um
    // stop() do usuario. Limpado por load(), stop() e seek().
    bool ended() const { return ended_.load(std::memory_order_acquire); }

    // --- thread de audio
    void render(float* out, std::uint32_t frames) noexcept;

    int sample_rate() const { return sample_rate_; }
    int channels() const { return channels_; }

private:
    void apply_replaygain(const ProbeResult& info);
    void start_decoder(const std::string& url, State desired);
    void join_decoder();
    void decode_loop(std::string url, State desired);

    const int sample_rate_;
    const int channels_;

    FloatRing ring_;
    Decoder decoder_;
    std::thread decoder_thread_;
    std::atomic<bool> thread_quit_{false};

    std::atomic<State> state_{State::Stopped};
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<bool> eof_{false};
    std::atomic<bool> ended_{false};

    std::atomic<std::int64_t> position_frames_{0};
    std::atomic<std::uint32_t> underruns_{0};

    // Cadeia de processamento, na ordem de ARCHITECTURE.md secao 5.
    dsp::RampedGain replaygain_;
    dsp::Equalizer equalizer_;
    dsp::VolumeBalance volume_balance_;
    dsp::Limiter limiter_;

    ReplayGainMode replaygain_mode_ = ReplayGainMode::Off;
    float replaygain_max_boost_db_ = 0.0f;
    std::atomic<float> replaygain_db_{0.0f};
    std::atomic<bool> replaygain_present_{false};

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
