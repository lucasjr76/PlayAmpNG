#include "core/audio/engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

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
      ring_(static_cast<std::size_t>(sample_rate * kRingSeconds) * channels) {}

Engine::~Engine() { join_decoder(); }

// ---------------------------------------------------------------- controle

void Engine::load(const std::string& url, bool start_playing) {
    join_decoder();
    decoder_.close();
    ring_.reset();

    // AR-03 — toda carga tem geracao propria. Resposta assincrona carimbada
    // com geracao antiga e descartada em vez de sobrescrever o estado atual.
    generation_.fetch_add(1, std::memory_order_relaxed);

    position_frames_.store(0, std::memory_order_relaxed);
    eof_.store(false, std::memory_order_relaxed);
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

    start_decoder(url, start_playing);
}

void Engine::stop() {
    join_decoder();
    ring_.reset();
    // PL-03 — parar volta a posicao 0 e mantem a faixa carregada.
    position_frames_.store(0, std::memory_order_relaxed);
    eof_.store(false, std::memory_order_relaxed);
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
    decoder_thread_ = std::thread([this, resume] { decode_loop(std::string{}, resume); });
    return true;
}

void Engine::play() {
    const State s = state_.load(std::memory_order_relaxed);
    if (s == State::Paused || s == State::Loading) state_.store(State::Playing);
}

void Engine::pause() {
    if (state_.load(std::memory_order_relaxed) == State::Playing) state_.store(State::Paused);
}

void Engine::set_volume(float linear) {
    volume_target_.store(std::clamp(linear, 0.0f, 1.0f), std::memory_order_relaxed);
}

// ------------------------------------------------------ thread de decodificacao

void Engine::start_decoder(const std::string& url, bool start_playing) {
    thread_quit_.store(false, std::memory_order_relaxed);
    decoder_.clear_cancel();
    decoder_thread_ = std::thread([this, url, start_playing] { decode_loop(url, start_playing); });
}

void Engine::join_decoder() {
    thread_quit_.store(true, std::memory_order_relaxed);
    decoder_.cancel();  // AR-09 — solta o libav de qualquer bloqueio
    if (decoder_thread_.joinable()) decoder_thread_.join();
    thread_quit_.store(false, std::memory_order_relaxed);
    decoder_.clear_cancel();
}

// url vazia significa "continuar com o decodificador ja aberto" (caso do seek).
void Engine::decode_loop(std::string url, bool start_playing) {
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
            state_.store(start_playing ? State::Playing : State::Paused,
                         std::memory_order_relaxed);
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
                if (got == 0) state_.store(State::Stopped, std::memory_order_relaxed);
            } else {
                underruns_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    } else {
        std::memset(out, 0, wanted * sizeof(float));
    }

    // AU-10 — rampa de volume. Sem ela, mudanca de ganho vira degrau e estala.
    float v = volume_current_.load(std::memory_order_relaxed);
    const float target = volume_target_.load(std::memory_order_relaxed);
    const float step = 1.0f / (kVolumeRampSeconds * static_cast<float>(sample_rate_));

    float peak = 0.0f;
    for (std::uint32_t f = 0; f < frames; ++f) {
        if (v < target)
            v = std::min(target, v + step);
        else if (v > target)
            v = std::max(target, v - step);

        float* frame = out + static_cast<std::size_t>(f) * channels_;
        for (int c = 0; c < channels_; ++c) {
            frame[c] *= v;
            peak = std::max(peak, std::fabs(frame[c]));
        }
    }
    volume_current_.store(v, std::memory_order_relaxed);

    const std::int64_t advanced = static_cast<std::int64_t>(got / channels_);
    const std::int64_t position =
        position_frames_.fetch_add(advanced, std::memory_order_relaxed) + advanced;

    AudioPublication pub;
    pub.position_frames = position;
    pub.peak = peak;
    pub.underruns = underruns_.load(std::memory_order_relaxed);
    pub_.store(pub);
}

}  // namespace pang::core
