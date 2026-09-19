// Interacao do painel de playlist: o que o usuario faz com o mouse.
//
// Os dois casos cobertos aqui foram reportados em uso e nenhum teste os pegava,
// porque ambos so existem na interacao — a logica por tras deles estava certa:
//
//   1. A faixa que comeca a tocar nao ficava selecionada nem era trazida para
//      a vista. Trocar de faixa pelo botao ou pelo fim da musica deixava a
//      lista mostrando outra parte.
//   2. A barra de rolagem nao respondia a clique nem a arrasto; so a roda do
//      mouse rolava. Ela era desenhada e nao era area sensivel.

#include <QApplication>
#include <QMouseEvent>

#include <cstdio>
#include <string>

#include "core/state/controller.h"
#include "core/util/check.h"
#include "ui/panel/playlist_panel.h"
#include "ui/skin/winamp_skin.h"

namespace {

void click(QWidget* w, QPoint at, QEvent::Type type = QEvent::MouseButtonPress) {
    QMouseEvent event(type, at, w->mapToGlobal(at), Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(w, &event);
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    pang::ui::skin::WinampSkin skin;
    QString skin_error;
    PANG_CHECK(skin.load(QStringLiteral(PLAYAMPNG_SKIN_DIR "/default"), skin_error),
               qPrintable(QStringLiteral("skin carrega: %1").arg(skin_error)));
    skin.set_scale(1);

    pang::core::Engine engine(44100, 2);
    pang::core::Controller controller(engine);
    for (int i = 0; i < 60; ++i)
        controller.playlist().add("/musica/faixa" + std::to_string(i) + ".mp3");
    controller.playlist_changed();

    pang::ui::PlaylistPanel panel(controller, skin);
    panel.set_logical_height(116);
    const int rows = panel.rows_visible();
    std::printf("  %d linhas visiveis de %d faixas\n", rows, controller.playlist().size());
    PANG_CHECK(rows > 2 && rows < 60, "a lista mostra parte das faixas, nao todas");

    // ---------------------------------------------------- selecao acompanha
    {
        controller.play_index(0);
        panel.refresh();
        PANG_CHECK(panel.selection() == std::vector<int>{0}, "a primeira faixa fica selecionada");

        // Faixa bem adiante: a lista tem de rolar para mostra-la.
        const int alvo = 45;
        controller.play_index(alvo);
        panel.refresh();
        PANG_CHECK(panel.selection() == std::vector<int>{alvo},
                   "a faixa que passou a tocar fica selecionada");
        PANG_CHECK(alvo >= panel.scroll_position() && alvo < panel.scroll_position() + rows,
                   "a faixa que passou a tocar fica visivel");

        // Selecao do usuario nao pode ser desfeita por um refresh que nao
        // trocou de faixa: o tique da interface chama refresh() dez vezes por
        // segundo, e a selecao dele nao pode durar um decimo de segundo.
        click(&panel, QPoint(40, 14 + 2 * 8 + 1));
        const auto escolhida = panel.selection();
        PANG_CHECK(escolhida.size() == 1, "clique seleciona uma linha");
        panel.refresh();
        panel.refresh();
        PANG_CHECK(panel.selection() == escolhida,
                   "refresh sem troca de faixa preserva a selecao do usuario");
    }

    // ------------------------------------------------ arrasto da rolagem
    {
        controller.play_index(0);
        panel.refresh();
        PANG_CHECK(panel.scroll_position() == 0, "comeca no topo");

        const QRect barra = panel.scrollbar();
        const QRect cursor = panel.scroll_thumb();
        std::printf("  barra %d..%d, cursor %d..%d\n", barra.y(), barra.bottom(), cursor.y(),
                    cursor.bottom());
        PANG_CHECK(!cursor.isNull(), "ha cursor de rolagem quando a lista nao cabe");
        PANG_CHECK(barra.contains(cursor), "o cursor fica dentro da barra");

        // Agarra o cursor e arrasta ate o fim da barra.
        click(&panel, QPoint(barra.center().x(), cursor.center().y()));
        click(&panel, QPoint(barra.center().x(), barra.bottom() + 20), QEvent::MouseMove);
        const int maximo = controller.playlist().size() - rows;
        PANG_CHECK(panel.scroll_position() == maximo,
                   "arrastar o cursor ate o fim rola ate o fim da lista");

        click(&panel, QPoint(barra.center().x(), barra.bottom()), QEvent::MouseButtonRelease);

        // Clique direto no trilho salta para aquele ponto.
        click(&panel, QPoint(barra.center().x(), barra.y()));
        PANG_CHECK(panel.scroll_position() == 0, "clicar no topo do trilho volta ao inicio");
        click(&panel, QPoint(barra.center().x(), barra.y()), QEvent::MouseButtonRelease);

        // Clicar na barra nao pode selecionar a linha que estiver atras dela.
        const auto antes = panel.selection();
        click(&panel, QPoint(barra.center().x(), barra.center().y()));
        PANG_CHECK(panel.selection() == antes, "clicar na barra nao mexe na selecao");
        click(&panel, QPoint(barra.center().x(), barra.center().y()),
              QEvent::MouseButtonRelease);
    }

    return pang::check::exit_code();
}
