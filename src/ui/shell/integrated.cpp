#include "ui/shell/integrated.h"

#include <QKeyEvent>
#include <QShortcut>

namespace pang::ui::shell {

IntegratedShell::IntegratedShell(MainPanel* main, EqualizerPanel* equalizer,
                                 PlaylistPanel* playlist, skin::Atlas& atlas, QWidget* parent)
    : QWidget(parent), main_(main), equalizer_(equalizer), playlist_(playlist), atlas_(atlas) {
    main_->setParent(this);
    equalizer_->setParent(this);
    playlist_->setParent(this);

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

void IntegratedShell::relayout() {
    const int s = atlas_.scale();
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
