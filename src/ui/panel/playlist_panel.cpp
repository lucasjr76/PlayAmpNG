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

#include "ui/skin/winamp_layout.h"

namespace pang::ui {
namespace {

namespace wa = skin::winamp;

// Mesma altura das outras duas janelas. Com 20 a legenda da playlist nao
// batia com a da principal nem com a do equalizador, e as listras cabiam em
// numero diferente.
constexpr int kTitlebarHeight = 14;
constexpr int kRowHeight = wa::kGlyphHeight + 2;   // fonte do formato mais respiro
// A mesma margem das outras duas janelas: conteudo de 14 a 260. O valor vem
// da grade em tests/test_layout.cpp.
constexpr int kListMargin = 14;
constexpr int kFooterHeight = 24;
constexpr int kButtonWidth = 26;
constexpr int kButtonHeight = 12;
constexpr int kButtonSpacing = 28;
constexpr int kResizeGrip = 4;
constexpr int kScrollbarWidth = 6;
constexpr int kScrollThumbMinimum = 8;

// Cores do editor de playlist. O formato as guarda em pledit.txt; sem esse
// arquivo valem estes padroes. Sao os mesmos da paleta medida na referencia,
// para a playlist nao destoar das outras duas janelas.
const QColor kBackground{0, 0, 0};
const QColor kFace{58, 58, 86};
const QColor kFaceLight{78, 78, 108};
const QColor kFaceShade{45, 45, 68};
const QColor kBevelLight{108, 108, 142};
const QColor kBevelDark{26, 26, 40};
const QColor kCream{252, 251, 233};
// A familia dos botoes de transporte: face clara, contorno escuro, tinta
// escura. Sao os mesmos valores de tools/make_wsz.py.
const QColor kFaceTop{214, 222, 230};
const QColor kFaceBottom{166, 174, 190};
const QColor kFaceHighlight{239, 248, 250};
const QColor kFaceShadow{120, 128, 146};
const QColor kOutline{16, 16, 26};
const QColor kButtonInk{26, 30, 46};
const QColor kCurrentText{252, 251, 233};
const QColor kSelectedRow{38, 38, 92};

constexpr const char* kButtons[] = {"add", "dir", "rem", "clr", "imp", "exp"};
constexpr int kButtonCount = 6;

QString format_ms(std::int64_t ms) {
    if (ms < 0) return QStringLiteral("--:--");
    const std::int64_t total = ms / 1000;
    return QStringLiteral("%1:%2")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

// Acima de uma hora, "620:25" nao se le: o campo ganha o digito de hora.
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

PlaylistPanel::PlaylistPanel(core::Controller& controller, skin::WinampSkin& skin, QWidget* parent)
    : QWidget(parent), controller_(controller), skin_(skin) {
    setFocusPolicy(Qt::StrongFocus);
    // IN-03 — o painel nao tem widget por controle, entao o que o leitor de
    // tela anuncia e a janela. O detalhe de cada controle vem do tooltip, que
    // segue a posicao do cursor.
    setAccessibleName(tr("Playlist"));
    setAccessibleDescription(tr("Lista de faixas com duracao e total"));
    setAcceptDrops(true);  // LI-02
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    set_scale(skin.scale());
}

void PlaylistPanel::set_scale(int) {
    setFixedSize(kWidth * skin_.scale(), logical_height_ * skin_.scale());
    update();
}

void PlaylistPanel::set_logical_height(int height) {
    logical_height_ = std::max(kMinimumHeight, height);
    setFixedSize(kWidth * skin_.scale(), logical_height_ * skin_.scale());
    clamp_scroll();
    update();
}

void PlaylistPanel::refresh(bool force) {
    std::set<int> kept;
    for (int index : selected_)
        if (index >= 0 && index < controller_.playlist().size()) kept.insert(index);
    selected_.swap(kept);

    // A faixa que passou a tocar vira a selecao e e trazida para a vista.
    //
    // So quando ela MUDA: refazer isso a cada refresh desfaria a selecao que o
    // usuario acabou de fazer com o mouse, e prenderia a rolagem na faixa
    // atual. A troca pode vir do botao, do fim da faixa ou do barramento — em
    // qualquer caso o que o usuario ve tem de acompanhar o que ele ouve.
    const int current = controller_.current_index();
    if (current != last_current_) {
        last_current_ = current;
        if (current >= 0 && current < controller_.playlist().size()) {
            selected_.clear();
            selected_.insert(current);
            anchor_ = current;
            ensure_visible(current);
        }
    }

    clamp_scroll();

    // Repinta so quando ha o que ver de diferente. O tique da interface chama
    // isto dez vezes por segundo, e repintar a lista inteira a cada vez seria
    // trabalho jogado fora na maior parte delas.
    std::size_t signature = static_cast<std::size_t>(controller_.playlist().size());
    signature = signature * 1000003 + static_cast<std::size_t>(current + 1);
    signature = signature * 1000003 + static_cast<std::size_t>(scroll_);
    signature = signature * 1000003 + selected_.size();
    for (int index : selected_) signature = signature * 31 + static_cast<std::size_t>(index);
    if (force || signature != last_signature_) {
        last_signature_ = signature;
        update();
    }
}

// Rola o minimo necessario para a linha aparecer. Centrar sempre faria a lista
// saltar a cada troca mesmo quando a proxima faixa ja estava a vista.
void PlaylistPanel::ensure_visible(int index) {
    const int rows = visible_rows();
    if (index < scroll_)
        scroll_ = index;
    else if (index >= scroll_ + rows)
        scroll_ = index - rows + 1;
    clamp_scroll();
}

QPoint PlaylistPanel::to_logical(const QPoint& physical) const {
    const int s = std::max(1, skin_.scale());
    return QPoint(physical.x() / s, physical.y() / s);
}

int PlaylistPanel::visible_rows() const {
    const int area = logical_height_ - kTitlebarHeight - kFooterHeight;
    return std::max(1, area / kRowHeight);
}

void PlaylistPanel::clamp_scroll() {
    scroll_ = std::clamp(scroll_, 0, std::max(0, controller_.playlist().size() - visible_rows()));
}

int PlaylistPanel::row_at(const QPoint& p) const {
    const int top = kTitlebarHeight;
    if (p.y() < top) return -1;
    const int row = scroll_ + (p.y() - top) / kRowHeight;
    if (row < 0 || row >= controller_.playlist().size()) return -1;
    if (p.y() >= top + visible_rows() * kRowHeight) return -1;
    return row;
}

// Barra de rolagem: uma conta so, usada pelo desenho E pelo clique. Quando o
// desenho e o teste de acerto calculam a geometria cada um por sua conta, eles
// divergem e o cursor deixa de pegar onde aparece.
QRect PlaylistPanel::scrollbar_rect() const {
    const int top = kTitlebarHeight;
    return QRect(kWidth - kListMargin - kScrollbarWidth, top, kScrollbarWidth,
                 visible_rows() * kRowHeight);
}

QRect PlaylistPanel::scroll_thumb_rect() const {
    const QRect track = scrollbar_rect();
    const int total = controller_.playlist().size();
    const int rows = visible_rows();
    const int maximum = std::max(0, total - rows);
    if (maximum <= 0) return {};
    const int height = std::max(kScrollThumbMinimum, track.height() * rows / std::max(1, total));
    const int y = track.y() + (track.height() - height) * scroll_ / maximum;
    return QRect(track.x(), y, track.width(), height);
}

// Posiciona a rolagem de modo que o cursor fique sob o ponto apontado.
void PlaylistPanel::scroll_to_position(int logical_y) {
    const QRect track = scrollbar_rect();
    const int height = scroll_thumb_rect().height();
    const int span = track.height() - height;
    const int maximum = std::max(0, controller_.playlist().size() - visible_rows());
    if (span <= 0 || maximum <= 0) return;
    const int offset = std::clamp(logical_y - scroll_grab_offset_ - track.y(), 0, span);
    scroll_ = (offset * maximum + span / 2) / span;
    clamp_scroll();
}

// O rodape e uma faixa de kFooterHeight px colada na base; tudo que vive nele
// e centrado NA FAIXA, nao posicionado a partir da borda de baixo. Medido a
// partir da borda, o conteudo ficava mais perto do fim da janela do que do
// fim da lista.
int PlaylistPanel::footer_top() const { return logical_height_ - kFooterHeight; }

QRect PlaylistPanel::button_rect(int index) const {
    return QRect(kListMargin + index * kButtonSpacing,
                 footer_top() + (kFooterHeight - kButtonHeight) / 2, kButtonWidth, kButtonHeight);
}

void PlaylistPanel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const int s = skin_.scale();

    painter.fillRect(rect(), kFace);

    // Barra de titulo igual a das outras duas janelas: degrade da face e TRES
    // listras nas linhas 4, 6 e 8. O laco anterior ia de 4 a 15 de dois em
    // dois e produzia seis listras numa barra de altura diferente — dai as
    // tres legendas nao se parecerem.
    for (int row = 0; row < kTitlebarHeight; ++row) {
        const double t = row / double(kTitlebarHeight - 1);
        painter.fillRect(0, row * s, kWidth * s, s,
                         QColor(int(78 + (45 - 78) * t), int(78 + (45 - 78) * t),
                                int(108 + (68 - 108) * t)));
    }
    for (int row : {4, 6, 8}) {
        painter.fillRect(0, row * s, kWidth * s, s, kCream);
        painter.fillRect(0, (row + 1) * s, kWidth * s, s, kFaceShade);
    }
    const QString caption = QStringLiteral("playamp ng playlist");
    const int caption_x = (kWidth - skin_.text_width(caption)) / 2;
    const int caption_y = (kTitlebarHeight - wa::kGlyphHeight) / 2;
    // Campo preto atras do titulo, pelo mesmo motivo dos botoes.
    painter.fillRect((caption_x - 6) * s, 1 * s, (skin_.text_width(caption) + 12) * s,
                     (kTitlebarHeight - 2) * s, kFace);
    skin_.draw_text(painter, caption, {caption_x, caption_y}, kCream);

    const int top = kTitlebarHeight;
    const int rows = visible_rows();
    // A area da lista vai ate o rodape, nao ate o fim da ultima linha visivel.
    // A altura da janela raramente e multiplo exato da altura de linha, e o
    // resto ficava como uma faixa da cor do painel entre a lista e o rodape —
    // que empurrava o rodape para baixo do centro da faixa dele.
    painter.fillRect(0, top * s, kWidth * s, (footer_top() - top) * s, kBackground);

    const core::Playlist& playlist = controller_.playlist();
    const int current = controller_.current_index();

    // LI-17 — so as linhas visiveis sao tocadas.
    for (int row = 0; row < rows; ++row) {
        const int index = scroll_ + row;
        if (index >= playlist.size()) break;
        const int y = top + row * kRowHeight;

        // LI-10 — selecionada tem fundo proprio; em reproducao tem cor propria.
        // Sao dois estados distintos e podem coincidir.
        if (selected_.count(index))
            painter.fillRect(0, y * s, kWidth * s, kRowHeight * s, kSelectedRow);

        const core::Track& track = playlist.at(index);
        const QString duration = format_ms(track.duration_ms);
        const QString number = QStringLiteral("%1.").arg(index + 1);
        const int duration_width = skin_.text_width(duration);
        const int title_x = kListMargin + skin_.text_width(number) + wa::kGlyphWidth;
        const int available = std::max(0, kWidth - kListMargin - duration_width - 6 - title_x);

        QString title = QString::fromStdString(track.display_title());
        if (skin_.text_width(title) > available) {
            const int fits = std::max(0, available / wa::kGlyphWidth - 1);
            title = title.left(fits) + QStringLiteral(".");
        }

        painter.save();
        // O texto do formato e monocromatico; a cor sai do recorte, entao a
        // distincao entre faixa atual e demais e feita por composicao.
        skin_.draw_text(painter, number, {kListMargin, y + 1});
        skin_.draw_text(painter, title, {title_x, y + 1});
        skin_.draw_text(painter, duration, {kWidth - kListMargin - duration_width, y + 1});
        if (index == current)
            painter.fillRect(0, y * s, 3 * s, kRowHeight * s, kCurrentText);
        painter.restore();
    }

    // Barra de rolagem, so quando ha o que rolar.
    if (const QRect thumb = scroll_thumb_rect(); !thumb.isNull()) {
        const QRect track = scrollbar_rect();
        painter.fillRect(track.x() * s, track.y() * s, track.width() * s, track.height() * s,
                         kFaceShade);
        painter.fillRect(thumb.x() * s, thumb.y() * s, thumb.width() * s, thumb.height() * s,
                         kFaceLight);
        painter.fillRect(thumb.x() * s, thumb.y() * s, thumb.width() * s, s, kBevelLight);
        painter.fillRect(thumb.x() * s, thumb.bottom() * s, thumb.width() * s, s, kBevelDark);
    }

    // Rodape.
    for (int i = 0; i < kButtonCount; ++i) {
        const QRect area = button_rect(i);
        // Mesma peca dos botoes de transporte: contorno escuro, face clara em
        // degrade, realce no topo-esquerda e tinta escura. Com a face da cor
        // do painel eles nao pareciam da mesma familia.
        painter.fillRect(area.x() * s, area.y() * s, area.width() * s, area.height() * s,
                         kOutline);
        for (int row = 1; row < area.height() - 1; ++row) {
            const double t = (row - 1) / double(area.height() - 3);
            painter.fillRect((area.x() + 1) * s, (area.y() + row) * s, (area.width() - 2) * s, s,
                             QColor(int(kFaceTop.red() + (kFaceBottom.red() - kFaceTop.red()) * t),
                                    int(kFaceTop.green() +
                                        (kFaceBottom.green() - kFaceTop.green()) * t),
                                    int(kFaceTop.blue() +
                                        (kFaceBottom.blue() - kFaceTop.blue()) * t)));
        }
        painter.fillRect((area.x() + 1) * s, (area.y() + 1) * s, (area.width() - 2) * s, s,
                         kFaceHighlight);
        painter.fillRect((area.x() + 1) * s, (area.y() + 1) * s, s, (area.height() - 2) * s,
                         kFaceHighlight);
        painter.fillRect((area.x() + 1) * s, (area.bottom() - 1) * s, (area.width() - 2) * s, s,
                         kFaceShadow);
        painter.fillRect((area.right() - 1) * s, (area.y() + 1) * s, s, (area.height() - 2) * s,
                         kFaceShadow);
        const QString label = QLatin1String(kButtons[i]);
        skin_.draw_tight_text(painter, label,
                              {area.x() + (area.width() - skin_.tight_text_width(label) + 1) / 2,
                               area.y() + (area.height() - wa::kGlyphHeight) / 2 + 1},
                              kButtonInk);
    }

    // Alca de redimensionar no canto inferior direito, como na referencia: sem
    // ela nao ha sinal de que a aresta de baixo arrasta.
    for (int line = 0; line < 3; ++line) {
        const int offset = 2 + line * 3;
        for (int i = 0; i <= offset; ++i)
            painter.fillRect((kWidth - 3 - i) * s, (logical_height_ - 3 - offset + i) * s, s, s,
                             kBevelLight);
    }

    int unknown = 0;
    const std::int64_t known = playlist.known_duration_ms(&unknown);
    // LI-11 — o "+" avisa que ha duracao desconhecida fora da soma.
    const QString totals = QStringLiteral("%1 %2%3")
                               .arg(playlist.size())
                               .arg(format_total_ms(known))
                               .arg(unknown > 0 ? QStringLiteral("+") : QString());
    // A CAIXA do relogio e que termina em 258, nao o texto: e ela a aresta que
    // se ve, e e ela que precisa bater com o fim dos demais elementos.
    const int clock_width = skin_.text_width(totals) + 10;
    const int clock_height = wa::kGlyphHeight + 8;
    const QRect clock(kWidth - kListMargin - clock_width + 1,
                      footer_top() + (kFooterHeight - clock_height) / 2, clock_width,
                      clock_height);
    const int totals_x = clock.x() + 5;
    const int totals_y = clock.y() + 4;
    painter.fillRect(clock.x() * s, clock.y() * s, clock.width() * s, clock.height() * s,
                     kBackground);
    painter.fillRect(clock.x() * s, clock.y() * s, clock.width() * s, s, kBevelDark);
    painter.fillRect(clock.x() * s, clock.y() * s, s, clock.height() * s, kBevelDark);
    painter.fillRect(clock.x() * s, clock.bottom() * s, clock.width() * s, s, kBevelLight);
    painter.fillRect(clock.right() * s, clock.y() * s, s, clock.height() * s, kBevelLight);
    skin_.draw_text(painter, totals, {totals_x, totals_y});
}

void PlaylistPanel::mousePressEvent(QMouseEvent* event) {
    const QPoint p = to_logical(event->pos());

    // Aresta inferior redimensiona: sem isso nao ha como ver mais linhas.
    if (p.y() >= logical_height_ - kResizeGrip) {
        resizing_ = true;
        resize_origin_ = event->globalPosition().toPoint().y();
        resize_start_height_ = logical_height_;
        return;
    }

    // Barra de rolagem: clicar no cursor arrasta; clicar no trilho salta para
    // o ponto. Antes so a roda do mouse rolava, e a barra era um enfeite.
    if (const QRect track = scrollbar_rect(); track.contains(p) && !scroll_thumb_rect().isNull()) {
        const QRect thumb = scroll_thumb_rect();
        scrolling_ = true;
        // Arrastando o cursor, ele acompanha o ponto agarrado; clicando no
        // trilho, ele centra no clique — que e o que se espera de um salto.
        scroll_grab_offset_ = thumb.contains(p) ? p.y() - thumb.y() : thumb.height() / 2;
        scroll_to_position(p.y());
        update();
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
        if (selected_.count(row)) selected_.erase(row); else selected_.insert(row);
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
    if (scrolling_) {
        scroll_to_position(to_logical(event->pos()).y());
        update();
        return;
    }
    if (!resizing_) {
        setCursor(to_logical(event->pos()).y() >= logical_height_ - kResizeGrip
                      ? Qt::SizeVerCursor
                      : Qt::ArrowCursor);
        return;
    }
    const int delta = (event->globalPosition().toPoint().y() - resize_origin_) /
                      std::max(1, skin_.scale());
    set_logical_height(resize_start_height_ + delta);
    if (on_height_changed) on_height_changed();
}

void PlaylistPanel::mouseReleaseEvent(QMouseEvent*) {
    resizing_ = false;
    scrolling_ = false;
}

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
            // LI-09 — busca por digitacao. Nao ha caixa de texto: em 275 px ela
            // custaria uma linha inteira.
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
