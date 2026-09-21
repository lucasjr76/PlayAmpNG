#include "ui/shell/integrated.h"

#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QShortcut>

#include "core/util/log.h"
#include "ui/shell/snapping.h"

namespace pang::ui::shell {

IntegratedShell::IntegratedShell(MainPanel* main, EqualizerPanel* equalizer,
                                 PlaylistPanel* playlist, skin::WinampSkin& skin, QWidget* parent)
    : QWidget(parent), main_(main), equalizer_(equalizer), playlist_(playlist), skin_(skin) {
    // A janela e sem decoracao: o skin desenha a propria faixa de titulo, e uma
    // barra do sistema em cima dela seria uma segunda.
    //
    // Fica AQUI, e nao em quem constroi o shell, porque ja divergiu: o
    // app.cpp aplicava os flags e um teste montava o mesmo shell sem eles. A
    // janela do teste ganhava 4 px de moldura, e o encaixe alinhava pela borda
    // errada por um pixel de diferenca. Duas rotas para a mesma configuracao
    // sempre divergem; esta passa a ser uma.
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    main_->setParent(this);
    equalizer_->setParent(this);
    playlist_->setParent(this);

    for (QWidget* panel : {static_cast<QWidget*>(main_), static_cast<QWidget*>(equalizer_),
                           static_cast<QWidget*>(playlist_)}) {
        auto assume = [this, panel](const QPoint& global) {
            return begin_title_drag(panel, global);
        };
        if (panel == static_cast<QWidget*>(main_)) main_->on_title_drag = assume;
        else if (panel == static_cast<QWidget*>(equalizer_)) equalizer_->on_title_drag = assume;
        else playlist_->on_title_drag = assume;
    }

    // Atalhos de aplicacao, e nao tratamento de tecla no widget.
    //
    // A primeira versao tratava as teclas no painel principal, e elas so
    // funcionavam se ELE estivesse com o foco — com a playlist focada, nada
    // acontecia. Qt::ApplicationShortcut dispara independentemente de qual
    // painel esta focado, que e o comportamento que um atalho global deve ter.
    const auto shortcut = [this](const QKeySequence& keys, std::function<void()> action) {
        auto* item = new QShortcut(keys, this);
        item->setContext(Qt::ApplicationShortcut);
        QObject::connect(item, &QShortcut::activated, this, std::move(action));
    };

    shortcut(QKeySequence(QStringLiteral("Ctrl+1")), [this] { set_scale(1); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+2")), [this] { set_scale(2); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+3")), [this] { set_scale(3); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+W")), [this] { set_compact(!compact_); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+D")), [this] { set_detached(!detached_); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+E")),
             [this] { set_equalizer_visible(!equalizer_visible_); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+P")),
             [this] { set_playlist_visible(!playlist_visible_); });
    shortcut(QKeySequence(QStringLiteral("Ctrl+T")),
             [this] { set_always_on_top(!always_on_top_); });

    relayout();
}

void IntegratedShell::set_equalizer_visible(bool visible) {
    equalizer_visible_ = visible;
    relayout();
}

void IntegratedShell::set_playlist_visible(bool visible) {
    playlist_visible_ = visible;
    relayout();
}

void IntegratedShell::set_always_on_top(bool on) {
    if (always_on_top_ == on) return;
    always_on_top_ = on;
    // setWindowFlag reabre a janela no X11, entao o estado de visibilidade
    // precisa ser restaurado na sequencia, senao o player some da tela ao
    // alternar a opcao.
    const bool was_visible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, on);
    if (was_visible) show();
}

void IntegratedShell::set_compact(bool compact) {
    compact_ = compact;
    main_->set_compact(compact);
    relayout();
}

void IntegratedShell::set_scale(int scale) {
    main_->set_scale(scale);
    equalizer_->set_scale(scale);
    playlist_->set_scale(scale);
    relayout();
}

void IntegratedShell::set_detached(bool detached) {
    if (detached == detached_) return;

    // AP-18 — sem poder posicionar as janelas, destacar nao produz o modo
    // destacado: produz tres janelas espalhadas onde o compositor quiser, que
    // nao encaixam e nao se movem juntas. Recusar aqui, e nao so no menu, e o
    // que faz o atalho Ctrl+D concordar com o que a interface mostra — senao a
    // tecla faria o que o menu declara indisponivel.
    if (detached && !platform_can_position_windows()) {
        core::log::info("modo destacado indisponivel: " +
                        snapping_unavailable_reason().toStdString());
        return;
    }
    detached_ = detached;

    const Qt::WindowFlags flags = Qt::Dialog | Qt::FramelessWindowHint;
    for (QWidget* panel : {static_cast<QWidget*>(equalizer_), static_cast<QWidget*>(playlist_)}) {
        if (detached_) {
            panel->setParent(nullptr);
            panel->setWindowFlags(flags);
        } else {
            panel->setWindowFlags(Qt::Widget);
            panel->setParent(this);
        }
    }
    relayout();
}

