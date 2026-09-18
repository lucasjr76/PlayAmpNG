#include "core/meta/tags.h"

#include <charconv>
#include <cstdlib>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

namespace pang::core::meta {
namespace {

std::string entry(AVDictionary* dict, const char* key) {
    if (!dict) return {};
    const AVDictionaryEntry* e = av_dict_get(dict, key, nullptr, AV_DICT_IGNORE_SUFFIX);
    return e && e->value ? e->value : std::string{};
}

// "7", "7/12" e "07" viram 7. Texto invalido vira 0, que significa ausente.
int parse_leading_int(const std::string& text) {
    int value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{}) return 0;
    return value;
}

}  // namespace

Track read_tags(const std::string& path) {
    Track t;
    t.path = path;

    av_log_set_level(AV_LOG_ERROR);

    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return t;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return t;
    }

    AVDictionary* meta = fmt->metadata;
    t.title = entry(meta, "title");
    t.artist = entry(meta, "artist");
    if (t.artist.empty()) t.artist = entry(meta, "album_artist");
    t.album = entry(meta, "album");
    t.genre = entry(meta, "genre");
    t.track_number = parse_leading_int(entry(meta, "track"));

    std::string date = entry(meta, "date");
    if (date.empty()) date = entry(meta, "year");
    t.year = parse_leading_int(date);
    if (t.year < 1000 || t.year > 3000) t.year = 0;  // "2024-05-01" ok, lixo nao

    // Algumas tags vivem no fluxo, nao no conteiner (comum em Ogg e FLAC).
    const int index = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (index >= 0) {
        AVDictionary* stream_meta = fmt->streams[index]->metadata;
        if (t.title.empty()) t.title = entry(stream_meta, "title");
        if (t.artist.empty()) t.artist = entry(stream_meta, "artist");
        if (t.album.empty()) t.album = entry(stream_meta, "album");
        if (t.genre.empty()) t.genre = entry(stream_meta, "genre");
    }

    // PL-26 — duracao desconhecida continua -1, nunca vira zero.
    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
        t.duration_ms = fmt->duration / 1000;

    avformat_close_input(&fmt);
    t.metadata_loaded = true;
    return t;
}

}  // namespace pang::core::meta
