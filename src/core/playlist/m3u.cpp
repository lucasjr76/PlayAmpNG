#include "core/playlist/m3u.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace pang::core::playlist_io {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return std::string(s.substr(b, e - b));
}

bool is_url(const std::string& s) {
    const std::size_t scheme = s.find("://");
    return scheme != std::string::npos && scheme > 0 && scheme < 16;
}

// LI-13 — resolve relativo ao diretorio do arquivo de playlist.
std::string resolve(const std::string& entry, const fs::path& base) {
    if (entry.empty() || is_url(entry)) return entry;
    fs::path p(entry);
    // Playlists gravadas no Windows usam barra invertida.
    if (p.is_relative() && entry.find('\\') != std::string::npos) {
        std::string converted = entry;
        std::replace(converted.begin(), converted.end(), '\\', '/');
        p = fs::path(converted);
    }
    if (p.is_absolute()) return p.lexically_normal().string();
    return (base / p).lexically_normal().string();
}

std::vector<Track> load_m3u(std::istream& in, const fs::path& base) {
    std::vector<Track> out;
    std::string line;
    std::string pending_title;
    std::int64_t pending_duration = -1;

    while (std::getline(in, line)) {
        const std::string text = trim(line);
        if (text.empty()) continue;

        if (text[0] == '#') {
            // #EXTINF:<segundos>,<titulo>   — segundos -1 significa desconhecido
            if (text.rfind("#EXTINF:", 0) == 0) {
                const std::size_t comma = text.find(',', 8);
                const std::string seconds = text.substr(8, comma - 8);
                try {
                    const long value = std::stol(seconds);
                    pending_duration = value >= 0 ? value * 1000 : -1;
                } catch (...) {
                    pending_duration = -1;
                }
                if (comma != std::string::npos) pending_title = trim(text.substr(comma + 1));
            }
            continue;
        }

        Track t;
        t.path = resolve(text, base);
        t.duration_ms = pending_duration;
        if (!pending_title.empty()) {
            // "Artista - Titulo" e a convencao do EXTINF.
            const std::size_t dash = pending_title.find(" - ");
            if (dash != std::string::npos) {
                t.artist = pending_title.substr(0, dash);
                t.title = pending_title.substr(dash + 3);
            } else {
                t.title = pending_title;
            }
        }
        out.push_back(std::move(t));
        pending_title.clear();
        pending_duration = -1;
    }
    return out;
}

std::vector<Track> load_pls(std::istream& in, const fs::path& base) {
    // PLS e indexado: File1/Title1/Length1, File2/... A ordem das linhas no
    // arquivo nao e garantida, entao montamos por indice e so no fim achatamos.
    std::vector<Track> ordered;
    std::string line;

    auto slot = [&ordered](std::size_t index) -> Track& {
        if (ordered.size() < index) ordered.resize(index);
        return ordered[index - 1];
    };

    while (std::getline(in, line)) {
        const std::string text = trim(line);
        const std::size_t eq = text.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = lower(text.substr(0, eq));
        const std::string value = trim(text.substr(eq + 1));
        if (value.empty()) continue;

        auto index_after = [&key](const char* prefix) -> std::size_t {
            const std::size_t n = std::char_traits<char>::length(prefix);
            if (key.rfind(prefix, 0) != 0) return 0;
            try {
                return static_cast<std::size_t>(std::stoul(key.substr(n)));
            } catch (...) {
                return 0;
            }
        };

        if (const std::size_t i = index_after("file"); i > 0)
            slot(i).path = resolve(value, base);
        else if (const std::size_t j = index_after("title"); j > 0)
            slot(j).title = value;
        else if (const std::size_t k = index_after("length"); k > 0) {
            try {
                const long seconds = std::stol(value);
                slot(k).duration_ms = seconds >= 0 ? seconds * 1000 : -1;
            } catch (...) {
            }
        }
    }

    // Indices ausentes deixam buracos; remove.
    ordered.erase(std::remove_if(ordered.begin(), ordered.end(),
                                 [](const Track& t) { return t.path.empty(); }),
                  ordered.end());
    return ordered;
}

}  // namespace

Format format_for(const std::string& path) {
    const std::string lowered = lower(path);
    if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".pls") == 0)
        return Format::PLS;
    if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".m3u") == 0)
        return Format::M3U;
    return Format::M3U8;
}

std::vector<Track> load(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "nao foi possivel abrir a playlist: " + path;
        return {};
    }

    const fs::path base = fs::path(path).parent_path();
    std::vector<Track> tracks = format_for(path) == Format::PLS ? load_pls(in, base)
                                                               : load_m3u(in, base);
    if (tracks.empty()) error = "playlist vazia ou sem entradas reconheciveis";
    return tracks;
}

bool save(const std::string& path, const std::vector<Track>& tracks, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "nao foi possivel gravar a playlist: " + path;
        return false;
    }

    const Format format = format_for(path);
    const auto absolute_path = [](const Track& t) {
        std::error_code ec;
        const fs::path p(t.path);
        if (p.is_absolute() || t.path.find("://") != std::string::npos) return t.path;
        return fs::absolute(p, ec).lexically_normal().string();
    };

    if (format == Format::PLS) {
        out << "[playlist]\n";
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            const Track& t = tracks[i];
            out << "File" << (i + 1) << '=' << absolute_path(t) << '\n';
            out << "Title" << (i + 1) << '=' << t.display_title() << '\n';
            // -1 e o valor que o PLS usa para duracao desconhecida.
            out << "Length" << (i + 1) << '='
                << (t.duration_ms >= 0 ? t.duration_ms / 1000 : -1) << '\n';
        }
        out << "NumberOfEntries=" << tracks.size() << "\nVersion=2\n";
    } else {
        out << "#EXTM3U\n";
        for (const Track& t : tracks) {
            out << "#EXTINF:" << (t.duration_ms >= 0 ? t.duration_ms / 1000 : -1) << ','
                << t.display_title() << '\n';
            out << absolute_path(t) << '\n';
        }
    }

    if (!out) {
        error = "falha ao escrever a playlist";
        return false;
    }
    return true;
}

}  // namespace pang::core::playlist_io
