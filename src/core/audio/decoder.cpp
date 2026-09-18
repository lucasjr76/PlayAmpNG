#include "core/audio/decoder.h"

#include <algorithm>
#include <cstring>
#include <optional>

#include "core/util/log.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace pang::core {
namespace {

// "-6.54 dB" -> -6.54. Texto invalido devolve nullopt.
std::optional<float> parse_replaygain(const char* text) {
    if (!text) return std::nullopt;
    try {
        return std::stof(text);
    } catch (...) {
        return std::nullopt;
    }
}

// Le ReplayGain das tags do conteiner e do fluxo.
//
// Dois formatos convivem: as tags replaygain_* classicas, em dB relativos a
// -18 LUFS, e R128_*_GAIN do Opus, em Q7.8 relativo a -23 LUFS. A diferenca de
// referencia entre os dois padroes e de 5 dB, e ignora-la deixaria faixas Opus
// 5 dB mais baixas que o resto da biblioteca.
void read_replaygain(AVDictionary* dict, std::optional<float>& track,
                     std::optional<float>& album) {
    if (!dict) return;
    if (const AVDictionaryEntry* e = av_dict_get(dict, "replaygain_track_gain", nullptr, 0))
        if (auto v = parse_replaygain(e->value)) track = v;
    if (const AVDictionaryEntry* e = av_dict_get(dict, "replaygain_album_gain", nullptr, 0))
        if (auto v = parse_replaygain(e->value)) album = v;

    constexpr float kR128ToReplayGainDb = 5.0f;
    if (!track)
        if (const AVDictionaryEntry* e = av_dict_get(dict, "R128_TRACK_GAIN", nullptr, 0))
            if (auto v = parse_replaygain(e->value))
                track = *v / 256.0f + kR128ToReplayGainDb;
    if (!album)
        if (const AVDictionaryEntry* e = av_dict_get(dict, "R128_ALBUM_GAIN", nullptr, 0))
            if (auto v = parse_replaygain(e->value))
                album = *v / 256.0f + kR128ToReplayGainDb;
}

std::string av_error(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof(buf));
    return buf;
}

}  // namespace

Decoder::Decoder() = default;

Decoder::~Decoder() { close(); }

int Decoder::interrupt_cb(void* opaque) {
    // Devolver 1 faz o libav abandonar a operacao bloqueante com erro. E o
    // unico jeito de parar, trocar de faixa ou fechar o aplicativo sem esperar
    // o timeout do sistema em leitura de rede ou de disco montado por rede.
    return static_cast<Decoder*>(opaque)->cancelled() ? 1 : 0;
}

