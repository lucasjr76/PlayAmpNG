#pragma once

#include <QWidget>

#include <chrono>
#include <functional>
#include <set>

#include "core/state/controller.h"
#include "ui/skin/winamp_skin.h"

namespace pang::ui {

// Painel de playlist. Largura fixa de 275 px como os outros; a altura cresce.
//
// LI-17 — a lista e virtualizada por construcao: o desenho percorre apenas as
// linhas visiveis, entao dez mil itens custam o mesmo que dez.
class PlaylistPanel : public QWidget {
public:
    static constexpr int kWidth = 275;
    static constexpr int kMinimumHeight = 116;

    PlaylistPanel(core::Controller& controller, skin::WinampSkin& skin, QWidget* parent = nullptr);

    void set_scale(int scale);
    void set_logical_height(int height);
    int logical_height() const { return logical_height_; }

    // A lista mudou por fora: reajusta rolagem e selecao.
    void refresh();

    std::function<void()> on_add_files;
    std::function<void()> on_add_directory;
    std::function<void()> on_import;
    std::function<void()> on_export;
    std::function<void(const QStringList&)> on_drop;

    // A altura mudou por arraste da aresta inferior; o shell reempilha.
    std::function<void()> on_height_changed;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    QPoint to_logical(const QPoint& physical) const;
    int visible_rows() const;
    int row_at(const QPoint& logical) const;
    QRect button_rect(int index) const;
    int footer_top() const;
    void clamp_scroll();

    core::Controller& controller_;
    skin::WinampSkin& skin_;

    int logical_height_ = kMinimumHeight;
    int scroll_ = 0;
    int anchor_ = -1;
    bool resizing_ = false;
    int resize_origin_ = 0;
    int resize_start_height_ = 0;
    std::set<int> selected_;

    // LI-09 — busca por digitacao; o texto expira depois de um segundo parado.
    QString search_;
    std::chrono::steady_clock::time_point last_keystroke_{};
};

}  // namespace pang::ui
