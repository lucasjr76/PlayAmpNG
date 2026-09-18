#include "core/util/log.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <mutex>

namespace pang::core::log {
namespace {

// Parametros de consulta cujo valor nunca deve aparecer no log.
constexpr std::array<std::string_view, 6> kSecretParams{"token",    "api_key",  "apikey",
                                                        "password", "auth",     "session"};

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::mutex& sink_mutex() {
    static std::mutex m;
    return m;
}

void emit(const char* level, std::string_view message) {
    std::lock_guard<std::mutex> lock(sink_mutex());
    std::fprintf(stderr, "[%s] %.*s\n", level, static_cast<int>(message.size()), message.data());
}

}  // namespace

std::string redact(std::string_view url) {
    std::string out(url);

    // 1. esquema://usuario:senha@host  ->  esquema://***@host
    const std::size_t scheme = out.find("://");
    if (scheme != std::string::npos) {
        const std::size_t host_start = scheme + 3;
        // O '@' so conta como separador de credencial se vier antes da barra
        // que inicia o caminho.
        const std::size_t path = out.find('/', host_start);
        const std::size_t at = out.find('@', host_start);
        if (at != std::string::npos && (path == std::string::npos || at < path))
            out.replace(host_start, at - host_start, "***");
    }

    // 2. ?token=...&password=...  ->  ?token=***&password=***
    std::size_t pos = out.find('?');
    while (pos != std::string::npos) {
        const std::size_t key_start = pos + 1;
        const std::size_t eq = out.find('=', key_start);
        if (eq == std::string::npos) break;

        const std::string_view key(out.data() + key_start, eq - key_start);
        std::size_t value_end = out.find('&', eq + 1);
        if (value_end == std::string::npos) value_end = out.size();

        bool secret = false;
        for (std::string_view s : kSecretParams)
            if (iequals(key, s)) secret = true;

        if (secret) {
            out.replace(eq + 1, value_end - eq - 1, "***");
            value_end = eq + 1 + 3;
        }
        pos = value_end < out.size() ? value_end : std::string::npos;
    }

    return out;
}

void info(std::string_view message) { emit("info", message); }
void warn(std::string_view message) { emit("aviso", message); }
void error(std::string_view message) { emit("erro", message); }

}  // namespace pang::core::log
