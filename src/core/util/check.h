#pragma once

#include <cstdio>
#include <string_view>

// AR-10 — verificacao que nao desaparece em build de release.
//
// assert() some quando NDEBUG esta definido, e RelWithDebInfo (a configuracao
// padrao deste projeto) define NDEBUG. Um teste cujas verificacoes evaporam
// justamente na configuracao em que o produto e construido nao verifica nada.
//
// PANG_CHECK avalia sempre a condicao, registra a falha, continua a execucao —
// um teste deve relatar todas as falhas, nao so a primeira — e faz o processo
// sair com codigo diferente de zero.

namespace pang::check {

inline int failures = 0;

inline void report(bool ok, std::string_view expr, std::string_view msg, const char* file,
                   int line) {
    if (ok) return;
    ++failures;
    std::fprintf(stderr, "FALHA %s:%d: %.*s\n           condicao: %.*s\n", file, line,
                 static_cast<int>(msg.size()), msg.data(), static_cast<int>(expr.size()),
                 expr.data());
}

// Codigo de saida para o main do binario de teste.
inline int exit_code() {
    if (failures == 0) {
        std::printf("todas as verificacoes passaram\n");
        return 0;
    }
    std::fprintf(stderr, "%d verificacao(oes) falharam\n", failures);
    return 1;
}

}  // namespace pang::check

#define PANG_CHECK(cond, msg) \
    ::pang::check::report(static_cast<bool>(cond), #cond, (msg), __FILE__, __LINE__)
