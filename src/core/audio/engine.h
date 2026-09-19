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

// Descreve uma faixa que passou a ocupar o ring a partir de `start_frame`.
//
// O ring carrega amostras, nao faixas. Para que a emenda de gapless nao precise
// esvaziar nada, uma fila paralela diz a partir de que quadro consumido as
// amostras pertencem a proxima faixa. E o thread de audio, ao cruzar essa
// marca, quem troca posicao, duracao e demais metadados publicados.
struct TrackSegment {
    std::uint64_t generation = 0;
    std::uint64_t token = 0;  // identidade da faixa do lado do Controller
    std::int64_t start_frame = 0;
    std::int64_t duration_frames = -1;
    std::int64_t bitrate_bps = -1;
    int sample_rate = 0;
    int channels = 0;
    bool seekable = false;
    float replaygain_db = 0.0f;
    bool replaygain_present = false;
};

// Fila SPSC de tamanho fixo: decodificador empurra, audio consome.
class SegmentQueue {
public:
    static constexpr std::size_t kCapacity = 16;

    bool push(const TrackSegment& segment) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) % kCapacity;
        if (next == head_.load(std::memory_order_acquire)) return false;  // cheia
        slots_[tail] = segment;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    const TrackSegment* peek() const noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return nullptr;
        return &slots_[head];
    }

    void pop() noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return;
        head_.store((head + 1) % kCapacity, std::memory_order_release);
    }

    // Precondicao: nem produtor nem consumidor em execucao.
    void clear() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

private:
    TrackSegment slots_[kCapacity]{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

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
    void load(const std::string& url, State desired, std::uint64_t token = 0);
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

    // MD-05 — o que o stream diz estar tocando agora, e o nome da estacao.
    // Vazios para fonte local. Seguros de ler de qualquer thread.
    std::string icy_title() const;
    std::string station() const;
    bool live() const;
    ReplayGainMode replaygain_mode() const { return replaygain_mode_; }

    // Ganho positivo do ReplayGain e limitado a isto. O padrao 0 dB significa
    // "so atenua", que e a primeira das tres defesas contra clipping.
    void set_replaygain_max_boost_db(float db) { replaygain_max_boost_db_ = db; }

    // --- visualizacao (VI-17, VI-19, VI-21)
    //
    // VI-17 — desligar a visualizacao desliga a CAPTURA, na origem. Parar so o
    // consumo deixaria o custo da copia dentro do callback de audio.
    void set_capture_enabled(bool on) { capture_enabled_.store(on, std::memory_order_relaxed); }
    bool capture_enabled() const { return capture_enabled_.load(std::memory_order_relaxed); }

    // Consome amostras do ponto de captura (pos-equalizador, pre-volume).
    // Chamado pelo thread da interface. Devolve quantos floats saiu.
    std::size_t read_visualization(float* destination, std::size_t max_floats);

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

    // --- gapless (AU-13)
    //
    // set_next informa qual faixa emendar quando a atual acabar. O
    // decodificador abre a proxima sem esvaziar o ring, entao nao ha lacuna.
    // O `token` e opaco para o engine: o Controller usa o id da faixa, e le de
    // volta em current_token() para saber que a emenda aconteceu.
    void set_next(std::string url, std::uint64_t token);
    void clear_next();
    std::uint64_t current_token() const { return current_token_.load(std::memory_order_acquire); }

    // --- thread de audio
    void render(float* out, std::uint32_t frames) noexcept;

    int sample_rate() const { return sample_rate_; }
    int channels() const { return channels_; }

private:
    void apply_replaygain(const ProbeResult& info);
    void publish_track_info(const ProbeResult& info);
    void set_stream_info(const ProbeResult& info);
    void apply_segment(const TrackSegment& segment) noexcept;
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

    // generation_ e a geracao do que ESTA TOCANDO; generation_seq_ e o
    // contador de onde saem as novas. Com gapless o decodificador abre a faixa
    // seguinte segundos antes de ela ser ouvida, entao publicar a geracao na
    // decodificacao faria a interface trocar de faixa antes do som trocar.
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint64_t> generation_seq_{0};
    std::atomic<bool> eof_{false};
    std::atomic<bool> ended_{false};

    // Contabilidade de posicao com emenda.
    //   posicao = position_base_ + (consumed_ - track_origin_)
    // A emenda apenas move track_origin_ para o inicio da nova faixa e zera a
    // base, sem interromper o fluxo de amostras.
    std::atomic<std::int64_t> consumed_frames_{0};
    std::int64_t track_origin_ = 0;      // so o thread de audio escreve
    std::int64_t position_base_ = 0;     // idem
    std::atomic<std::uint32_t> underruns_{0};

    // Ring de captura da visualizacao. Separado do ring de audio: o audio
    // nunca espera por ele, e um bloco que nao couber e descartado.
    FloatRing vis_ring_;
    std::atomic<bool> capture_enabled_{false};
    std::atomic<std::uint32_t> vis_drops_{0};

    SegmentQueue segments_;
    std::atomic<std::uint64_t> current_token_{0};
    std::mutex next_mutex_;
    std::string next_url_;
    std::uint64_t next_token_ = 0;

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

    // MD-04, MD-05 — estado da fonte de rede. O thread de decodificacao
    // escreve, a interface le.
    std::atomic<bool> live_{false};
    mutable std::mutex stream_mutex_;
    std::string icy_title_;
    std::string station_;
};

}  // namespace pang::core
