#include "core/audio/engine.h"

#include "core/audio/network.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <optional>

#include "core/util/log.h"

namespace pang::core {
namespace {

// ~2 s de buffer: folga suficiente para absorver leitura de disco sem que o
// seek fique lento por causa do que ja foi decodificado adiante.
constexpr double kRingSeconds = 2.0;

// Bloco que o decodificador entrega por vez.
constexpr std::size_t kDecodeChunkFrames = 4096;

// MD-04 — marcas d'agua do estado Buffering, em quadros. Com ~2 s de ring,
// 0,25 s para entrar e 0,75 s para sair: a histerese e o que impede o estado
// de piscar a cada bloco quando a rede entrega em rajadas.
constexpr std::size_t kBufferingLowFrames = 11025;    // 0,25 s a 44,1 kHz
constexpr std::size_t kBufferingHighFrames = 33075;   // 0,75 s a 44,1 kHz

constexpr double kVolumeRampSeconds = 0.020;  // AU-10

// Oito quadros de FFT de folga. O suficiente para a interface a 60 Hz nunca
// perder bloco em uso normal, e pequeno o bastante para o atraso da
// visualizacao em relacao ao som nao ser perceptivel.
constexpr std::size_t kVisBlocks = 8;
constexpr std::size_t kVisBlockFrames = 1024;

}  // namespace

Engine::Engine(int sample_rate, int channels)
    : sample_rate_(sample_rate),
      channels_(channels),
      ring_(static_cast<std::size_t>(sample_rate * kRingSeconds) * channels),
      vis_ring_(kVisBlocks * kVisBlockFrames * static_cast<std::size_t>(channels)) {
    replaygain_.configure(sample_rate, kVolumeRampSeconds);
    equalizer_.configure(sample_rate, channels);
    volume_balance_.configure(sample_rate, channels);
    limiter_.configure(sample_rate, channels);
}

Engine::~Engine() { join_decoder(); }

// ---------------------------------------------------------------- controle

void Engine::load(const std::string& url, State desired, std::uint64_t token) {
    join_decoder();
    decoder_.close();
    ring_.reset();

    // AR-03 — toda carga tem geracao propria. Resposta assincrona carimbada
    // com geracao antiga e descartada em vez de sobrescrever o estado atual.
    const std::uint64_t generation = generation_seq_.fetch_add(1, std::memory_order_relaxed) + 1;
    generation_.store(generation, std::memory_order_relaxed);

    consumed_frames_.store(0, std::memory_order_relaxed);
    track_origin_ = 0;
    position_base_ = 0;
    segments_.clear();
    eof_.store(false, std::memory_order_relaxed);
    ended_.store(false, std::memory_order_release);
    duration_frames_.store(-1, std::memory_order_relaxed);
    bitrate_bps_.store(-1, std::memory_order_relaxed);
    source_rate_.store(0, std::memory_order_relaxed);
    source_channels_.store(0, std::memory_order_relaxed);
    seekable_.store(false, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_.clear();
    }
    pub_.store(AudioPublication{});
    current_token_.store(token, std::memory_order_release);
    state_.store(State::Loading, std::memory_order_relaxed);

    start_decoder(url, desired);
}

void Engine::stop() {
    join_decoder();
    ring_.reset();
    segments_.clear();
    // PL-03 — parar volta a posicao 0 e mantem a faixa carregada.
    consumed_frames_.store(0, std::memory_order_relaxed);
    track_origin_ = 0;
    position_base_ = 0;
    eof_.store(false, std::memory_order_relaxed);
    ended_.store(false, std::memory_order_release);
    state_.store(State::Stopped, std::memory_order_relaxed);
    pub_.store(AudioPublication{});
    if (decoder_.is_open()) decoder_.seek(0.0);
}

bool Engine::seek(double seconds) {
    if (!seekable_.load(std::memory_order_relaxed)) return false;

    const State previous = state_.load(std::memory_order_relaxed);
    join_decoder();

    if (!decoder_.is_open() || !decoder_.seek(seconds)) return false;

    ring_.reset();
    segments_.clear();
    eof_.store(false, std::memory_order_relaxed);
    ended_.store(false, std::memory_order_release);
    const std::int64_t target = static_cast<std::int64_t>(seconds * sample_rate_);
    consumed_frames_.store(0, std::memory_order_relaxed);
    track_origin_ = 0;
    position_base_ = target;

    // Publica ja: sem isso a interface so veria a nova posicao no primeiro
    // render, e uma busca com o audio suspenso pareceria nao ter acontecido.
    AudioPublication pub;
    pub.position_frames = target;
    pub.underruns = underruns_.load(std::memory_order_relaxed);
    pub_.store(pub);

    // O decodificador ja esta aberto e posicionado: o loop apenas retoma dali.
    thread_quit_.store(false, std::memory_order_relaxed);
    decoder_.clear_cancel();
    const bool resume = previous == State::Playing;
    state_.store(resume ? State::Playing : State::Paused, std::memory_order_relaxed);
    decoder_thread_ = std::thread(
        [this, resume] { decode_loop(std::string{}, resume ? State::Playing : State::Paused); });
    return true;
}

void Engine::play() {
    const State s = state_.load(std::memory_order_relaxed);
    if (s == State::Paused || s == State::Loading) state_.store(State::Playing);
}

void Engine::pause() {
    if (state_.load(std::memory_order_relaxed) == State::Playing) state_.store(State::Paused);
}

void Engine::set_volume(float linear) { volume_balance_.set_volume(linear); }

void Engine::set_balance(float balance) { volume_balance_.set_balance(balance); }

void Engine::set_stream_info(const ProbeResult& info) {
    live_.store(info.live, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(stream_mutex_);
    station_ = info.station ? *info.station : std::string{};
    icy_title_.clear();
}

std::string Engine::icy_title() const {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    return icy_title_;
}

bool Engine::live() const { return live_.load(std::memory_order_relaxed); }

std::string Engine::station() const {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    return station_;
}

void Engine::publish_track_info(const ProbeResult& info) {
    duration_frames_.store(info.duration_us
                               ? static_cast<std::int64_t>(double(*info.duration_us) *
                                                           sample_rate_ / 1e6)
                               : -1,
                           std::memory_order_relaxed);
    bitrate_bps_.store(info.bitrate_bps ? *info.bitrate_bps : -1, std::memory_order_relaxed);
    source_rate_.store(info.sample_rate, std::memory_order_relaxed);
    source_channels_.store(info.channels, std::memory_order_relaxed);
    seekable_.store(info.seekable, std::memory_order_relaxed);
}

void Engine::set_next(std::string url, std::uint64_t token) {
    std::lock_guard<std::mutex> lock(next_mutex_);
    next_url_ = std::move(url);
    next_token_ = token;
}

void Engine::clear_next() {
    std::lock_guard<std::mutex> lock(next_mutex_);
    next_url_.clear();
    next_token_ = 0;
}

void Engine::set_replaygain_mode(ReplayGainMode mode) {
    replaygain_mode_ = mode;
    if (decoder_.is_open()) apply_replaygain(decoder_.info());
}

// AU-15 — escolhe o ganho conforme o modo e limita o reforco positivo.
//
// Sem esse teto, uma faixa com ReplayGain positivo entraria na cadeia ja acima
// de 0 dBFS e obrigaria o limitador a trabalhar o tempo todo. A primeira defesa
// contra clipping e nao criar o problema.
void Engine::apply_replaygain(const ProbeResult& info) {
    std::optional<float> gain;
    if (replaygain_mode_ == ReplayGainMode::Track)
        gain = info.replaygain_track_db ? info.replaygain_track_db : info.replaygain_album_db;
    else if (replaygain_mode_ == ReplayGainMode::Album)
        gain = info.replaygain_album_db ? info.replaygain_album_db : info.replaygain_track_db;

    const float db = gain ? std::min(*gain, replaygain_max_boost_db_) : 0.0f;
    replaygain_db_.store(db, std::memory_order_relaxed);
    replaygain_present_.store(gain.has_value(), std::memory_order_relaxed);
    replaygain_.set_db(db);
}

// ------------------------------------------------------ thread de decodificacao

void Engine::start_decoder(const std::string& url, State desired) {
    thread_quit_.store(false, std::memory_order_relaxed);
    decoder_.clear_cancel();
    decoder_thread_ = std::thread([this, url, desired] { decode_loop(url, desired); });
}

void Engine::join_decoder() {
    thread_quit_.store(true, std::memory_order_relaxed);
    decoder_.cancel();  // AR-09 — solta o libav de qualquer bloqueio
    if (decoder_thread_.joinable()) decoder_thread_.join();
    thread_quit_.store(false, std::memory_order_relaxed);
    decoder_.clear_cancel();
}

// url vazia significa "continuar com o decodificador ja aberto" (caso do seek).
void Engine::decode_loop(std::string url, State desired) {
    if (!url.empty()) {
        std::string error;
        if (!decoder_.open(url, sample_rate_, channels_, error)) {
            const std::string message = error + " (" + log::redact(url) + ")";
            {
                std::lock_guard<std::mutex> lock(error_mutex_);
                last_error_ = message;
            }
            log::error(message);
            state_.store(State::Error, std::memory_order_relaxed);
            return;
        }

        publish_track_info(decoder_.info());
        apply_replaygain(decoder_.info());
        set_stream_info(decoder_.info());
    }

    std::vector<float> chunk(kDecodeChunkFrames * channels_);
    bool announced = url.empty();  // no seek o estado ja foi definido
    std::int64_t written_frames = 0;

    while (!thread_quit_.load(std::memory_order_relaxed)) {
        const std::size_t room_frames = ring_.writable() / channels_;
        if (room_frames == 0) {
            // ponytail: espera fixa em vez de variavel de condicao. 2 ms com
            // ~2 s de buffer e irrelevante; trocar se o perfil apontar.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        // MD-04 — em fonte ao vivo o buffer pode esvaziar sem que nada esteja
        // errado. O estado passa a Buffering e volta a Playing sozinho. A
        // histerese evita piscar entre os dois a cada bloco.
        if (live_.load(std::memory_order_relaxed)) {
            const std::size_t level = ring_.readable() / channels_;
            const State now = state_.load(std::memory_order_relaxed);
            if (now == State::Playing && level < kBufferingLowFrames)
                state_.store(State::Buffering, std::memory_order_relaxed);
            else if (now == State::Buffering && level >= kBufferingHighFrames)
                state_.store(State::Playing, std::memory_order_relaxed);
        }

        if (auto title = decoder_.take_icy_title()) {
            std::lock_guard<std::mutex> lock(stream_mutex_);
            icy_title_ = *title;
        }

        const std::size_t want = std::min(room_frames, kDecodeChunkFrames);
        std::size_t got = decoder_.read(chunk.data(), want);

        // MD-07, RB-04 — fonte ao vivo nao tem fim: leitura vazia e queda de
        // conexao, nao fim de faixa. Reabre a MESMA url um numero limitado de
        // vezes, e so entao declara erro. Fonte local cai direto no caminho de
        // fim de faixa abaixo.
        if (got == 0 && live_.load(std::memory_order_relaxed) && !url.empty()) {
            bool recovered = false;
            for (int attempt = 1; attempt <= kReconnectAttempts && !recovered; ++attempt) {
                if (thread_quit_.load(std::memory_order_relaxed)) return;
                state_.store(State::Buffering, std::memory_order_relaxed);
                log::warn("stream interrompido, tentativa " + std::to_string(attempt) + " de " +
                          std::to_string(kReconnectAttempts) + " (" + log::redact(url) + ")");
                std::this_thread::sleep_for(std::chrono::milliseconds(kReconnectDelayMs));

                std::string error;
                decoder_.close();
                decoder_.clear_cancel();
                if (decoder_.open(url, sample_rate_, channels_, error)) {
                    got = decoder_.read(chunk.data(), want);
                    recovered = got > 0;
                }
            }
            if (!recovered) {
                const std::string message = "stream interrompido e nao restabelecido (" +
                                            log::redact(url) + ")";
                {
                    std::lock_guard<std::mutex> lock(error_mutex_);
                    last_error_ = message;
                }
                log::error(message);
                state_.store(State::Error, std::memory_order_relaxed);
                return;
            }
            state_.store(State::Playing, std::memory_order_relaxed);
        }

        if (got == 0) {
            // AU-13 — fim da faixa. Se ha proxima, emenda aqui: o decodificador
            // e reaberto e continua escrevendo NO MESMO ring, sem esvaziar. O
            // thread de audio nem percebe, e e por isso que nao ha lacuna.
            //
            // ponytail: a proxima faixa e aberta so agora, nao 5 s antes. Os
            // ~2 s ja bufferizados no ring cobrem de sobra o tempo de abrir um
            // arquivo local. Para fonte de rede, pre-abrir antes do fim.
            std::string next;
            std::uint64_t token = 0;
            {
                std::lock_guard<std::mutex> lock(next_mutex_);
                next = next_url_;
                token = next_token_;
                next_url_.clear();
                next_token_ = 0;
            }
            if (next.empty()) {
                eof_.store(true, std::memory_order_release);
                break;
            }

            std::string error;
            if (!decoder_.open(next, sample_rate_, channels_, error)) {
                log::error(error + " (" + log::redact(next) + ")");
                eof_.store(true, std::memory_order_release);
                break;
            }

            TrackSegment segment;
            segment.generation = generation_seq_.fetch_add(1, std::memory_order_relaxed) + 1;
            segment.token = token;
            segment.start_frame = written_frames;
            segment.duration_frames = decoder_.duration_frames();
            const ProbeResult& info = decoder_.info();
            segment.bitrate_bps = info.bitrate_bps ? *info.bitrate_bps : -1;
            segment.sample_rate = info.sample_rate;
            segment.channels = info.channels;
            segment.seekable = info.seekable;
            segment.replaygain_db = 0.0f;
            segment.replaygain_present = false;
            if (replaygain_mode_ != ReplayGainMode::Off) {
                const auto gain = replaygain_mode_ == ReplayGainMode::Track
                                      ? (info.replaygain_track_db ? info.replaygain_track_db
                                                                  : info.replaygain_album_db)
                                      : (info.replaygain_album_db ? info.replaygain_album_db
                                                                  : info.replaygain_track_db);
                if (gain) {
                    segment.replaygain_db = std::min(*gain, replaygain_max_boost_db_);
                    segment.replaygain_present = true;
                }
            }
            segments_.push(segment);
            continue;
        }
        ring_.write(chunk.data(), got * channels_);
        written_frames += static_cast<std::int64_t>(got);

        // AR-04 — so sai de Loading depois que ha audio de verdade no buffer.
        if (!announced) {
            announced = true;
            state_.store(desired, std::memory_order_relaxed);
        }
    }
}

// ------------------------------------------------------------------ leitura

Snapshot Engine::snapshot() const {
    const AudioPublication pub = pub_.load();
    Snapshot s;
    s.state = state_.load(std::memory_order_relaxed);
    s.generation = generation_.load(std::memory_order_relaxed);
    s.position_frames = pub.position_frames;
    s.duration_frames = duration_frames_.load(std::memory_order_relaxed);
    s.seekable = seekable_.load(std::memory_order_relaxed);
    s.sample_rate = source_rate_.load(std::memory_order_relaxed);
    s.channels = source_channels_.load(std::memory_order_relaxed);
    s.bitrate_bps = bitrate_bps_.load(std::memory_order_relaxed);
    s.peak = pub.peak;
    s.underruns = pub.underruns;
    s.clamp_hits = pub.clamp_hits;
    s.limiter_gain = pub.limiter_gain;
    s.vis_drops = pub.vis_drops;
    s.replaygain_db = replaygain_db_.load(std::memory_order_relaxed);
    s.replaygain_present = replaygain_present_.load(std::memory_order_relaxed);
    return s;
}

std::string Engine::last_error() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

// -------------------------------------------------------------- thread de audio

void Engine::render(float* out, std::uint32_t frames) noexcept {
    const std::size_t wanted = static_cast<std::size_t>(frames) * channels_;
    const State st = state_.load(std::memory_order_relaxed);

    std::size_t got = 0;
    if (st == State::Playing) {
        got = ring_.read(out, wanted);
        if (got < wanted) {
            std::memset(out + got, 0, (wanted - got) * sizeof(float));
            if (eof_.load(std::memory_order_acquire)) {
                if (got == 0) {
                    // Fim natural da faixa. O sinalizador distingue isso de um
                    // stop() do usuario, que e quem o Controller precisa saber
                    // para decidir se avanca (PL-24).
                    ended_.store(true, std::memory_order_release);
                    state_.store(State::Stopped, std::memory_order_relaxed);
                }
            } else {
                underruns_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    } else {
        std::memset(out, 0, wanted * sizeof(float));
    }

    // Cadeia de processamento — ARCHITECTURE.md secao 5. A ordem importa: o
    // ReplayGain entra antes do equalizador para que o preamp opere sobre um
    // nivel ja normalizado, e o limitador fica por ultimo para ver o sinal
    // exatamente como ele sairia para o dispositivo.
    replaygain_.process(out, frames, channels_);   // 1
    equalizer_.process(out, frames);               // 2 (preamp) e 3 (cascata)

    // PONTO DE CAPTURA da visualizacao: depois do equalizador, antes de
    // balanco e volume. O espectro reage ao equalizador — que e o retorno que o
    // usuario espera — e nao encolhe quando o volume baixa, o que deixaria a
    // visualizacao morta em volume zero.
    //
    // So captura tocando: em pausa o consumidor para de ler (congela o quadro,
    // VI-15), e continuar escrevendo silencio encheria o ring e inflaria o
    // contador de descartes sem nada de errado ter acontecido. Medido: 500
    // descartes em meio segundo de pausa.
    if (st == State::Playing && capture_enabled_.load(std::memory_order_relaxed)) {
        // VI-21 — descarte, nunca sobrescrita. Sobrescrever dado nao consumido
        // e corrida: o consumidor pode estar lendo exatamente aquela regiao.
        // O callback nao espera e nao escreve por cima; o bloco novo se perde e
        // o contador registra.
        if (vis_ring_.writable() >= wanted)
            vis_ring_.write(out, wanted);
        else
            vis_drops_.fetch_add(1, std::memory_order_relaxed);
    }
    volume_balance_.process(out, frames);          // 4 e 5
    limiter_.process(out, frames);                 // 6 e 7

    float peak = 0.0f;
    for (std::size_t i = 0; i < wanted; ++i) peak = std::max(peak, std::fabs(out[i]));

    const std::int64_t advanced = static_cast<std::int64_t>(got / channels_);
    const std::int64_t consumed =
        consumed_frames_.fetch_add(advanced, std::memory_order_relaxed) + advanced;

    // AU-13 — cruzou a marca de uma faixa emendada: troca os metadados
    // publicados e reinicia a contagem de posicao, sem tocar no fluxo de audio.
    //
    // ponytail: a troca acontece na granularidade do bloco de render, nao da
    // amostra. O erro maximo e um bloco (~10 ms) na posicao exibida; o audio
    // nao e afetado, que e o que gapless significa.
    while (const TrackSegment* segment = segments_.peek()) {
        if (segment->start_frame > consumed) break;
        apply_segment(*segment);
        segments_.pop();
    }

    const std::int64_t position = position_base_ + (consumed - track_origin_);

    AudioPublication pub;
    pub.position_frames = position;
    pub.peak = peak;
    pub.underruns = underruns_.load(std::memory_order_relaxed);
    pub.clamp_hits = limiter_.clamp_hits();
    pub.vis_drops = vis_drops_.load(std::memory_order_relaxed);
    pub.limiter_gain = limiter_.gain_reduction();
    pub_.store(pub);
}

std::size_t Engine::read_visualization(float* destination, std::size_t max_floats) {
    return vis_ring_.read(destination, max_floats);
}

void Engine::apply_segment(const TrackSegment& segment) noexcept {
    // So aqui a faixa emendada passa a ser "a que esta tocando": este ponto e o
    // instante em que o audio dela alcanca a saida.
    generation_.store(segment.generation, std::memory_order_relaxed);
    track_origin_ = segment.start_frame;
    position_base_ = 0;
    duration_frames_.store(segment.duration_frames, std::memory_order_relaxed);
    bitrate_bps_.store(segment.bitrate_bps, std::memory_order_relaxed);
    source_rate_.store(segment.sample_rate, std::memory_order_relaxed);
    source_channels_.store(segment.channels, std::memory_order_relaxed);
    seekable_.store(segment.seekable, std::memory_order_relaxed);
    replaygain_db_.store(segment.replaygain_db, std::memory_order_relaxed);
    replaygain_present_.store(segment.replaygain_present, std::memory_order_relaxed);
    replaygain_.set_db(segment.replaygain_db);
    current_token_.store(segment.token, std::memory_order_release);
}

}  // namespace pang::core
