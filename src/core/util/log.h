#pragma once

#include <cstdio>
#include <string>
#include <string_view>

// Log de diagnostico.
//
// AU-21 — nada aqui pode ser chamado do thread de audio. A solucao nao e um
// log assincrono: e nao ter caminho de log no callback. O thread de audio
// apenas incrementa contadores atomicos (underruns, atuacoes do clamp,
// descartes da visualizacao) que outra thread le e registra. Assim nao existe
// fila a esvaziar nem alocacao a evitar — nao existe o problema.
namespace pang::core::log {

// AR-06 — remove credenciais antes de qualquer registro.
//
// Trata `esquema://usuario:senha@host` e parametros de consulta conhecidos por
// carregar segredo. Usada tambem nas mensagens de erro exibidas na interface,
// nao so no arquivo de log.
std::string redact(std::string_view url);

// AR-06 — a mesma redacao, aplicada a cada URL que aparecer dentro de um
// TEXTO LIVRE.
//
// Existe por causa do FFmpeg: as mensagens dele passam a ir para o log do
// player, e nao se controla o que elas contem. Medido nos caminhos de rede
// testados, nenhuma trouxe credencial; esta e a rede de seguranca para os
// caminhos que nao foram testados.
std::string redact_text(std::string_view text);

// AR-05 — alem do stderr, grava num arquivo.
//
// Um log que o usuario nao encontra nao serve para diagnostico. Aberto o
// player pelo menu do sistema, o stderr vai para o journal no Linux e para
// lugar nenhum no Windows. Com o arquivo, "manda o log" tem um lugar certo
// nos tres sistemas. Vazio desliga.
//
// Recebe o arquivo JA ABERTO e passa a ser dono dele. Abrir fica com quem
// chama porque, no Windows, um caminho com acento so abre por _wfopen — e isso
// exige distinguir o sistema, o que core/ nao faz (AR-07). nullptr desliga.
void set_file(std::FILE* file);

void info(std::string_view message);
void warn(std::string_view message);
void error(std::string_view message);

}  // namespace pang::core::log
