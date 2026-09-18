// Trava as relacoes de alinhamento do painel principal.
//
// Sem isto, cada ajuste de posicao soltava outro elemento: a versao que este
// teste substitui tinha 21 bordas esquerdas distintas e 21 direitas, ou seja,
// nenhum elemento compartilhava aresta com outro. O que se ve na tela como
// "nada alinha com nada" e exatamente isso, e e verificavel.

#include <QRect>

#include <initializer_list>
#include <set>

#include "core/util/check.h"
#include "ui/panel/main_layout.h"

using namespace pang::ui::layout;

namespace {

int left(const QRect& r) { return r.x(); }
int right(const QRect& r) { return r.x() + r.width() - 1; }
int top(const QRect& r) { return r.y(); }
int bottom(const QRect& r) { return r.y() + r.height() - 1; }

void same_left(std::initializer_list<QRect> group, const char* what) {
    std::set<int> edges;
    for (const QRect& r : group) edges.insert(left(r));
    PANG_CHECK(edges.size() == 1, what);
}

void same_right(std::initializer_list<QRect> group, const char* what) {
    std::set<int> edges;
    for (const QRect& r : group) edges.insert(right(r));
    PANG_CHECK(edges.size() == 1, what);
}

void same_band(std::initializer_list<QRect> group, const char* what) {
    std::set<int> tops, bottoms;
    for (const QRect& r : group) {
        tops.insert(top(r));
        bottoms.insert(bottom(r));
    }
    PANG_CHECK(tops.size() == 1 && bottoms.size() == 1, what);
}

}  // namespace

int main() {
    // Tempo e visualizacao vivem DENTRO do mesmo bloco preto, e nao em dois
    // pocos separados: e isso que da a leitura de mostrador unico.
    PANG_CHECK(kDisplayBlock.contains(kTime), "o mostrador de tempo fica dentro do bloco");
    PANG_CHECK(kDisplayBlock.contains(kVis), "a visualizacao fica dentro do bloco");
    PANG_CHECK(kVis.y() > bottom(kTime), "a visualizacao fica abaixo do tempo, sem sobrepor");

    // Coluna da direita.
    same_left({kTitleWell, kVolume}, "coluna direita: titulo e volume com a mesma borda esquerda");

    // Borda direita util, compartilhada pelo que encosta nela.
    same_right({kTitleWell, kPlaylist, kPosition, kRepeat},
               "borda direita compartilhada por titulo, PL, barra de posicao e repeat");

    // Margem esquerda, compartilhada pelo que comeca nela.
    same_left({kDisplayBlock, kPosition, kPrevious},
              "margem esquerda compartilhada pelos elementos que comecam nela");

    // Linhas da grade.
    PANG_CHECK(kDisplayBlock.y() == kTitleWell.y(),
               "bloco da esquerda e poco do titulo comecam na mesma linha");
    same_band({kVolume, kBalance, kEqualizer, kPlaylist},
              "linha 2: volume, balanco, EQ e PL ocupam a mesma faixa");
    same_band({kPrevious, kPlay, kPause, kStop, kNext},
              "linha 5: os botoes de transporte ocupam a mesma faixa");

    // Transporte encostado: sem vao entre botoes vizinhos.
    PANG_CHECK(kPlay.x() == right(kPrevious) + 1, "play encosta em anterior");
    PANG_CHECK(kPause.x() == right(kPlay) + 1, "pause encosta em play");
    PANG_CHECK(kStop.x() == right(kPause) + 1, "stop encosta em pause");
    PANG_CHECK(kNext.x() == right(kStop) + 1, "proxima encosta em stop");

    // A area util da visualizacao acomoda 19 barras de 3 px com 1 px de
    // intervalo, sem sobra a distribuir.
    PANG_CHECK(kVis.width() == 19 * 4, "area util da visualizacao cabe 19 barras de passo 4");
    PANG_CHECK(kVis.x() == kDisplayBlock.x() + 1 && right(kVis) <= right(kDisplayBlock) - 1,
               "area util da visualizacao respeita a borda do bloco");

    // Nada ultrapassa o painel.
    for (const QRect& r : {kDisplayBlock, kTitleWell, kVolume, kBalance, kEqualizer,
                           kPlaylist, kPosition, kPrevious, kNext, kEject, kShuffle, kRepeat,
                           kClose})
        PANG_CHECK(r.x() >= 0 && right(r) < 275 && r.y() >= 0 && bottom(r) < 116,
                   "elemento dentro dos limites do painel");

    // Nenhuma sobreposicao entre elementos da mesma linha.
    const QRect row2[] = {kVolume, kBalance, kEqualizer, kPlaylist};
    for (std::size_t i = 0; i + 1 < std::size(row2); ++i)
        PANG_CHECK(row2[i + 1].x() > right(row2[i]), "linha 2 sem sobreposicao entre vizinhos");

    return pang::check::exit_code();
}