bool Decoder::open(const std::string& url, int target_rate, int target_channels,
                   std::string& error) {
    close();
    clear_cancel();
    target_rate_ = target_rate;
    target_channels_ = target_channels;

    av_log_set_level(AV_LOG_ERROR);

    fmt_ = avformat_alloc_context();
    if (!fmt_) {
        error = "sem memoria para o contexto de formato";
        return false;
    }
    fmt_->interrupt_callback.callback = &Decoder::interrupt_cb;
    fmt_->interrupt_callback.opaque = this;

    // Timeout de leitura/escrita para protocolos que o suportam. Em conjunto
    // com o interrupt callback, garante que nenhuma abertura fica presa.
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rw_timeout", "10000000", 0);  // 10 s em microssegundos

    int rc = avformat_open_input(&fmt_, url.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (rc < 0) {
        error = "nao foi possivel abrir: " + av_error(rc);
        fmt_ = nullptr;  // avformat_open_input ja liberou em caso de falha
        close();
        return false;
    }

    if ((rc = avformat_find_stream_info(fmt_, nullptr)) < 0) {
        error = "sem informacao de fluxo: " + av_error(rc);
        close();
        return false;
    }

    stream_index_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index_ < 0) {
        error = "nenhum fluxo de audio encontrado";
        close();
        return false;
    }

    const AVCodecParameters* par = fmt_->streams[stream_index_]->codecpar;
    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    if (!dec) {
        error = std::string("sem decodificador para ") + avcodec_get_name(par->codec_id);
        close();
        return false;
    }

    codec_ = avcodec_alloc_context3(dec);
    if (!codec_ || avcodec_parameters_to_context(codec_, par) < 0) {
        error = "nao foi possivel configurar o decodificador";
        close();
        return false;
    }
    // NAO ligamos AV_CODEC_FLAG2_SKIP_MANUAL: por padrao o libavcodec ja apara
    // o delay do codificador e o padding do fim usando AV_PKT_DATA_SKIP_SAMPLES.
    // Deixar como esta e o que evita descarte duplicado no gapless (M3).
    if ((rc = avcodec_open2(codec_, dec, nullptr)) < 0) {
        error = "nao foi possivel abrir o decodificador: " + av_error(rc);
        close();
        return false;
    }

    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, target_channels_);
    rc = swr_alloc_set_opts2(&swr_, &out_layout, AV_SAMPLE_FMT_FLT, target_rate_,
                             &codec_->ch_layout, codec_->sample_fmt, codec_->sample_rate, 0,
                             nullptr);
    av_channel_layout_uninit(&out_layout);
    if (rc < 0 || swr_init(swr_) < 0) {
        error = "nao foi possivel configurar a reamostragem";
        close();
        return false;
    }

    pkt_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (!pkt_ || !frame_) {
        error = "sem memoria para pacote ou quadro";
        close();
        return false;
    }

    info_ = ProbeResult{};
    info_.format = fmt_->iformat->long_name ? fmt_->iformat->long_name : fmt_->iformat->name;
    info_.codec = avcodec_get_name(par->codec_id);
    info_.sample_rate = par->sample_rate;
    info_.channels = par->ch_layout.nb_channels;
    if (fmt_->duration != AV_NOPTS_VALUE && fmt_->duration > 0) info_.duration_us = fmt_->duration;
    if (par->bit_rate > 0)
        info_.bitrate_bps = par->bit_rate;
    else if (fmt_->bit_rate > 0)
        info_.bitrate_bps = fmt_->bit_rate;
    info_.seekable = fmt_->pb && (fmt_->pb->seekable & AVIO_SEEKABLE_NORMAL);

    read_replaygain(fmt_->metadata, info_.replaygain_track_db, info_.replaygain_album_db);
    read_replaygain(fmt_->streams[stream_index_]->metadata, info_.replaygain_track_db,
                    info_.replaygain_album_db);

    source_eof_ = false;
    drained_ = false;
    pending_frames_ = pending_offset_ = 0;
    return true;
}

void Decoder::close() {
    if (swr_) swr_free(&swr_);
    if (frame_) av_frame_free(&frame_);
    if (pkt_) av_packet_free(&pkt_);
    if (codec_) avcodec_free_context(&codec_);
    if (fmt_) avformat_close_input(&fmt_);
    fmt_ = nullptr;
    stream_index_ = -1;
    pending_.clear();
    pending_frames_ = pending_offset_ = 0;
    source_eof_ = drained_ = false;
    info_ = ProbeResult{};
}

std::int64_t Decoder::duration_frames() const {
    if (!info_.duration_us) return -1;
    return static_cast<std::int64_t>(static_cast<double>(*info_.duration_us) * target_rate_ / 1e6);
}

// Converte o quadro corrente (ou drena) para pending_.
void Decoder::drain_resampler() {
    // Sem isso, o atraso interno do filtro engole o final da faixa — e o buraco
    // resultante seria atribuido por engano ao mecanismo de emenda do gapless.
    for (;;) {
        const int room = swr_get_out_samples(swr_, 0);
        if (room <= 0) break;
        pending_.resize(static_cast<std::size_t>(room) * target_channels_);
        std::uint8_t* out = reinterpret_cast<std::uint8_t*>(pending_.data());
        const int got = swr_convert(swr_, &out, room, nullptr, 0);
        if (got <= 0) break;
        pending_frames_ = static_cast<std::size_t>(got);
        pending_offset_ = 0;
        return;
    }
    pending_frames_ = pending_offset_ = 0;
    drained_ = true;
}

