#pragma once

#include <cstdint>
#include <vector>

namespace pang::core {

// Ordem embaralhada com historico de navegacao.
//
// LI-15 — um ciclo percorre todos os itens sem repetir nenhum; so ao terminar
// o ciclo um novo embaralhamento comeca. LI-14 — voltar percorre o historico
// real, nao um sorteio novo, senao "anterior" no shuffle levaria a uma faixa
// que nunca tocou.
class ShuffleOrder {
public:
    // Novo ciclo sobre `count` itens. Se `start` for valido, ele vira o
    // primeiro item do ciclo, para que ativar shuffle nao pule a faixa atual.
    void reset(int count, int start, std::uint64_t seed);

    // A playlist mudou: o ciclo atual nao vale mais.
    void invalidate() { order_.clear(); cursor_ = -1; }
    bool valid_for(int count) const { return static_cast<int>(order_.size()) == count; }

    int current() const;

    // -1 quando o ciclo terminou. Quem chama decide o que fazer conforme o
    // modo de repeticao.
    int next();

    // -1 quando ja esta no inicio do historico.
    int previous();

    int position() const { return cursor_; }
    int size() const { return static_cast<int>(order_.size()); }

private:
    std::vector<int> order_;
    int cursor_ = -1;
};

}  // namespace pang::core
