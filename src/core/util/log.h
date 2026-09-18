#pragma once

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

void info(std::string_view message);
void warn(std::string_view message);
void error(std::string_view message);

}  // namespace pang::core::log
