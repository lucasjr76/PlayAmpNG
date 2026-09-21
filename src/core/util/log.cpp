#include "core/util/log.h"

#include <array>
#include <chrono>
#include <ctime>
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

std::FILE* g_file = nullptr;

// AR-05 — hora local com milissegundos em cada linha.
//
// Sem ela o log nao responde a pergunta mais comum de um relato: "o som parou
// as 15h". Tambem e o que mostra, numa reconexao, que as tentativas estao de
// fato espacadas — antes o log dizia "tentativa 1, 2, 3" sem dizer se foram
// em um segundo ou em um minuto.
//
// ponytail: std::localtime usa um buffer estatico e nao e reentrante. Aqui e
// chamado sempre sob sink_mutex, e nada mais no programa o usa; se algum dia
// usar, trocar por std::chrono::zoned_time quando as tres bibliotecas-padrao
// suportarem current_zone().
void stamp(char (&buffer)[16]) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()).count() % 1000;
    const std::tm* local = std::localtime(&seconds);
    std::snprintf(buffer, sizeof buffer, "%02d:%02d:%02d.%03d", local ? local->tm_hour : 0,
                  local ? local->tm_min : 0, local ? local->tm_sec : 0, static_cast<int>(millis));
}

void emit(const char* level, std::string_view message) {
    std::lock_guard<std::mutex> lock(sink_mutex());
    char when[16];
    stamp(when);
    const int size = static_cast<int>(message.size());
    std::fprintf(stderr, "%s [%s] %.*s\n", when, level, size, message.data());
    if (g_file) {
        std::fprintf(g_file, "%s [%s] %.*s\n", when, level, size, message.data());
        // Sem esvaziar a cada linha, o que um travamento interrompe e
        // justamente o fim do log — as linhas que explicariam o travamento.
        std::fflush(g_file);
    }
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

std::string redact_text(std::string_view text) {
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t scheme = text.find("://", pos);
        if (scheme == std::string_view::npos) break;

        // O inicio da URL e o inicio da palavra que contem o "://".
        std::size_t start = scheme;
        while (start > pos && !std::isspace(static_cast<unsigned char>(text[start - 1])) &&
               text[start - 1] != '\'' && text[start - 1] != '"' && text[start - 1] != '(')
            --start;
        std::size_t end = scheme + 3;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) &&
               text[end] != '\'' && text[end] != '"' && text[end] != ')')
            ++end;

        out.append(text.substr(pos, start - pos));
        out += redact(text.substr(start, end - start));
        pos = end;
    }
    out.append(text.substr(pos));
    return out;
}

void set_file(std::FILE* file) {
    std::lock_guard<std::mutex> lock(sink_mutex());
    if (g_file) std::fclose(g_file);
    g_file = file;
}

void info(std::string_view message) { emit("info", message); }
void warn(std::string_view message) { emit("aviso", message); }
void error(std::string_view message) { emit("erro", message); }

}  // namespace pang::core::log
