// AP-08, AP-09 — o arraste com encaixe, com janelas de verdade.
//
// O test_snapping verifica a geometria; este verifica a LIGACAO: que arrastar
// a faixa de titulo move a janela, que o encaixe e aplicado ao soltar perto de
// outra, e que o conjunto encostado viaja junto. Sao coisas diferentes, e a
// segunda ja quebrou uma vez sem a primeira acusar nada.
//
// Roda em offscreen. O backend offscreen permite posicionar janelas, que e o
// que o recurso exige — no Wayland o teste nao teria o que exercitar, e e por
// isso que a plataforma e consultada em tempo de execucao.

#include <QApplication>
#include <QMouseEvent>

#include <cstdio>
#include <vector>

#include "core/audio/engine.h"
#include "core/state/controller.h"
#include "core/util/check.h"
#include "ui/panel/equalizer_panel.h"
#include "ui/panel/main_panel.h"
#include "ui/panel/playlist_panel.h"
#include "ui/shell/integrated.h"
#include "ui/shell/snapping.h"

using namespace pang;

namespace {

// Entrega o evento como o Qt entregaria: pelo filtro instalado na aplicacao.
void move_mouse_to(const QPoint& global) {
    QMouseEvent event(QEvent::MouseMove, QPointF(0, 0), QPointF(global), Qt::NoButton,
                      Qt::LeftButton, Qt::NoModifier);
    qApp->sendEvent(qApp, &event);
}

void release_mouse_at(const QPoint& global) {
    QMouseEvent event(QEvent::MouseButtonRelease, QPointF(0, 0), QPointF(global),
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    qApp->sendEvent(qApp, &event);
}

#define RUN(f)                      \
    do {                            \
        std::printf("-> %s\n", #f); \
        f();                        \
    } while (0)

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    PANG_CHECK(ui::shell::platform_can_position_windows(),
               "o backend deste teste permite posicionar janelas");

    ui::skin::WinampSkin skin;
    QString skin_error;
    PANG_CHECK(skin.load(QStringLiteral(PLAYAMPNG_SKIN_DIR "/default"), skin_error),
               qPrintable(QStringLiteral("skin carrega: %1").arg(skin_error)));
    skin.set_scale(1);

    core::Engine engine(44100, 2);
    core::Controller controller(engine);
    std::vector<core::dsp::EqPreset> user_presets;

    auto* main_panel = new ui::MainPanel(controller, engine, skin);
    auto* eq_panel = new ui::EqualizerPanel(engine.equalizer(), user_presets, skin);
    auto* pl_panel = new ui::PlaylistPanel(controller, skin);
    ui::shell::IntegratedShell shell(main_panel, eq_panel, pl_panel, skin);

    shell.set_equalizer_visible(true);
    shell.set_playlist_visible(true);
    shell.show();
    shell.set_detached(true);
    PANG_CHECK(shell.detached(), "entrou no modo destacado");

    // Posicoes conhecidas: o equalizador longe de todos, para que o unico
    // movimento observado seja o do arraste.
    shell.move(100, 100);
    eq_panel->move(1000, 1000);
    pl_panel->move(2000, 2000);
    app.processEvents();

    // ---------------------------------------------------- arraste simples
    {
        const QPoint antes = eq_panel->pos();
        const QPoint press(1010, 1005);  // dentro da faixa de titulo
        PANG_CHECK(eq_panel->on_title_drag && eq_panel->on_title_drag(press),
                   "o shell assume o arraste da faixa de titulo");

        move_mouse_to(press + QPoint(40, 30));
        PANG_CHECK(eq_panel->pos() == antes + QPoint(40, 30),
                   "a janela acompanha o ponteiro");
        release_mouse_at(press + QPoint(40, 30));

        // Depois de soltar, mover o ponteiro nao pode mais arrastar nada.
        //
        // Esta verificacao pega o encerramento AUSENTE, e nao cada uma das
        // duas linhas que o fazem: limpar o ponteiro e desinstalar o filtro
        // sao redundantes entre si, e tirar so uma nao muda nada observavel.
        // Registrado porque um teste que parece cobrir mais do que cobre e
        // pior que um teste que falta.
        const QPoint parado = eq_panel->pos();
        move_mouse_to(press + QPoint(400, 300));
        PANG_CHECK(eq_panel->pos() == parado, "soltar encerra o arraste");
    }

    // ------------------------------------------------------------ encaixe
    {
        // Leva o equalizador para 6 px abaixo da base do painel principal:
        // dentro do limiar, e portanto deve grudar.
        const QRect principal = shell.frameGeometry();
        eq_panel->move(principal.left() + 3, principal.bottom() + 1 + 6);
        app.processEvents();

        PANG_CHECK(principal.width() == eq_panel->frameGeometry().width(),
                   "as janelas tem a mesma largura: nenhuma ganhou moldura do sistema");
        const QPoint press = eq_panel->pos() + QPoint(10, 5);
        PANG_CHECK(eq_panel->on_title_drag(press), "arraste iniciado");
        move_mouse_to(press);  // sem deslocar: so deixa o encaixe agir
        release_mouse_at(press);

        PANG_CHECK(eq_panel->pos().y() == principal.bottom() + 1,
                   "AP-08: encaixou na base do painel principal");
        PANG_CHECK(eq_panel->pos().x() == principal.left(),
                   "AP-08: e alinhou a borda esquerda");
    }

    // -------------------------------------------------- movimento em grupo
    {
        // Playlist encostada no equalizador, que esta encostado no principal.
        pl_panel->move(eq_panel->pos().x(), eq_panel->frameGeometry().bottom() + 1);
        app.processEvents();

        const QPoint eq_antes = eq_panel->pos();
        const QPoint pl_antes = pl_panel->pos();

        const QPoint press = shell.pos() + QPoint(10, 5);
        PANG_CHECK(main_panel->on_title_drag && main_panel->on_title_drag(press),
                   "arraste do painel principal iniciado");
        move_mouse_to(press + QPoint(100, 50));

        PANG_CHECK(eq_panel->pos() == eq_antes + QPoint(100, 50),
                   "AP-09: o equalizador colado veio junto");
        PANG_CHECK(pl_panel->pos() == pl_antes + QPoint(100, 50),
                   "AP-09: e a playlist colada NELE tambem, por transitividade");
        release_mouse_at(press + QPoint(100, 50));
    }

    // ------------------------------------------------------- desencaixar
    //
    // O defeito que este bloco guarda foi relatado em uso real: as janelas
    // grudavam e nao havia como separa-las. A causa era formar o grupo a
    // partir de qualquer janela arrastada — puxar a secundaria trazia a
    // principal junto, e o conjunto so crescia.
    {
        // Equalizador encostado na base do principal.
        const QRect principal = shell.frameGeometry();
        eq_panel->move(principal.left(), principal.bottom() + 1);
        pl_panel->move(4000, 4000);
        app.processEvents();

        const QPoint principal_antes = shell.pos();
        const QPoint press = eq_panel->pos() + QPoint(10, 5);
        PANG_CHECK(eq_panel->on_title_drag(press), "arraste da secundaria iniciado");

        // Bem alem do limiar, senao o ima a traria de volta — e isso seria
        // correto, e nao o defeito que se procura aqui.
        const QPoint longe(500, 400);
        move_mouse_to(press + longe);

        PANG_CHECK(shell.pos() == principal_antes,
                   "AP-09: arrastar a secundaria NAO leva o painel principal junto");
        PANG_CHECK(!pang::ui::shell::touching(shell.frameGeometry(),
                                              eq_panel->frameGeometry()),
                   "e as duas ficam de fato separadas");
        release_mouse_at(press + longe);
    }

    // ------------------------------------- janela solta nao viaja com o grupo
    {
        pl_panel->move(3000, 3000);
        app.processEvents();
        const QPoint solta = pl_panel->pos();

        const QPoint press = shell.pos() + QPoint(10, 5);
        PANG_CHECK(main_panel->on_title_drag(press), "arraste iniciado");
        move_mouse_to(press + QPoint(70, 0));
        PANG_CHECK(pl_panel->pos() == solta, "a janela afastada fica onde estava");
        release_mouse_at(press + QPoint(70, 0));
    }

    return pang::check::exit_code();
}
