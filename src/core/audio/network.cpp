#include "core/audio/network.h"

extern "C" {
#include <libavutil/dict.h>
}

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace pang::core {
namespace {

// "http://..." -> "http". Vazio quando nao ha esquema (caminho local).
std::string scheme_of(const std::string& url) {
    const std::size_t colon = url.find("://");
    if (colon == std::string::npos) return {};
    std::string scheme = url.substr(0, colon);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return scheme;
}

}  // namespace

bool is_remote(const std::string& url) {
    const std::string scheme = scheme_of(url);
    return scheme == "http" || scheme == "https" || scheme == "hls";
}

void apply_network_options(AVDictionary** opts) {
    av_dict_set(opts, "protocol_whitelist", kProtocolWhitelist, 0);

    // Em microssegundos. Junto com o interrupt callback do Decoder, garante que
    // nenhuma abertura ou leitura fica presa indefinidamente.
    av_dict_set_int(opts, "rw_timeout", static_cast<std::int64_t>(kReadTimeoutMs) * 1000, 0);

    // MD-06 — o libavformat segue redirecionamento sozinho; o limite existe
    // para que uma corrente de redirecionamentos nao vire laco.
    av_dict_set_int(opts, "max_reload", kReconnectAttempts, 0);

    // NAO ligamos "reconnect"/"reconnect_streamed". Elas fazem o libavformat
    // reabrir a conexao por baixo, de forma invisivel e sem limite de contagem,
    // e isso tem dois efeitos ruins: probe() nunca ve o fim de um stream e fica
    // lendo indefinidamente, e a reconexao exigida por MD-07 — com numero e
    // intervalo de tentativas limitados — deixa de ser observavel, porque
    // acontece uma camada abaixo de quem deveria conta-la. Quem reconecta e o
    // Engine, que conta, registra em log e desiste.

    // Limita o que find_stream_info consome antes de decidir. Sem teto, numa
    // fonte ao vivo ele le ate o timeout so para tentar estimar duracao que a
    // fonte nunca vai informar.
    av_dict_set_int(opts, "probesize", 512 * 1024, 0);
    av_dict_set_int(opts, "analyzeduration", 2 * 1000000, 0);

    // MD-05 — pede os metadados ICY ao servidor. Sem isto o Shoutcast/Icecast
    // nao envia o nome da estacao nem o titulo da faixa corrente.
    av_dict_set(opts, "icy", "1", 0);

    // Alguns servidores recusam cliente sem identificacao.
    av_dict_set(opts, "user_agent", "PlayAmpNG", 0);
}

}  // namespace pang::core
