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
#include <QImage>
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

    // --------------------------------------------- aparencia da selecao
    {
        controller.play_index(3);
        panel.refresh();

        QImage imagem(panel.size(), QImage::Format_ARGB32);
        imagem.fill(Qt::transparent);
        panel.render(&imagem);

        const QRect barra = panel.scrollbar();
        // A barra de rolagem e CROMO DE JANELA: mora na borda direita. Recuada
        // pela margem da lista, ela flutuava no meio do preto e parecia solta
        // do que rola.
        PANG_CHECK(barra.x() + barra.width() == pang::ui::PlaylistPanel::kWidth,
                   "a barra de rolagem encosta na borda direita da janela");

        // A linha selecionada nao pode ter PRETO dentro dela. O text.bmp do
        // formato tem fundo preto; desenhado por cima do azul da selecao, cada
        // palavra levava junto uma caixa preta e a linha ficava listrada.
        const int linha = 14 + (3 - panel.scroll_position()) * 8;
        int pretos = 0, azuis = 0;
        for (int y = linha + 1; y < linha + 7; ++y)
            for (int x = 2; x < barra.x() - 2; ++x) {
                const QRgb c = imagem.pixel(x, y);
                if (qRed(c) < 12 && qGreen(c) < 12 && qBlue(c) < 12) ++pretos;
                if (qBlue(c) > 60 && qBlue(c) > qRed(c) + 30) ++azuis;
            }
        std::printf("  linha selecionada: %d px azuis, %d px pretos\n", azuis, pretos);
        PANG_CHECK(azuis > 200, "a linha selecionada tem fundo azul");
        PANG_CHECK(pretos == 0, "a linha selecionada nao tem caixa preta atras do texto");
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

    // ------------------------------------------- LI-04: arrastar para reordenar
    //
    // Pelo painel, com eventos de mouse, e nao chamando Playlist::move: o que
    // se verifica aqui e a INTERACAO — limiar de arraste, selecao preservada
    // durante o gesto, destino entre linhas. A conta em si ja tem teste no
    // core.
    {
        panel.set_scroll_position(0);
        const auto path_at = [&](int i) { return controller.playlist().at(i).path; };
        const auto y_of = [](int row) { return 14 + row * 8 + 4; };  // meio da linha
        const int x = 40;

        const std::string item1 = path_at(1), item2 = path_at(2), item3 = path_at(3);
        const std::string item6 = path_at(6);

        // Seleciona as linhas 1, 2 e 3.
        click(&panel, QPoint(x, y_of(1)));
        click(&panel, QPoint(x, y_of(1)), QEvent::MouseButtonRelease);
        {
            QMouseEvent shift(QEvent::MouseButtonPress, QPointF(x, y_of(3)),
                              QPointF(panel.mapToGlobal(QPoint(x, y_of(3)))), Qt::LeftButton,
                              Qt::LeftButton, Qt::ShiftModifier);
            QCoreApplication::sendEvent(&panel, &shift);
            click(&panel, QPoint(x, y_of(3)), QEvent::MouseButtonRelease);
        }
        PANG_CHECK(panel.selection() == std::vector<int>({1, 2, 3}), "tres linhas selecionadas");

        // Pega a linha 2 — que ja esta selecionada — e arrasta para antes da 7.
        click(&panel, QPoint(x, y_of(2)));
        PANG_CHECK(panel.selection() == std::vector<int>({1, 2, 3}),
                   "pegar uma linha ja selecionada NAO desfaz a selecao multipla");

        click(&panel, QPoint(x, y_of(7) - 4), QEvent::MouseMove);  // fronteira 6|7
        click(&panel, QPoint(x, y_of(7) - 4), QEvent::MouseButtonRelease);

        PANG_CHECK(path_at(3) == item6, "o que estava depois sobe para abrir espaco");
        PANG_CHECK(path_at(4) == item1 && path_at(5) == item2 && path_at(6) == item3,
                   "as tres faixas foram juntas, na ordem em que estavam");
        PANG_CHECK(panel.selection() == std::vector<int>({4, 5, 6}),
                   "a selecao acompanha as faixas para a nova posicao");
    }

    // ---------------------------- LI-04: clique sem arraste reduz a selecao
    {
        panel.set_scroll_position(0);
        const auto y_of = [](int row) { return 14 + row * 8 + 4; };
        // A selecao 4..6 do bloco anterior continua valendo.
        click(&panel, QPoint(40, y_of(5)));
        click(&panel, QPoint(40, y_of(5)), QEvent::MouseButtonRelease);
        PANG_CHECK(panel.selection() == std::vector<int>({5}),
                   "clicar e soltar sem arrastar numa linha selecionada seleciona so ela");
    }

    // ------------------------------------ LI-04: tremida nao e arraste
    //
    // A primeira versao deste bloco tremia 1 px no meio da linha e passava
    // com ou sem limiar: o destino caia na fronteira da propria faixa, e
    // mover para onde ja se esta nao muda nada. Um teste que nao reprova
    // quando a regra some nao testa a regra. A tremida agora parte do FUNDO
    // da linha e anda ate um pixel antes do limiar — o bastante para
    // atravessar uma fronteira, e portanto para reordenar se o limiar faltar.
    {
        panel.set_scroll_position(0);
        const int limiar = QApplication::startDragDistance();
        const int fundo_da_linha_2 = 14 + 2 * 8 + 7;
        const int tremida = fundo_da_linha_2 + limiar - 1;
        const int fronteira_3_4 = 14 + 4 * 8 - 4;  // a partir daqui arredonda para 4
        PANG_CHECK(tremida >= fronteira_3_4,
                   "a tremida atravessa uma fronteira; senao este bloco nao testaria nada");

        const std::string antes = controller.playlist().at(2).path;
        click(&panel, QPoint(40, fundo_da_linha_2));
        click(&panel, QPoint(40, tremida), QEvent::MouseMove);
        click(&panel, QPoint(40, tremida), QEvent::MouseButtonRelease);
        PANG_CHECK(controller.playlist().at(2).path == antes,
                   "tremer abaixo do limiar de arraste nao reordena nada");
    }

    return pang::check::exit_code();
}
