// AP-14 — regra de recuperacao de janela fora da area visivel.
//
// Vive fora de core/ porque usa QRect, entao e um binario de teste proprio,
// ligado so a QtCore. Sem janela, sem dispositivo, roda headless.

#include <QRect>
#include <QVector>

#include "core/util/check.h"
#include "ui/shell/recovery.h"

using pang::ui::shell::is_reachable;
using pang::ui::shell::recover;

int main() {
    const QRect screen(0, 0, 1920, 1080);
    const QVector<QRect> screens{screen};

    PANG_CHECK(is_reachable(QRect(100, 100, 275, 116), screens),
               "janela inteiramente na tela e alcancavel");

    // O caso que a regra antiga, de 64x64 px, reprovaria por engano.
    PANG_CHECK(is_reachable(QRect(100, 100, 275, 14), screens),
               "AP-14: barra compacta de 275x14 px e alcancavel, e nao 'fora da tela'");

    PANG_CHECK(!is_reachable(QRect(-400, 100, 275, 116), screens),
               "janela deslocada para fora pela esquerda nao e alcancavel");
    PANG_CHECK(!is_reachable(QRect(100, 1200, 275, 116), screens),
               "janela abaixo da tela nao e alcancavel");

    // So o corpo visivel nao basta: se a faixa de arraste ficou acima do topo,
    // nao ha por onde pegar a janela.
    PANG_CHECK(!is_reachable(QRect(100, -40, 275, 116), screens),
               "AP-14: corpo visivel com a faixa de arraste fora do topo nao conta");

    // Largura minima: 40 px de faixa visivel ainda e pouco para pegar.
    PANG_CHECK(!is_reachable(QRect(1880, 100, 275, 116), screens),
               "menos de 64 px de faixa visivel nao conta como alcancavel");
    PANG_CHECK(is_reachable(QRect(1800, 100, 275, 116), screens),
               "120 px de faixa visivel bastam");

    // Segundo monitor, e o caso de ele desaparecer.
    const QVector<QRect> dual{screen, QRect(1920, 0, 1920, 1080)};
    PANG_CHECK(is_reachable(QRect(2200, 300, 275, 116), dual),
               "janela no segundo monitor e alcancavel enquanto ele existe");
    PANG_CHECK(!is_reachable(QRect(2200, 300, 275, 116), screens),
               "o mesmo retangulo deixa de ser alcancavel quando o monitor some");

    const QRect rescued = recover(QRect(2200, 300, 275, 116), screens, screen);
    PANG_CHECK(is_reachable(rescued, screens), "a janela resgatada volta a ser alcancavel");
    PANG_CHECK(rescued.size() == QSize(275, 116), "o resgate preserva o tamanho");

    const QRect untouched = recover(QRect(100, 100, 275, 116), screens, screen);
    PANG_CHECK(untouched == QRect(100, 100, 275, 116),
               "janela ja alcancavel nao e movida");

    return pang::check::exit_code();
}
