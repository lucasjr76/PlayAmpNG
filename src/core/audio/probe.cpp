#include "core/audio/probe.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace pang::core {
namespace {

std::string av_error(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof(buf));
    return buf;
}

struct FormatCloser {
    AVFormatContext* ctx = nullptr;
    ~FormatCloser() {
        if (ctx) avformat_close_input(&ctx);
    }
};

}  // namespace

std::optional<ProbeResult> probe(const std::string& url, std::string& error) {
    av_log_set_level(AV_LOG_ERROR);

    FormatCloser fmt;
    if (int rc = avformat_open_input(&fmt.ctx, url.c_str(), nullptr, nullptr); rc < 0) {
        error = "nao foi possivel abrir: " + av_error(rc);
        return std::nullopt;
    }
    if (int rc = avformat_find_stream_info(fmt.ctx, nullptr); rc < 0) {
        error = "sem informacao de fluxo: " + av_error(rc);
        return std::nullopt;
    }

    const int idx = av_find_best_stream(fmt.ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (idx < 0) {
        error = "nenhum fluxo de audio encontrado";
        return std::nullopt;
    }

    const AVCodecParameters* par = fmt.ctx->streams[idx]->codecpar;

    ProbeResult r;
    r.format = fmt.ctx->iformat->long_name ? fmt.ctx->iformat->long_name : fmt.ctx->iformat->name;
    r.codec = avcodec_get_name(par->codec_id);
    r.sample_rate = par->sample_rate;
    r.channels = par->ch_layout.nb_channels;

    if (fmt.ctx->duration != AV_NOPTS_VALUE && fmt.ctx->duration > 0)
        r.duration_us = fmt.ctx->duration;

    // Prefere o bitrate do fluxo; cai para o do conteiner. Zero significa
    // desconhecido no FFmpeg, e desconhecido nao vira numero aqui.
    if (par->bit_rate > 0)
        r.bitrate_bps = par->bit_rate;
    else if (fmt.ctx->bit_rate > 0)
        r.bitrate_bps = fmt.ctx->bit_rate;

    r.seekable = fmt.ctx->pb && (fmt.ctx->pb->seekable & AVIO_SEEKABLE_NORMAL);

    return r;
}

std::string ffmpeg_license() { return avutil_license(); }

std::string ffmpeg_version() {
    const char* v = av_version_info();
    return v ? v : "desconhecida";
}

}  // namespace pang::core
