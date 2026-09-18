#include "ui/shell/integrated.h"

namespace pang::ui::shell {

IntegratedShell::IntegratedShell(MainPanel* main, EqualizerPanel* equalizer,
                                 PlaylistPanel* playlist, skin::Atlas& atlas, QWidget* parent)
    : QWidget(parent), main_(main), equalizer_(equalizer), playlist_(playlist), atlas_(atlas) {
    main_->setParent(this);
    equalizer_->setParent(this);
    playlist_->setParent(this);
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
}

}  // namespace pang::ui::shell
