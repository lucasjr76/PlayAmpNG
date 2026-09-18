#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/audio/probe.h"

struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwrContext;

namespace pang::core {

// Decodificador de uma fonte de audio.
//
// Entrega sempre float32 intercalado na taxa e na contagem de canais do
// DISPOSITIVO, nao da fonte. E isso que torna o ring homogeneo e faz a emenda
// de gapless (M3) acontecer sobre amostras ja convertidas, sem caso especial
// para faixa com taxa ou layout diferente.
//
// Vive no thread de decodificacao. Nunca e tocado pelo thread de audio.
class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    bool open(const std::string& url, int target_rate, int target_channels, std::string& error);
    void close();
    bool is_open() const { return fmt_ != nullptr; }

    // Le ate max_frames quadros. Devolve quantos escreveu; 0 significa fim da
    // fonte, ja com o reamostrador drenado.
    std::size_t read(float* out, std::size_t max_frames);

    bool seek(double seconds);

    const ProbeResult& info() const { return info_; }

    // Duracao na taxa alvo. -1 quando a fonte nao informa (stream ao vivo).
    std::int64_t duration_frames() const;

    // AR-09 — faz o libav abandonar qualquer operacao bloqueante em andamento.
    // Seguro de chamar de outro thread.
    void cancel() { cancel_.store(true, std::memory_order_relaxed); }
    void clear_cancel() { cancel_.store(false, std::memory_order_relaxed); }
    bool cancelled() const { return cancel_.load(std::memory_order_relaxed); }

private:
    static int interrupt_cb(void* opaque);
    bool fill_pending();   // decodifica e converte o proximo bloco
    void drain_resampler();
    void discard_resampler();

    AVFormatContext* fmt_ = nullptr;
    AVCodecContext* codec_ = nullptr;
    AVPacket* pkt_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwrContext* swr_ = nullptr;

    int stream_index_ = -1;
    int target_rate_ = 0;
    int target_channels_ = 0;

    ProbeResult info_;

    std::vector<float> pending_;
    std::size_t pending_frames_ = 0;
    std::size_t pending_offset_ = 0;
    bool source_eof_ = false;   // libavcodec sinalizou fim
    bool drained_ = false;      // reamostrador ja drenado

    std::atomic<bool> cancel_{false};
};

}  // namespace pang::core