void Decoder::discard_resampler() {
    // Usado apos seek: o que ficou dentro do reamostrador pertence a posicao
    // anterior e nao pode vazar para depois do salto.
    for (;;) {
        const int room = swr_get_out_samples(swr_, 0);
        if (room <= 0) break;
        std::vector<float> scratch(static_cast<std::size_t>(room) * target_channels_);
        std::uint8_t* out = reinterpret_cast<std::uint8_t*>(scratch.data());
        if (swr_convert(swr_, &out, room, nullptr, 0) <= 0) break;
    }
}

bool Decoder::fill_pending() {
    while (!drained_) {
        if (cancelled()) return false;

        int rc = avcodec_receive_frame(codec_, frame_);
        if (rc == 0) {
            const int room = swr_get_out_samples(swr_, frame_->nb_samples);
            if (room <= 0) {
                av_frame_unref(frame_);
                continue;
            }
            pending_.resize(static_cast<std::size_t>(room) * target_channels_);
            std::uint8_t* out = reinterpret_cast<std::uint8_t*>(pending_.data());
            const int got = swr_convert(swr_, &out, room,
                                        const_cast<const std::uint8_t**>(frame_->extended_data),
                                        frame_->nb_samples);
            av_frame_unref(frame_);
            if (got <= 0) continue;
            pending_frames_ = static_cast<std::size_t>(got);
            pending_offset_ = 0;
            return true;
        }

        if (rc == AVERROR_EOF) {
            drain_resampler();
            return pending_frames_ > 0;
        }

        if (rc != AVERROR(EAGAIN)) {
            log::error("erro ao decodificar: " + av_error(rc));
            drain_resampler();
            return pending_frames_ > 0;
        }

        // EAGAIN: o decodificador quer mais um pacote.
        if (source_eof_) {
            avcodec_send_packet(codec_, nullptr);  // sinaliza fim
            source_eof_ = false;                   // so uma vez
            continue;
        }

        const int rr = av_read_frame(fmt_, pkt_);
        if (rr < 0) {
            source_eof_ = true;
            continue;
        }
        if (pkt_->stream_index == stream_index_) avcodec_send_packet(codec_, pkt_);
        av_packet_unref(pkt_);
    }
    return false;
}

std::size_t Decoder::read(float* out, std::size_t max_frames) {
    if (!is_open()) return 0;

    std::size_t written = 0;
    while (written < max_frames) {
        if (pending_offset_ >= pending_frames_) {
            if (!fill_pending()) break;
        }
        const std::size_t available = pending_frames_ - pending_offset_;
        const std::size_t take = std::min(available, max_frames - written);
        std::memcpy(out + written * target_channels_,
                    pending_.data() + pending_offset_ * target_channels_,
                    take * target_channels_ * sizeof(float));
        pending_offset_ += take;
        written += take;
    }
    return written;
}

bool Decoder::seek(double seconds) {
    if (!is_open() || !info_.seekable) return false;

    const std::int64_t ts = static_cast<std::int64_t>(seconds * AV_TIME_BASE);
    const std::int64_t stream_ts =
        av_rescale_q(ts, AVRational{1, AV_TIME_BASE}, fmt_->streams[stream_index_]->time_base);

    if (av_seek_frame(fmt_, stream_index_, stream_ts, AVSEEK_FLAG_BACKWARD) < 0) return false;

    avcodec_flush_buffers(codec_);
    discard_resampler();
    pending_frames_ = pending_offset_ = 0;
    source_eof_ = false;
    drained_ = false;
    return true;
}

}  // namespace pang::core
