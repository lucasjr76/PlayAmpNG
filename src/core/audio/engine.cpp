#include "core/audio/engine.h"

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

constexpr double kVolumeRampSeconds = 0.020;  // AU-10

}  // namespace

Engine::Engine(int sample_rate, int channels)
    : sample_rate_(sample_rate),
      channels_(channels),
      ring_(static_cast<std::size_t>(sample_rate * kRingSeconds) * channels) {
    replaygain_.configure(sample_rate, kVolumeRampSeconds);
    equalizer_.configure(sample_rate, channels);
    volume_balance_.configure(sample_rate, channels);
    limiter_.configure(sample_rate, channels);
}

Engine::~Engine() { join_decoder(); }

// ---------------------------------------------------------------- controle

void Engine::load(const std::string& url, State desired) {
    join_decoder();
    decoder_.close();
    ring_.reset();

    // AR-03 — toda carga tem geracao propria. Resposta assincrona carimbada
    // com geracao antiga e descartada em vez de sobrescrever o estado atual.
    generation_.fetch_add(1, std::memory_order_relaxed);

    position_frames_.store(0, std::memory_order_relaxed);
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
    state_.store(State::Loading, std::memory_order_relaxed);

    start_decoder(url, desired);
}

void Engine::stop() {
    join_decoder();
    ring_.reset();
    // PL-03 — parar volta a posicao 0 e mantem a faixa carregada.
    position_frames_.store(0, std::memory_order_relaxed);
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
    eof_.store(false, std::memory_order_relaxed);
    ended_.store(false, std::memory_order_release);
    const std::int64_t target = static_cast<std::int64_t>(seconds * sample_rate_);
    position_frames_.store(target, std::memory_order_relaxed);

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

        const ProbeResult& info = decoder_.info();
        duration_frames_.store(decoder_.duration_frames(), std::memory_order_relaxed);
        bitrate_bps_.store(info.bitrate_bps ? *info.bitrate_bps : -1, std::memory_order_relaxed);
        source_rate_.store(info.sample_rate, std::memory_order_relaxed);
        source_channels_.store(info.channels, std::memory_order_relaxed);
        seekable_.store(info.seekable, std::memory_order_relaxed);
        apply_replaygain(info);
    }

    std::vector<float> chunk(kDecodeChunkFrames * channels_);
    bool announced = url.empty();  // no seek o estado ja foi definido

    while (!thread_quit_.load(std::memory_order_relaxed)) {
        const std::size_t room_frames = ring_.writable() / channels_;
        if (room_frames == 0) {
            // ponytail: espera fixa em vez de variavel de condicao. 2 ms com
            // ~2 s de buffer e irrelevante; trocar se o perfil apontar.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        const std::size_t want = std::min(room_frames, kDecodeChunkFrames);
        const std::size_t got = decoder_.read(chunk.data(), want);
        if (got == 0) {
            eof_.store(true, std::memory_order_release);
            break;
        }
        ring_.write(chunk.data(), got * channels_);

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
    // [M4: ponto de captura da visualizacao entra aqui]
    volume_balance_.process(out, frames);          // 4 e 5
    limiter_.process(out, frames);                 // 6 e 7

    float peak = 0.0f;
    for (std::size_t i = 0; i < wanted; ++i) peak = std::max(peak, std::fabs(out[i]));

    const std::int64_t advanced = static_cast<std::int64_t>(got / channels_);
    const std::int64_t position =
        position_frames_.fetch_add(advanced, std::memory_order_relaxed) + advanced;

    AudioPublication pub;
    pub.position_frames = position;
    pub.peak = peak;
    pub.underruns = underruns_.load(std::memory_order_relaxed);
    pub.clamp_hits = limiter_.clamp_hits();
    pub_.store(pub);
}

}  // namespace pang::core
