#include "ui/panel/playlist_panel.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <chrono>

namespace pang::ui {
namespace {

constexpr int kTitlebarHeight = 14;
constexpr int kRowHeight = 9;
constexpr int kListMargin = 6;
constexpr int kFooterHeight = 24;
constexpr int kButtonWidth = 28;
constexpr int kButtonHeight = 13;
constexpr int kButtonSpacing = 30;

// Faixa de arraste da aresta inferior. Quatro pixels logicos viram oito ou doze
// na escala util, que e alvo suficiente.
constexpr int kResizeGrip = 4;

struct Button {
    const char* label;
    const char* tooltip;
};

constexpr Button kButtons[] = {
    {"ARQ", "adicionar arquivos"}, {"DIR", "adicionar pasta"}, {"REM", "remover selecionados"},
    {"LMP", "limpar lista"},       {"IMP", "importar"},        {"EXP", "exportar"},
};
constexpr int kButtonCount = 6;

QString format_ms(std::int64_t ms) {
    if (ms < 0) return QStringLiteral("--:--");
    const std::int64_t total = ms / 1000;
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

// Total da lista pode passar de uma hora, e "620:25" nao se le. Acima de 60
// minutos o campo ganha o digito de hora.
QString format_total_ms(std::int64_t ms) {
    if (ms < 0) return QStringLiteral("--:--");
    const std::int64_t total = ms / 1000;
    if (total < 3600) return format_ms(ms);
    return QStringLiteral("%1:%2:%3")
        .arg(total / 3600)
        .arg((total % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

PlaylistPanel::PlaylistPanel(core::Controller& controller, skin::Atlas& atlas, QWidget* parent)
    : QWidget(parent), controller_(controller), atlas_(atlas) {
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);  // LI-02
    setMouseTracking(true);
    // Fundo opaco: sem isso o widget pode deixar passar o que estiver atras.
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    set_scale(atlas.scale());
}

void PlaylistPanel::set_scale(int) {
    setFixedSize(kWidth * atlas_.scale(), logical_height_ * atlas_.scale());
    update();
}

void PlaylistPanel::set_logical_height(int height) {
    logical_height_ = std::max(kMinimumHeight, height);
    setFixedSize(kWidth * atlas_.scale(), logical_height_ * atlas_.scale());
    clamp_scroll();
    update();
}

void PlaylistPanel::refresh() {
    // Indices selecionados que nao existem mais saem da selecao.
    std::set<int> kept;
    for (int index : selected_)
        if (index >= 0 && index < controller_.playlist().size()) kept.insert(index);
    selected_.swap(kept);
    clamp_scroll();
    update();
}

QPoint PlaylistPanel::to_logical(const QPoint& physical) const {
    const int s = std::max(1, atlas_.scale());
    return QPoint(physical.x() / s, physical.y() / s);
}

int PlaylistPanel::visible_rows() const {
    const int area = logical_height_ - kTitlebarHeight - kFooterHeight - kListMargin;
    return std::max(1, area / kRowHeight);
}

void PlaylistPanel::clamp_scroll() {
    const int maximum = std::max(0, controller_.playlist().size() - visible_rows());
    scroll_ = std::clamp(scroll_, 0, maximum);
}

int PlaylistPanel::row_at(const QPoint& p) const {
    const int top = kTitlebarHeight + 4;
    if (p.y() < top) return -1;
    const int row = scroll_ + (p.y() - top) / kRowHeight;
    if (row < 0 || row >= controller_.playlist().size()) return -1;
    if (p.y() >= top + visible_rows() * kRowHeight) return -1;
    return row;
}

QRect PlaylistPanel::button_rect(int index) const {
    return QRect(kListMargin + index * kButtonSpacing, logical_height_ - kButtonHeight - 4,
                 kButtonWidth, kButtonHeight);
}

void PlaylistPanel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const int s = atlas_.scale();

    painter.fillRect(rect(), atlas_.color(QStringLiteral("background")));
    atlas_.draw_tiled(painter, QStringLiteral("frame/titlebar"), QRect(0, 0, kWidth, kTitlebarHeight));
    const QString caption = QStringLiteral("PLAYLIST");
    atlas_.draw_text(painter, caption, (kWidth - atlas_.text_width(caption)) / 2, 4);

    // Poco da lista.
    const int top = kTitlebarHeight + 4;
    const int rows = visible_rows();
    const QRect well(kListMargin - 2, top - 2, kWidth - 2 * kListMargin + 4,
                     rows * kRowHeight + 4);
    painter.fillRect(well.x() * s, well.y() * s, well.width() * s, well.height() * s,
                     atlas_.color(QStringLiteral("well")));

    const core::Playlist& playlist = controller_.playlist();
    const int current = controller_.current_index();
    const QColor green = atlas_.color(QStringLiteral("green"));
    const QColor dim = atlas_.color(QStringLiteral("gray_text"));

    // LI-17 — so as linhas visiveis sao tocadas.
    for (int row = 0; row < rows; ++row) {
        const int index = scroll_ + row;
        if (index >= playlist.size()) break;

        const int y = top + row * kRowHeight;

        // LI-10 — selecionada tem fundo; em reproducao tem cor propria. Sao
        // dois estados distintos, e podem coincidir.
        if (selected_.count(index))
            painter.fillRect((kListMargin - 1) * s, y * s, (kWidth - 2 * kListMargin + 2) * s,
                             (kRowHeight - 1) * s, atlas_.color(QStringLiteral("bevel_dark")));

        const core::Track& track = playlist.at(index);
        const QString duration = format_ms(track.duration_ms);
        const QString number = QStringLiteral("%1.").arg(index + 1);

        atlas_.draw_text(painter, number, kListMargin, y,
                         index == current ? green : dim);

        // Reserva a direita para a duracao, mais a folga da barra de rolagem,
        // e corta o titulo no que REALMENTE sobrar. A conta anterior dividia
        // pela largura do glifo e ainda deixava o titulo encostar na duracao.
        const int duration_width = atlas_.text_width(duration);
        const int title_x = kListMargin + atlas_.text_width(number) + 4;
        const int reserved = kListMargin + duration_width + 8;
        const int available = std::max(0, kWidth - reserved - title_x);

        QString title = QString::fromStdString(track.display_title());
        if (atlas_.text_width(title) > available) {
            const int per_glyph = atlas_.glyph_width() + 1;
            const int fits = std::max(0, (available + 1) / per_glyph - 1);
            title = title.left(fits) + QStringLiteral(".");
        }

        atlas_.draw_text(painter, title, title_x, y, index == current ? green : dim);
        atlas_.draw_text(painter, duration, kWidth - kListMargin - duration_width, y,
                         index == current ? green : dim);
    }

    // Barra de rolagem: so aparece quando ha o que rolar.
    const int maximum = std::max(0, playlist.size() - rows);
    if (maximum > 0) {
        const int track_x = kWidth - kListMargin + 1;
        painter.fillRect(track_x * s, top * s, 3 * s, rows * kRowHeight * s,
                         atlas_.color(QStringLiteral("bevel_dark")));
        const int thumb_height = std::max(6, rows * kRowHeight * rows / playlist.size());
        const int thumb_y = top + (rows * kRowHeight - thumb_height) * scroll_ / maximum;
        painter.fillRect(track_x * s, thumb_y * s, 3 * s, thumb_height * s, green);
    }

    // Rodape: botoes e totais.
    for (int i = 0; i < kButtonCount; ++i) {
        const QRect area = button_rect(i);
        atlas_.draw(painter, QStringLiteral("toggle/blank/normal"), area.x(), area.y());
        const QString label = QLatin1String(kButtons[i].label);
        atlas_.draw_text(painter, label, area.x() + (kButtonWidth - atlas_.text_width(label)) / 2,
                         area.y() + 3);
    }

    int unknown = 0;
    const std::int64_t known = playlist.known_duration_ms(&unknown);
    // LI-11 — o "+" avisa que ha duracao desconhecida fora da soma.
    const QString totals = QStringLiteral("%1 %2%3")
                               .arg(playlist.size())
                               .arg(format_total_ms(known))
                               .arg(unknown > 0 ? QStringLiteral("+") : QString());
    atlas_.draw_text(painter, totals, kWidth - kListMargin - atlas_.text_width(totals),
                     logical_height_ - kButtonHeight - 1);
}

void PlaylistPanel::mousePressEvent(QMouseEvent* event) {
    const QPoint p = to_logical(event->pos());

    // Aresta inferior: arrastar redimensiona a lista. Sem isso nao ha como ver
    // mais que as linhas que couberam na altura inicial.
    if (p.y() >= logical_height_ - kResizeGrip) {
        resizing_ = true;
        resize_origin_ = event->globalPosition().toPoint().y();
        resize_start_height_ = logical_height_;
        return;
    }

    for (int i = 0; i < kButtonCount; ++i) {
        if (!button_rect(i).contains(p)) continue;
        switch (i) {
            case 0: if (on_add_files) on_add_files(); break;
            case 1: if (on_add_directory) on_add_directory(); break;
            case 2: {  // LI-06 — remove da lista, nao do disco
                std::vector<int> rows(selected_.begin(), selected_.end());
                if (!rows.empty()) {
                    controller_.playlist().remove(rows);
                    controller_.playlist_changed();
                    selected_.clear();
                    refresh();
                }
                break;
            }
            case 3:
                controller_.playlist().clear();
                controller_.playlist_changed();
                selected_.clear();
                refresh();
                break;
            case 4: if (on_import) on_import(); break;
            case 5: if (on_export) on_export(); break;
            default: break;
        }
        return;
    }

    const int row = row_at(p);
    if (row < 0) {
        if (p.y() < kTitlebarHeight && window()->windowHandle())
            window()->windowHandle()->startSystemMove();
        return;
    }

    // LI-05 — selecao multipla com os modificadores de sempre.
    if (event->modifiers() & Qt::ControlModifier) {
        if (selected_.count(row))
            selected_.erase(row);
        else
            selected_.insert(row);
        anchor_ = row;
    } else if ((event->modifiers() & Qt::ShiftModifier) && anchor_ >= 0) {
        selected_.clear();
        for (int i = std::min(anchor_, row); i <= std::max(anchor_, row); ++i) selected_.insert(i);
    } else {
        selected_.clear();
        selected_.insert(row);
        anchor_ = row;
    }
    update();
}

void PlaylistPanel::mouseMoveEvent(QMouseEvent* event) {
    if (!resizing_) {
        setCursor(to_logical(event->pos()).y() >= logical_height_ - kResizeGrip
                      ? Qt::SizeVerCursor
                      : Qt::ArrowCursor);
        return;
    }
    const int delta = (event->globalPosition().toPoint().y() - resize_origin_) /
                      std::max(1, atlas_.scale());
    set_logical_height(resize_start_height_ + delta);
    if (on_height_changed) on_height_changed();
}

void PlaylistPanel::mouseReleaseEvent(QMouseEvent*) { resizing_ = false; }

void PlaylistPanel::mouseDoubleClickEvent(QMouseEvent* event) {
    const int row = row_at(to_logical(event->pos()));
    if (row >= 0) controller_.play_index(row);
}

void PlaylistPanel::wheelEvent(QWheelEvent* event) {
    scroll_ -= event->angleDelta().y() / 40;
    clamp_scroll();
    update();
}

void PlaylistPanel::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Delete: {
            std::vector<int> rows(selected_.begin(), selected_.end());
            if (rows.empty()) break;
            controller_.playlist().remove(rows);
            controller_.playlist_changed();
            selected_.clear();
            refresh();
            break;
        }
        case Qt::Key_Up:       scroll_ = std::max(0, scroll_ - 1); clamp_scroll(); break;
        case Qt::Key_Down:     ++scroll_; clamp_scroll(); break;
        case Qt::Key_PageUp:   scroll_ -= visible_rows(); clamp_scroll(); break;
        case Qt::Key_PageDown: scroll_ += visible_rows(); clamp_scroll(); break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (!selected_.empty()) controller_.play_index(*selected_.begin());
            break;
        default: {
            // LI-09 — busca textual por digitacao. Nao ha caixa de texto: em
            // 275 px ela custaria uma linha inteira, e digitar direto na lista
            // e o gesto que o formato compacto pede.
            const QString text = event->text();
            if ((event->modifiers() & Qt::ControlModifier) || text.isEmpty() ||
                !text.at(0).isPrint()) {
                QWidget::keyPressEvent(event);
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - last_keystroke_ > std::chrono::seconds(1)) search_.clear();
            last_keystroke_ = now;
            search_ += text;

            const auto hits = controller_.playlist().find(search_.toStdString());
            if (hits.empty()) break;
            const int row = hits.front();
            selected_.clear();
            selected_.insert(row);
            anchor_ = row;
            // Centraliza o resultado, em vez de deixar na borda.
            scroll_ = row - visible_rows() / 2;
            clamp_scroll();
            break;
        }
    }
    update();
}

void PlaylistPanel::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void PlaylistPanel::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
        if (url.isLocalFile()) paths << url.toLocalFile();
    if (!paths.isEmpty() && on_drop) on_drop(paths);
    event->acceptProposedAction();
}

}  // namespace pang::ui
