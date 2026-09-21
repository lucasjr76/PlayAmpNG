#include "core/audio/probe.h"

#include "core/audio/network.h"

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

ProbeResult describe(AVFormatContext* fmt, int stream_index, const std::string& url) {
    const AVCodecParameters* par = fmt->streams[stream_index]->codecpar;

    ProbeResult r;
    r.format = fmt->iformat->long_name ? fmt->iformat->long_name : fmt->iformat->name;
    r.codec = avcodec_get_name(par->codec_id);
    r.sample_rate = par->sample_rate;
    r.channels = par->ch_layout.nb_channels;

    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
        r.duration_us = fmt->duration;

    // Prefere o bitrate do fluxo; cai para o do conteiner. Zero significa
    // desconhecido no FFmpeg, e desconhecido nao vira numero aqui.
    if (par->bit_rate > 0)
        r.bitrate_bps = par->bit_rate;
    else if (fmt->bit_rate > 0)
        r.bitrate_bps = fmt->bit_rate;

    r.seekable = fmt->pb && (fmt->pb->seekable & AVIO_SEEKABLE_NORMAL);

    // MD-09 — fonte ao vivo e a que nao pode ser buscada E nao informa
    // duracao. As duas condicoes juntas, porque arquivo em disco corrompido
    // pode nao informar duracao sem por isso ser stream, e um podcast em HTTP
    // pode ser pesquisavel e ter duracao.
    r.live = is_remote(url) && !r.seekable && !r.duration_us.has_value();

    // MD-05 — nome da estacao, quando o servidor envia.
    if (const AVDictionaryEntry* e = av_dict_get(fmt->metadata, "icy-name", nullptr, 0))
        if (e->value && *e->value) r.station = e->value;
    if (!r.station)
        if (const AVDictionaryEntry* e = av_dict_get(fmt->metadata, "StreamTitle", nullptr, 0))
            if (e->value && *e->value) r.station = e->value;

    return r;
}

std::optional<ProbeResult> probe(const std::string& url, std::string& error) {
    route_ffmpeg_log();

    FormatCloser fmt;
    AVDictionary* opts = nullptr;
    apply_network_options(&opts);
    const int rc_open = avformat_open_input(&fmt.ctx, url.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (rc_open < 0) {
        error = "nao foi possivel abrir: " + av_error(rc_open);
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

    const ProbeResult r = describe(fmt.ctx, idx, url);

    return r;
}

std::string ffmpeg_license() { return avutil_license(); }

std::string ffmpeg_version() {
    const char* v = av_version_info();
    return v ? v : "desconhecida";
}

}  // namespace pang::core
