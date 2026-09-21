#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/playlist/track.h"

namespace pang::core {

// Modelo da playlist. Sem Qt, sem thread: e uma estrutura de dados.
//
// A leitura de metadados nao acontece aqui (LI-16 exige que seja assincrona);
// quem faz isso e meta::Scanner, que devolve resultados para serem aplicados
// com apply_metadata().
class Playlist {
public:
    int size() const { return static_cast<int>(tracks_.size()); }
    bool empty() const { return tracks_.empty(); }
    const Track& at(int index) const { return tracks_[static_cast<std::size_t>(index)]; }
    const std::vector<Track>& tracks() const { return tracks_; }

    // Devolve o indice do item inserido.
    int add(std::string path);

    // LI-03 — varredura de diretorio, recursiva ou nao. Devolve quantos entraram.
    // Ordena por caminho para que o resultado nao dependa da ordem do sistema
    // de arquivos, que varia entre maquinas e entre execucoes.
    int add_directory(const std::string& directory, bool recursive);

    // LI-06 — remove da lista sem tocar nos arquivos originais.
    void remove(std::vector<int> indices);
    void clear();

    // LI-04 — reordenacao. Move o item de `from` para a posicao `to`.
    void move(int from, int to);

    // LI-04 com LI-05 — move um CONJUNTO de itens para antes da linha
    // `before`, preservando a ordem relativa entre eles.
    //
    // `before` e uma posicao ENTRE linhas, e nao um indice de item: vai de 0
    // (antes de tudo) a size() (depois de tudo). E assim que um arraste se
    // descreve — o usuario solta entre duas linhas, nao em cima de uma.
    //
    // Mover varios nao e repetir o move de um: cada movimento desloca os
    // indices dos seguintes, e aplicar em sequencia embaralha a selecao. Aqui
    // os indices sao todos lidos ANTES de qualquer mudanca.
    //
    // Devolve o indice onde o PRIMEIRO item movido foi parar — os demais vem
    // em seguida, contiguos. -1 quando nada foi movido. Quem mostra a selecao
    // precisa disso, e recalcular do lado de fora seria uma segunda rota para
    // a mesma conta, que e o que mais diverge neste projeto.
    int move(std::vector<int> indices, int before);

    // LI-08 — ordenacao estavel; itens sem o campo vao para o fim.
    void sort(SortKey key, bool ascending);

    // LI-09 — busca textual em titulo, artista, album e caminho.
    std::vector<int> find(std::string_view query) const;

    // LI-11 — soma apenas as duracoes conhecidas. Also_unknown recebe quantos
    // itens ficaram de fora, para a interface poder dizer "ou mais".
    std::int64_t known_duration_ms(int* unknown_count = nullptr) const;

    // Aplica o resultado da leitura assincrona de tags.
    // Ignora silenciosamente um id que ja nao existe (AR-03).
    void apply_metadata(std::uint64_t id, const Track& metadata);

    // Identidade estavel. Ordenar, remover e reordenar mudam os indices, mas
    // nao o id — e por isso que quem aponta para a faixa em reproducao guarda
    // o id, nao a posicao. Evita todo um mecanismo de remapeamento de indices.
    int index_of(std::uint64_t id) const;

private:
    std::vector<Track> tracks_;
    std::uint64_t next_id_ = 1;
};

// Extensoes reconhecidas na varredura de diretorio.
bool is_supported_audio(std::string_view path);

}  // namespace pang::core
