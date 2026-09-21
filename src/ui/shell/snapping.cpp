#include "ui/shell/snapping.h"

#include <cstdlib>

namespace pang::ui::shell {
namespace {

// Em QRect, right() e bottom() sao a ULTIMA coluna e a ULTIMA linha ocupadas,
// e nao a primeira livre. Encostar a base de A no topo de B e, portanto,
// A.bottom() + 1 == B.top(). Confundir isso deixa um pixel de fresta ou uma
// linha de sobreposicao — defeito que so aparece quando se olha de perto, e
// que o teste pega olhando o numero.
struct Candidate {
    int offset = 0;
    int distance = 0;
    bool found = false;
};

void consider(Candidate& best, int from, int to, int threshold) {
    const int delta = to - from;
    const int distance = std::abs(delta);
    if (distance > threshold) return;
    if (best.found && distance >= best.distance) return;
    best = {delta, distance, true};
}

// Duas faixas [a1,a2] e [b1,b2] se sobrepoem em ao menos um pixel.
bool overlaps(int a1, int a2, int b1, int b2) { return a1 <= b2 && b1 <= a2; }

}  // namespace

QPoint snap_offset(const QRect& moving, const QVector<QRect>& fixed, int threshold) {
    Candidate x, y;

    for (const QRect& f : fixed) {
        // As faixas sao INFLADAS pelo limiar antes da comparacao.
        //
        // A primeira versao exigia sobreposicao estrita no outro eixo, e o
        // teste mostrou o erro: duas janelas empilhadas nao se sobrepoem na
        // vertical, e era exatamente nelas que o alinhamento lateral tinha de
        // agir. Com a inflacao, uma janela a menos de um limiar de distancia
        // conta como vizinha — que e o que "esta perto de encaixar" significa.
        //
        // Sem nenhuma condicao, uma janela no outro canto da tela puxaria a
        // que esta sendo arrastada, e o usuario leria isso como a janela
        // fugindo do mouse.
        if (overlaps(moving.left() - threshold, moving.right() + threshold, f.left(),
                     f.right())) {
            consider(y, moving.top(), f.bottom() + 1, threshold);     // por baixo
            consider(y, moving.bottom() + 1, f.top(), threshold);     // por cima
            consider(y, moving.top(), f.top(), threshold);            // topos alinhados
            consider(y, moving.bottom(), f.bottom(), threshold);      // bases alinhadas
        }

        if (overlaps(moving.top() - threshold, moving.bottom() + threshold, f.top(),
                     f.bottom())) {
            consider(x, moving.left(), f.right() + 1, threshold);     // a direita
            consider(x, moving.right() + 1, f.left(), threshold);     // a esquerda
            consider(x, moving.left(), f.left(), threshold);          // esquerdas alinhadas
            consider(x, moving.right(), f.right(), threshold);        // direitas alinhadas
        }
    }

    return {x.found ? x.offset : 0, y.found ? y.offset : 0};
}

bool touching(const QRect& a, const QRect& b) {
    const bool vertical = (a.bottom() + 1 == b.top() || b.bottom() + 1 == a.top()) &&
                          overlaps(a.left(), a.right(), b.left(), b.right());
    const bool horizontal = (a.right() + 1 == b.left() || b.right() + 1 == a.left()) &&
                            overlaps(a.top(), a.bottom(), b.top(), b.bottom());
    return vertical || horizontal;
}

QVector<int> group_of(int origin, const QVector<QRect>& windows) {
    QVector<int> group;
    if (origin < 0 || origin >= windows.size()) return group;

    QVector<bool> seen(windows.size(), false);
    QVector<int> pending{origin};
    seen[origin] = true;

    while (!pending.isEmpty()) {
        const int current = pending.takeLast();
        group.append(current);
        for (int i = 0; i < windows.size(); ++i) {
            if (seen[i]) continue;
            if (!touching(windows[current], windows[i])) continue;
            seen[i] = true;
            pending.append(i);
        }
    }
    return group;
}

}  // namespace pang::ui::shell
