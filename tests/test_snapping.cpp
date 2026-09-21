// AP-08, AP-09 — encaixe magnetico e movimento em grupo.
//
// Roda sem janela, sem tela e sem compositor: o que se verifica aqui e a
// GEOMETRIA, que e onde a regra erra na pratica. Um teste que precisasse de
// janelas de topo de verdade nao rodaria no Wayland — justamente a plataforma
// onde o recurso nao existe — nem numa maquina de integracao sem tela.

#include <QRect>
#include <QVector>

#include <cstdio>

#include "core/util/check.h"
#include "ui/shell/snapping.h"

using pang::ui::shell::group_of;
using pang::ui::shell::kSnapThreshold;
using pang::ui::shell::snap_offset;
using pang::ui::shell::touching;

namespace {

// Geometrias na escala 2x, que e a padrao do player.
const QRect kMain(100, 100, 550, 232);

void encaixa_por_baixo() {
    // Painel logo abaixo do principal, faltando 4 px para encostar.
    const QRect abaixo(100, kMain.bottom() + 1 + 4, 550, 232);
    const QPoint offset = snap_offset(abaixo, {kMain});

    PANG_CHECK(offset.y() == -4, "sobe os 4 px que faltavam para encostar");
    PANG_CHECK(offset.x() == 0, "ja alinhado a esquerda, nao desloca na horizontal");

    const QRect depois = abaixo.translated(offset);
    PANG_CHECK(depois.top() == kMain.bottom() + 1,
               "encostado e bottom+1 == top, sem fresta e sem sobreposicao");
    PANG_CHECK(touching(kMain, depois), "e o resultado e reconhecido como encostado");
}

void encaixa_por_cima() {
    const QRect acima(100, kMain.top() - 232 - 3, 550, 232);
    const QPoint offset = snap_offset(acima, {kMain});
    PANG_CHECK(offset.y() == 3, "desce os 3 px para encostar por cima");
    PANG_CHECK(touching(kMain, acima.translated(offset)), "fica encostado");
}

void alinha_a_lateral_junto_com_o_empilhamento() {
    // Desalinhado 5 px na horizontal E 6 px na vertical: os dois eixos
    // corrigem na mesma operacao.
    const QRect solto(105, kMain.bottom() + 1 + 6, 550, 232);
    const QPoint offset = snap_offset(solto, {kMain});
    PANG_CHECK(offset.x() == -5, "alinha a borda esquerda");
    PANG_CHECK(offset.y() == -6, "e encosta por baixo, na mesma chamada");
}

void ignora_o_que_esta_longe() {
    const QRect longe(100, kMain.bottom() + 1 + kSnapThreshold + 1, 550, 232);
    PANG_CHECK(snap_offset(longe, {kMain}) == QPoint(0, 0),
               "um pixel alem do limiar nao atrai");

    const QRect no_limite(100, kMain.bottom() + 1 + kSnapThreshold, 550, 232);
    PANG_CHECK(snap_offset(no_limite, {kMain}).y() == -kSnapThreshold,
               "exatamente no limiar ainda atrai");
}

// O caso que revela a diferenca entre "perto" e "perto NAQUELE eixo".
void nao_atrai_janela_que_nao_se_cruza() {
    // Mesma altura do principal, mas do outro lado da tela: a distancia
    // vertical entre as bordas e zero, e mesmo assim nao ha o que encaixar.
    const QRect ao_lado(2000, kMain.top() + 2, 550, 232);
    const QPoint offset = snap_offset(ao_lado, {kMain});
    PANG_CHECK(offset.y() == 0,
               "sem cruzamento horizontal, bordas horizontais nao se atraem");
}

void escolhe_o_encaixe_mais_proximo() {
    // Duas candidatas para o mesmo eixo: vale a de menor deslocamento.
    const QRect outra(100, kMain.bottom() + 1 + 240, 550, 232);
    const QRect entre(100, kMain.bottom() + 1 + 2, 550, 232);
    const QPoint offset = snap_offset(entre, {kMain, outra});
    PANG_CHECK(offset.y() == -2, "encaixa no vizinho mais proximo, nao no primeiro da lista");
}

void encostado_exige_lado_em_comum() {
    const QRect a(0, 0, 100, 100);
    const QRect diagonal(100, 100, 100, 100);  // toca apenas no canto
    PANG_CHECK(!touching(a, diagonal), "tocar so no canto nao e estar encostado");

    const QRect abaixo(0, 100, 100, 100);
    PANG_CHECK(touching(a, abaixo), "compartilhar um lado e");

    const QRect sobreposto(0, 50, 100, 100);
    PANG_CHECK(!touching(a, sobreposto), "sobrepor nao e encostar");
}

// AP-09 — o caso que separa "vizinho direto" de "conjunto".
void o_grupo_e_transitivo() {
    const QRect principal(0, 0, 550, 232);
    const QRect equalizador(0, 232, 550, 232);
    const QRect playlist(0, 464, 550, 464);
    const QRect solta(2000, 2000, 550, 232);

    const QVector<QRect> janelas{principal, equalizador, playlist, solta};
    const QVector<int> grupo = group_of(0, janelas);

    PANG_CHECK(grupo.size() == 3,
               "arrastar o principal leva o equalizador E a playlist colada nele");
    PANG_CHECK(grupo.contains(0) && grupo.contains(1) && grupo.contains(2),
               "os tres empilhados formam um conjunto");
    PANG_CHECK(!grupo.contains(3), "a janela afastada fica onde esta");
}

void grupo_de_janela_isolada_e_ela_mesma() {
    const QVector<QRect> janelas{QRect(0, 0, 10, 10), QRect(500, 500, 10, 10)};
    const QVector<int> grupo = group_of(1, janelas);
    PANG_CHECK(grupo.size() == 1 && grupo[0] == 1,
               "sem vizinhos, o grupo e a propria janela");
}

void indice_invalido_nao_quebra() {
    const QVector<QRect> janelas{QRect(0, 0, 10, 10)};
    PANG_CHECK(group_of(7, janelas).isEmpty(), "indice fora da lista devolve vazio");
    PANG_CHECK(group_of(-1, janelas).isEmpty(), "indice negativo devolve vazio");
}

// AP-18 — de que plataformas se pode esperar encaixe.
void a_plataforma_decide_o_encaixe() {
    using pang::ui::shell::snapping_available;

    PANG_CHECK(!snapping_available(QStringLiteral("wayland")),
               "Wayland nao deixa o cliente posicionar a propria janela");

    // O caso que um teste ingenuo erraria: sob XWayland o backend e xcb, e o
    // posicionamento funciona. Decidir por XDG_SESSION_TYPE daria "wayland" e
    // desabilitaria o recurso sem motivo.
    PANG_CHECK(snapping_available(QStringLiteral("xcb")),
               "X11 permite, inclusive quando a sessao e Wayland com XWayland");

    PANG_CHECK(snapping_available(QStringLiteral("windows")), "Windows permite");
    PANG_CHECK(snapping_available(QStringLiteral("cocoa")), "macOS permite");
}

#define RUN(f)                       \
    do {                             \
        std::printf("-> %s\n", #f);  \
        f();                         \
    } while (0)

}  // namespace

int main() {
    RUN(encaixa_por_baixo);
    RUN(encaixa_por_cima);
    RUN(alinha_a_lateral_junto_com_o_empilhamento);
    RUN(ignora_o_que_esta_longe);
    RUN(nao_atrai_janela_que_nao_se_cruza);
    RUN(escolhe_o_encaixe_mais_proximo);
    RUN(encostado_exige_lado_em_comum);
    RUN(o_grupo_e_transitivo);
    RUN(grupo_de_janela_isolada_e_ela_mesma);
    RUN(indice_invalido_nao_quebra);
    RUN(a_plataforma_decide_o_encaixe);
    return pang::check::exit_code();
}