QVector<QWidget*> IntegratedShell::detached_windows() {
    // A janela do painel principal e o PROPRIO shell: no modo destacado ele
    // encolhe ate o tamanho do painel em vez de virar uma quarta janela vazia.
    QVector<QWidget*> windows{this};
    if (equalizer_->isVisible()) windows.append(equalizer_);
    if (playlist_->isVisible()) windows.append(playlist_);
    return windows;
}

bool IntegratedShell::begin_title_drag(QWidget* panel, const QPoint& global) {
    // No modo integrado a janela e uma so, e o arraste normal do sistema serve.
    // Sem poder posicionar, nao ha o que fazer aqui.
    if (!detached_ || !platform_can_position_windows()) return false;

    QWidget* window = panel == static_cast<QWidget*>(main_) ? this : panel;

    const QVector<QWidget*> windows = detached_windows();
    QVector<QRect> geometries;
    for (QWidget* w : windows) geometries.append(w->frameGeometry());

    const int index = windows.indexOf(window);
    if (index < 0) return false;

    // AP-09 — o conjunto e ASSIMETRICO, e tem de ser.
    //
    // So o painel principal arrasta quem esta encostado nele. Arrastar uma
    // janela secundaria move apenas ela, e e assim que se DESPRENDE uma do
    // grupo. A primeira versao formava o grupo a partir de qualquer janela, e
    // o resultado era que nada nunca se separava: puxar a playlist trazia o
    // player inteiro atras. Magnetico para sempre nao e magnetico, e cola.
    //
    // E tambem o gesto classico do Winamp, que e a referencia deste projeto.
    const QVector<int> group =
        window == this ? group_of(index, geometries) : QVector<int>{index};

    dragging_ = window;
    drag_origin_ = global;
    drag_group_.clear();
    drag_group_start_.clear();
    drag_others_.clear();
    for (int i = 0; i < windows.size(); ++i) {
        if (group.contains(i)) {
            drag_group_.append(windows[i]);
            drag_group_start_.append(geometries[i]);
        } else {
            drag_others_.append(geometries[i]);
        }
    }

    // Filtro na aplicacao inteira, e nao no painel: o ponteiro sai da area do
    // painel no primeiro movimento rapido, e um filtro local perderia o
    // arraste no meio.
    qApp->installEventFilter(this);
    return true;
}

bool IntegratedShell::eventFilter(QObject* watched, QEvent* event) {
    if (!dragging_) return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseMove) {
        // A posicao vem do EVENTO, e nao de QCursor::pos(): consultar o cursor
        // le o estado do sistema no instante da leitura, que nao e o instante
        // do evento, e torna o arraste impossivel de verificar sem um mouse de
        // verdade. Com a posicao do evento, um teste headless exercita o
        // encaixe inteiro.
        const QPoint delta = static_cast<QMouseEvent*>(event)->globalPosition().toPoint() -
                             drag_origin_;

        // O encaixe e calculado para a janela ARRASTADA e aplicado a todo o
        // grupo. Calcular por janela faria cada uma encaixar num vizinho
        // diferente, e o conjunto se esticaria em vez de se mover.
        const int moved = drag_group_.indexOf(dragging_);
        const QRect proposed = drag_group_start_[moved].translated(delta);
        const QPoint adjust = snap_offset(proposed, drag_others_);

        for (int i = 0; i < drag_group_.size(); ++i)
            drag_group_[i]->move(drag_group_start_[i].topLeft() + delta + adjust);
        return false;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        dragging_ = nullptr;
        qApp->removeEventFilter(this);
        if (on_layout_changed) on_layout_changed();
        return false;
    }

    return QWidget::eventFilter(watched, event);
}

void IntegratedShell::relayout() {
    const int s = skin_.scale();
    int y = 0;

    const int main_height = compact_ ? MainPanel::kCompactHeight : MainPanel::kHeight;
    main_->move(0, y * s);
    main_->show();
    y += main_height;

    // No modo compacto os outros paineis somem junto: a barra e a barra.
    const bool show_equalizer = equalizer_visible_ && !compact_;
    const bool show_playlist = playlist_visible_ && !compact_;

    if (detached_) {
        // Destacado: a janela do shell encolhe para o painel principal, e os
        // outros dois viram janelas proprias logo abaixo dele.
        setFixedSize(MainPanel::kWidth * s, main_height * s);
        int below = pos().y() + main_height * s;
        for (auto* panel : {static_cast<QWidget*>(equalizer_), static_cast<QWidget*>(playlist_)}) {
            const bool visible =
                panel == static_cast<QWidget*>(equalizer_) ? show_equalizer : show_playlist;
            panel->setVisible(visible);
            if (!visible) continue;
            panel->move(pos().x(), below);
            below += panel->height();
        }
        if (on_layout_changed) on_layout_changed();
        return;
    }

    equalizer_->setVisible(show_equalizer);
    if (show_equalizer) {
        equalizer_->move(0, y * s);
        y += EqualizerPanel::kHeight;
    }

    playlist_->setVisible(show_playlist);
    if (show_playlist) {
        playlist_->move(0, y * s);
        y += playlist_->logical_height();
    }

    setFixedSize(MainPanel::kWidth * s, y * s);
    if (on_layout_changed) on_layout_changed();
}

}  // namespace pang::ui::shell
