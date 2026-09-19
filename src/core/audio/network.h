#pragma once

#include <string>

struct AVDictionary;

namespace pang::core {

// Opcoes de rede para abrir uma fonte com o libavformat.
//
// Existe como ponto unico porque probe() e Decoder::open() abrem a MESMA url e
// precisam das mesmas garantias. Se divergirem, o probe aceita uma fonte que o
// decodificador recusa — ou pior, aplica uma restricao de seguranca em um
// caminho e nao no outro.
//
// O chamador e dono do dicionario e o libera com av_dict_free.
void apply_network_options(AVDictionary** opts);

// Protocolos que o libavformat pode usar. Tudo o mais e recusado.
//
// Sem essa lista, uma entrada de playlist pode pedir "concat:", "subfile:" ou
// um protocolo de arquivo local encadeado e fazer o player ler o que nao devia.
// A url vem de arquivo M3U que o usuario nao escreveu, entao ela e entrada
// nao confiavel e a lista e restritiva de proposito.
inline constexpr const char* kProtocolWhitelist =
    "file,crypto,data,hls,http,https,httpproxy,tcp,tls,unix";

// MD-07 — limites de reconexao. Valem para o libavformat (que reabre a conexao
// TCP sozinho dentro de uma leitura) e para o nivel acima, que reabre a fonte
// inteira quando o fluxo morre de vez.
inline constexpr int kReconnectAttempts = 3;
inline constexpr int kReconnectDelayMs = 1000;
inline constexpr int kReadTimeoutMs = 10000;

// Verdadeiro para url que sai da maquina. Fontes locais nao levam as opcoes de
// rede e nao sao candidatas a reconexao.
bool is_remote(const std::string& url);

}  // namespace pang::core
