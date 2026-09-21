// AP-11 — modo compacto: cada mini-controle dispara a acao certa.
//
// O test_layout confere a geometria. Uma tabela de areas com dois botoes
// trocados passaria em toda verificacao de geometria e ainda assim tocaria
// quando se pede pausa. Este teste clica.
//
// Guarda tambem o defeito que a primeira versao do compacto tinha: o tempo
// aparecia "0010", sem os dois-pontos.

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QThread>

#include <chrono>
#include <cstdio>
#include <string>

#include "core/audio/engine.h"
#include "core/state/controller.h"
#include "core/util/check.h"
#include "ui/panel/main_panel.h"
#include "ui/skin/winamp_layout.h"
#include "ui/skin/winamp_skin.h"

namespace wa = pang::ui::skin::winamp;
using pang::core::State;

namespace {

void click(QWidget* panel, const QRect& area) {
    const QPointF at = area.center();
    QMouseEvent press(QEvent::MouseButtonPress, at, panel->mapToGlobal(at), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, at, panel->mapToGlobal(at), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(panel, &press);
    QCoreApplication::sendEvent(panel, &release);
}

// Espera o EVENTO, com prazo, em vez de dormir um tempo fixo.
bool wait_for(const pang::core::Engine& engine, State wanted) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (engine.snapshot().state == wanted) return true;
        QThread::msleep(5);
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    pang::ui::skin::WinampSkin skin;
    QString skin_error;
    PANG_CHECK(skin.load(QStringLiteral(PLAYAMPNG_SKIN_DIR "/default"), skin_error),
               qPrintable(QStringLiteral("skin carrega: %1").arg(skin_error)));
    skin.set_scale(1);

    pang::core::Engine engine(44100, 2);
    pang::core::Controller controller(engine);
    for (int i = 0; i < 3; ++i)
        controller.playlist().add(std::string(PANG_TEST_ASSETS) + "/tone.wav");
    controller.playlist_changed();

    pang::ui::MainPanel panel(controller, engine, skin);
    bool opened = false, toggled = false;
    panel.on_open = [&] { opened = true; };
    panel.on_toggle_compact = [&] { toggled = true; };
    panel.set_compact(true);

    controller.play_index(1);
    PANG_CHECK(wait_for(engine, State::Playing), "a faixa comeca a tocar");

    // ---------------------------------------------------------- transporte
    click(&panel, wa::kCompactPause);
    PANG_CHECK(engine.snapshot().state == State::Paused, "o mini-controle de pausa pausa");

    click(&panel, wa::kCompactPlay);
    PANG_CHECK(wait_for(engine, State::Playing), "o de tocar retoma");

    click(&panel, wa::kCompactNext);
    PANG_CHECK(controller.current_index() == 2, "o de proxima avanca uma faixa");

    click(&panel, wa::kCompactPrevious);
    PANG_CHECK(controller.current_index() == 1, "o de anterior volta uma faixa");

    click(&panel, wa::kCompactStop);
    PANG_CHECK(engine.snapshot().state == State::Stopped, "o de parar para");

    click(&panel, wa::kCompactEject);
    PANG_CHECK(opened, "o de ejetar abre o dialogo de arquivos");

    // --------------------------------------------------- botoes da janela
    click(&panel, QRect(wa::kCompactExpandAt, QSize(9, 9)));
    PANG_CHECK(toggled, "o botao de expandir sai do modo compacto");

    // Onde nao ha controle, clicar nao dispara nada.
    opened = toggled = false;
    click(&panel, wa::kCompactTitle);
    PANG_CHECK(!opened && !toggled, "clicar no titulo nao aciona controle nenhum");

    // ------------------------------------------ tempo com dois-pontos
    //
    // Conta a tinta em duas celulas do tempo. Uma verificacao so na celula do
    // ":" seria vacua: com "0010" ali cai o digito "1", que tambem tem tinta.
    // O que separa os dois casos: os dois-pontos tem tinta de sobra pouca, e
    // "MM:SS" tem cinco caracteres — a QUINTA celula precisa ter um digito,
    // e com "0010" ela fica vazia.
    QImage image(panel.size(), QImage::Format_RGB32);
    panel.render(&image);
    const auto ink_in_cell = [&](int cell) {
        const int x0 = wa::kCompactTimeAt.x() + cell * wa::kGlyphWidth;
        int ink = 0;
        for (int y = wa::kCompactTimeAt.y(); y < wa::kCompactTimeAt.y() + 6; ++y)
            for (int x = x0; x < x0 + wa::kGlyphWidth; ++x)
                if (qGreen(image.pixel(x, y)) > 120) ++ink;
        return ink;
    };
    std::printf("  tinta nas celulas do tempo: %d %d %d %d %d\n", ink_in_cell(0), ink_in_cell(1),
                ink_in_cell(2), ink_in_cell(3), ink_in_cell(4));
    PANG_CHECK(ink_in_cell(2) >= 1 && ink_in_cell(2) <= 3,
               "a terceira celula tem os dois-pontos, e nao um digito");
    PANG_CHECK(ink_in_cell(4) >= 4, "o tempo tem cinco caracteres: MM:SS");

    return pang::check::exit_code();
}
