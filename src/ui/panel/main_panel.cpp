#include "ui/panel/main_panel.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QPainter>
#include <QWindow>

#include <algorithm>
#include <cmath>

#include "ui/skin/winamp_layout.h"

namespace pang::ui {
namespace {

namespace wa = skin::winamp;
using core::State;

constexpr float kScrollInterval = 0.18f;  // segundos por pixel de rolagem

// Areas clicaveis, derivadas das posicoes e tamanhos do formato. Nao ha
// coordenada solta aqui: tudo sai de winamp_layout.h.
constexpr QRect area_of(const QPoint& at, const QSize& size) {
    return QRect(at.x(), at.y(), size.width(), size.height());
}

const QRect kTitlebarArea{0, 0, 275, 14};
const QRect kPreviousArea = area_of(wa::kPreviousAt, {23, 18});
const QRect kPlayArea = area_of(wa::kPlayAt, {23, 18});
const QRect kPauseArea = area_of(wa::kPauseAt, {23, 18});
const QRect kStopArea = area_of(wa::kStopAt, {23, 18});
const QRect kNextArea = area_of(wa::kNextAt, {23, 18});
const QRect kEjectArea = area_of(wa::kEjectAt, {22, 16});
const QRect kShuffleArea = area_of(wa::kShuffleAt, {47, 15});
const QRect kRepeatArea = area_of(wa::kRepeatAt, {28, 15});
const QRect kEqualizerArea = area_of(wa::kEqualizerAt, {23, 12});
const QRect kPlaylistArea = area_of(wa::kPlaylistAt, {23, 12});
const QRect kVolumeArea = area_of(wa::kVolumeAt, wa::kVolumeSize);
const QRect kBalanceArea = area_of(wa::kBalanceAt, wa::kBalanceSize);
const QRect kPositionArea = area_of(wa::kPositionAt, wa::kPositionBackground.source.size());
const QRect kTimeArea{wa::kTimeSignAt.x(), wa::kTimeSignAt.y(), 63, wa::kDigitHeight};
const QRect kMinimizeArea = area_of(wa::kMinimizeAt, {9, 9});
const QRect kShadeArea = area_of(wa::kShadeAt, {9, 9});
const QRect kCloseArea = area_of(wa::kCloseAt, {9, 9});

constexpr int kThumbWidth = 14;
constexpr int kPositionThumbWidth = 29;

// Quatro digitos, "MMSS". Sem valor devolve espacos, que viram a celula vazia
// do numbers.bmp — nunca um zero inventado (PL-26).
QString format_time(std::int64_t frames, int rate) {
    if (frames < 0 || rate <= 0) return QStringLiteral("    ");
    const std::int64_t total = frames / rate;
    return QStringLiteral("%1%2")
        .arg(std::min<std::int64_t>(total / 60, 99), 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace

MainPanel::MainPanel(core::Controller& controller, core::Engine& engine, skin::WinampSkin& skin,
                     QWidget* parent)
    : QWidget(parent), controller_(controller), engine_(engine), skin_(skin) {
    setFocusPolicy(Qt::StrongFocus);
    // IN-03 — o painel nao tem widget por controle, entao o que o leitor de
    // tela anuncia e a janela. O detalhe de cada controle vem do tooltip, que
    // segue a posicao do cursor.
    setAccessibleName(tr("Player"));
    setAccessibleDescription(tr("Controles de reproducao, volume, balanco, posicao e visualizacao"));
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    analyzer_.configure(engine.sample_rate(), engine.channels());
    capture_.resize(8192);
    set_scale(skin.scale());
    set_visualization(Visualization::Spectrum);
}

void MainPanel::set_scale(int scale) {
    skin_.set_scale(scale);
    // AP-13 — a area clicavel escala junto: o alvo CRESCE com a escala.
    setFixedSize(kWidth * skin_.scale(), (compact_ ? kCompactHeight : kHeight) * skin_.scale());
    update();
}

void MainPanel::set_compact(bool compact) {
    compact_ = compact;
    set_scale(skin_.scale());
}

void MainPanel::set_visualization(Visualization mode) {
    visualization_ = mode;
    engine_.set_capture_enabled(mode != Visualization::Off);  // VI-17
    if (mode == Visualization::Off) analyzer_.reset();
    update();
}

QPoint MainPanel::to_logical(const QPoint& physical) const {
    const int s = std::max(1, skin_.scale());
    return QPoint(physical.x() / s, physical.y() / s);
}

void MainPanel::tick(float dt_seconds) {
    snapshot_ = engine_.snapshot();

    if (visualization_ != Visualization::Off) {
        if (snapshot_.sample_rate > 0)
            analyzer_.set_source_nyquist(static_cast<float>(snapshot_.sample_rate) / 2.0f);

        if (snapshot_.state == State::Stopped || snapshot_.state == State::Error)
            analyzer_.reset();
        else if (snapshot_.state == State::Paused)
            analyzer_.hold();
        else
            for (;;) {
                const std::size_t got =
                    engine_.read_visualization(capture_.data(), capture_.size());
                if (got == 0) break;
                analyzer_.feed(capture_.data(), got / engine_.channels());
                if (got < capture_.size()) break;
            }
        analyzer_.advance_peaks(dt_seconds);
    }

    title_timer_ += dt_seconds;
    if (title_timer_ >= kScrollInterval) {
        title_timer_ = 0.0f;
        ++title_offset_;
    }
    update();
}

// ---------------------------------------------------------------- desenho

// Nome de cada controle, para o tooltip e para o nome acessivel. Traz o atalho
// junto porque e onde o usuario procura: quem passa o mouse para descobrir o
// que o botao faz tambem quer saber se ha tecla para ele.
//
// O switch e sobre o proprio Hit e nao tem `default`: assim, acrescentar um
// controle sem lhe dar nome vira aviso do compilador, e nao um tooltip vazio
// que ninguem nota. Mapear por inteiro, como a primeira versao fazia, deixaria
// os nomes trocados em silencio se o enum fosse reordenado.
QString MainPanel::control_name(Hit hit) {
    switch (hit) {
        case Hit::None:      return {};
        case Hit::Titlebar:  return tr("Barra de titulo — arraste para mover, duplo clique para o modo barra");
        case Hit::Previous:  return tr("Faixa anterior (Z)");
        case Hit::Play:      return tr("Tocar (X)");
        case Hit::Pause:     return tr("Pausar (C)");
        case Hit::Stop:      return tr("Parar (V)");
        case Hit::Next:      return tr("Proxima faixa (B)");
        case Hit::Eject:     return tr("Abrir arquivos");
        case Hit::Shuffle:   return tr("Ordem aleatoria");
        case Hit::Repeat:    return tr("Repetir");
        case Hit::Equalizer: return tr("Equalizador (Ctrl+E)");
        case Hit::Playlist:  return tr("Playlist (Ctrl+P)");
        case Hit::Volume:    return tr("Volume");
        case Hit::Balance:   return tr("Balanco");
        case Hit::Position:  return tr("Posicao na faixa — arraste para buscar");
        case Hit::Time:      return tr("Tempo — clique para alternar entre decorrido e restante");
        case Hit::Vis:       return tr("Visualizacao — clique para alternar espectro, osciloscopio e desligado");
        case Hit::Minimize:  return tr("Minimizar");
        case Hit::Shade:     return tr("Modo barra (Ctrl+W)");
        case Hit::Close:     return tr("Fechar");
    }
    return {};
}

bool MainPanel::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const QString name = control_name(hit_test(to_logical(help->pos())));
        if (name.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), name, this);
        return true;
    }
    return QWidget::event(event);
}

void MainPanel::contextMenuEvent(QContextMenuEvent* event) {
    if (on_context_menu) on_context_menu(event->globalPos());
}

void MainPanel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (compact_) {
        skin_.draw(painter, hasFocus() ? wa::kTitlebarActive : wa::kTitlebarInactive, {0, 0});
        skin_.draw_text(painter, format_time(snapshot_.position_frames, engine_.sample_rate()),
                        {46, 4});
        return;
    }

    // Fundo: tudo que nao e controle vem pronto de main.bmp — pocos,
    // dois-pontos do relogio e os rotulos fixos "kbps" e "khz". E essa a
    // diferenca de adotar o formato: o arranjo ja esta desenhado.
    skin_.draw(painter, wa::kMainBackground, {0, 0});
    skin_.draw(painter, hasFocus() ? wa::kTitlebarActive : wa::kTitlebarInactive, {0, 0});

    paint_display(painter);
    paint_visualization(painter);
    paint_limiter(painter);
    paint_sliders(painter);
    paint_buttons(painter);
}

void MainPanel::paint_display(QPainter& painter) {
    // PL-15 — decorrido ou restante, alternado clicando no mostrador.
    std::int64_t frames = snapshot_.position_frames;
    bool negative = false;
    if (show_remaining_ && snapshot_.duration_frames > 0) {
        frames = snapshot_.duration_frames - snapshot_.position_frames;
        negative = true;
    }
    const QString digits = format_time(frames, engine_.sample_rate());

    const auto digit = [&](QChar character, const QPoint& at) {
        // Celula 10 do numbers.bmp e a vazia.
        const int index = character.isDigit() ? character.digitValue() : 10;
        skin_.draw_frame(painter, wa::kDigitsBitmap,
                         QRect(index * wa::kDigitWidth, 0, wa::kDigitWidth, wa::kDigitHeight), 0,
                         0, at);
    };

    if (digits.size() == 4) {
        digit(digits.at(0), wa::kTimeMinuteTensAt);
        digit(digits.at(1), wa::kTimeMinuteUnitsAt);
        digit(digits.at(2), wa::kTimeSecondTensAt);
        digit(digits.at(3), wa::kTimeSecondUnitsAt);
    }
    if (negative)
        skin_.draw_text(painter, QStringLiteral("-"), {wa::kTimeSignAt.x(), wa::kTimeSignAt.y() + 4});

    skin_.draw(painter,
               snapshot_.state == State::Playing  ? wa::kIndicatorPlay
               : snapshot_.state == State::Paused ? wa::kIndicatorPause
                                                  : wa::kIndicatorStop,
               wa::kIndicatorAt);

    // PL-17, PL-18 — ausencia nao vira numero plausivel. Alinhados a direita
    // das caixas que main.bmp ja desenhou.
    const QString bitrate = snapshot_.bitrate_bps > 0
                                ? QString::number(snapshot_.bitrate_bps / 1000)
                                : QStringLiteral("   ");
    skin_.draw_text(painter, bitrate,
                    {wa::kBitrate.right() + 1 - skin_.text_width(bitrate), wa::kBitrate.y()});

    const QString rate = snapshot_.sample_rate > 0 ? QString::number(snapshot_.sample_rate / 1000)
                                                   : QStringLiteral("  ");
    skin_.draw_text(painter, rate,
                    {wa::kSampleRate.right() + 1 - skin_.text_width(rate), wa::kSampleRate.y()});

    // PL-19 — mono e estereo como dois indicadores, o inativo apagado.
    skin_.draw(painter, snapshot_.channels == 1 ? wa::kMonoOn : wa::kMonoOff, wa::kMonoAt);
    skin_.draw(painter, snapshot_.channels > 1 ? wa::kStereoOn : wa::kStereoOff, wa::kStereoAt);

    // PL-20 e PL-14 — posicao na playlist e titulo com rolagem.
    const int index = controller_.current_index();
    QString title;
    if (index >= 0)
        title = QStringLiteral("%1. %2")
                    .arg(index + 1)
                    .arg(QString::fromStdString(controller_.playlist().at(index).display_title()));
    else if (snapshot_.state == State::Error)
        title = QStringLiteral("ERRO");
    else
        title = QStringLiteral("PLAYAMP NG");

    const int s = skin_.scale();
    painter.save();
    painter.setClipRect(wa::kSongTitle.x() * s, wa::kSongTitle.y() * s,
                        wa::kSongTitle.width() * s, wa::kSongTitle.height() * s);
    if (skin_.text_width(title) <= wa::kSongTitle.width()) {
        title_offset_ = 0;
        skin_.draw_text(painter, title, wa::kSongTitle.topLeft());
    } else {
        // Emendado consigo mesmo, para nao saltar ao dar a volta.
        const QString marquee = title + QStringLiteral("   ***   ");
        const int span = skin_.text_width(marquee);
        if (title_offset_ >= span) title_offset_ = 0;
        skin_.draw_text(painter, marquee + marquee,
                        {wa::kSongTitle.x() - title_offset_, wa::kSongTitle.y()});
    }
    painter.restore();
}

// AU-14 — medidor de reducao do limitador, da direita para a esquerda.
//
// A escala vai ate 12 dB porque e a faixa do equalizador: com o preamp e as
// bandas no maximo, e essa a ordem de grandeza do excesso que o limitador tem
// de absorver. Reducao abaixo de meio decibel nao acende — o limitador toca de
// leve no sinal o tempo todo, e um medidor que pisca por isso vira ruido
// visual em vez de aviso.
void MainPanel::paint_limiter(QPainter& painter) {
    const int s = skin_.scale();
    const QRect meter = wa::kLimiterMeter;
    const float gain = snapshot_.limiter_gain;
    if (gain >= 1.0f) return;

    const float reduction_db = -20.0f * std::log10(std::max(gain, 1e-4f));
    if (reduction_db < 0.5f) return;

    const int filled =
        std::clamp(static_cast<int>(reduction_db / 12.0f * meter.width()), 1, meter.width());
    // Rampa propria, e nao a do espectro: indexar a paleta do espectro dava
    // verde-oliva escuro para reducao pequena, e um aviso que nao se ve nao
    // avisa. Aqui a cor vai de amarelo a vermelho conforme a reducao cresce,
    // sempre em brilho cheio.
    const float t = std::clamp(reduction_db / 9.0f, 0.0f, 1.0f);
    const QColor ink(static_cast<int>(240 - 30 * t), static_cast<int>(203 - 150 * t),
                     static_cast<int>(71 - 40 * t));
    painter.fillRect((meter.right() + 1 - filled) * s, meter.y() * s, filled * s,
                     meter.height() * s, ink);
}

void MainPanel::paint_visualization(QPainter& painter) {
    if (visualization_ == Visualization::Off) return;

    const int s = skin_.scale();
    const QRect well = wa::kVisualization;
    const QVector<QColor>& colors = skin_.visualization_colors();

    // viscolor.txt: 0 fundo, 1 grade, 2..17 degrade do espectro do topo para a
    // base, 18..23 osciloscopio. A cor vem do SKIN, nao de escolha nossa.
    painter.fillRect(well.x() * s, well.y() * s, well.width() * s, well.height() * s,
                     colors.isEmpty() ? QColor(0, 0, 0) : colors[0]);

    // Grade pontilhada do fundo, cor 1 do viscolor.txt. Existe para dar escala
    // ao que as barras mostram: sem ela o espectro flutua num retangulo preto.
    if (colors.size() > 1) {
        const QColor grid = colors[1];
        // A grade tem de cair nas MESMAS colunas e linhas da que ja vem
        // gravada no main.bmp. Contando a partir da origem do retangulo da
        // visualizacao, que comeca numa linha impar, os pontos saiam meio
        // passo fora dos de tras e os dois conjuntos juntos viravam tracos.
        for (int y = well.y() + (well.y() & 1); y <= well.bottom(); y += 2)
            for (int x = well.x() + (well.x() & 1); x <= well.right(); x += 2)
                painter.fillRect(x * s, y * s, s, s, grid);
    }

    if (visualization_ == Visualization::Scope) {
        const std::vector<float>& scope = analyzer_.scope();
        if (scope.empty()) return;
        painter.setPen(colors.size() > 20 ? colors[20] : QColor(0, 255, 0));
        const qreal middle = (well.y() + well.height() / 2.0) * s;
        const qreal amplitude = well.height() / 2.0 * s;
        QPointF previous(well.x() * s, middle);
        for (int x = 0; x < well.width() * s; ++x) {
            const std::size_t i =
                static_cast<std::size_t>(qreal(x) / (well.width() * s) * (scope.size() - 1));
            const QPointF point(well.x() * s + x, middle - scope[i] * amplitude);
            painter.drawLine(previous, point);
            previous = point;
        }
        return;
    }

    // 19 barras de 3 px com 1 px de intervalo ocupam os 76 px da area.
    constexpr int kBarWidth = 3;
    constexpr int kBarPitch = 4;
    const int rows = well.height();

    // As barras comecam uma coluna adiante da origem do retangulo para que os
    // VAOS entre elas caiam em colunas pares — as mesmas em que ficam os
    // pontos da grade. Alinhadas na origem, os vaos caiam todos em coluna
    // impar, ficavam sem ponto nenhum, e o que se via eram riscos pretos
    // separando as barras em vez do fundo pontilhado.
    constexpr int kBarInset = 1;
    for (int bar = 0; bar < core::dsp::SpectrumAnalyzer::kBars; ++bar) {
        const int x = (well.x() + kBarInset + bar * kBarPitch) * s;
        const int filled =
            static_cast<int>(analyzer_.bars()[static_cast<std::size_t>(bar)] * rows);

        for (int row = 0; row < filled; ++row) {
            QColor color(0, 255, 0);
            if (colors.size() >= 18) {
                // Indice 2 no topo da barra, 17 na base.
                const int index = 17 - row * 15 / std::max(1, rows - 1);
                color = colors[std::clamp(index, 2, 17)];
            }
            painter.fillRect(x, (well.y() + rows - 1 - row) * s, kBarWidth * s, s, color);
        }

        const int peak =
            static_cast<int>(analyzer_.peaks()[static_cast<std::size_t>(bar)] * rows);
        if (peak > 0)
            painter.fillRect(x, (well.y() + rows - peak) * s, kBarWidth * s, s,
                             colors.size() > 23 ? colors[23] : QColor(200, 200, 200));
    }
}

void MainPanel::paint_sliders(QPainter& painter) {
    // O quadro escolhido representa o VALOR: e a cor da faixa inteira que diz
    // o nivel, e nao um degrade fixo com preenchimento por cima.
    const auto frame_for = [](float normalized, int frames) {
        return std::clamp(static_cast<int>(normalized * (frames - 1)), 0, frames - 1);
    };

    skin_.draw_frame(painter, "volume", QRect(0, 0, wa::kVolumeSize.width(), 13),
                     wa::kVolumeFrameStride, frame_for(engine_.volume(), wa::kVolumeFrames),
                     wa::kVolumeAt);
    skin_.draw(
        painter, dragging_ == Hit::Volume ? wa::kVolumeThumbPressed : wa::kVolumeThumbNormal,
        {wa::kVolumeAt.x() +
             static_cast<int>(engine_.volume() * (wa::kVolumeSize.width() - kThumbWidth)),
         wa::kVolumeAt.y() + 1});

    skin_.draw_frame(painter, "balance", QRect(0, 0, wa::kBalanceSize.width(), 13),
                     wa::kVolumeFrameStride,
                     frame_for(std::fabs(engine_.balance()), wa::kBalanceFrames), wa::kBalanceAt);
    skin_.draw(
        painter, dragging_ == Hit::Balance ? wa::kBalanceThumbPressed : wa::kBalanceThumbNormal,
        {wa::kBalanceAt.x() + static_cast<int>((engine_.balance() + 1.0f) / 2.0f *
                                               (wa::kBalanceSize.width() - kThumbWidth)),
         wa::kBalanceAt.y() + 1});

    skin_.draw(painter, wa::kPositionBackground, wa::kPositionAt);

    // PL-21/PL-23 — so aparece quando a fonte permite busca e informa duracao.
    if (snapshot_.seekable && snapshot_.duration_frames > 0) {
        const float fraction =
            std::clamp(static_cast<float>(snapshot_.position_frames) / snapshot_.duration_frames,
                       0.0f, 1.0f);
        skin_.draw(
            painter,
            dragging_ == Hit::Position ? wa::kPositionThumbPressed : wa::kPositionThumbNormal,
            {wa::kPositionAt.x() + static_cast<int>(fraction * (kPositionArea.width() - kPositionThumbWidth)),
             wa::kPositionAt.y()});
    }
}

void MainPanel::paint_buttons(QPainter& painter) {
    const auto button = [&](Hit hit, const wa::Sprite& normal, const wa::Sprite& down,
                            const QPoint& at) { skin_.draw(painter, pressed_ == hit ? down : normal, at); };

    button(Hit::Previous, wa::kPreviousNormal, wa::kPreviousPressed, wa::kPreviousAt);
    button(Hit::Play, wa::kPlayNormal, wa::kPlayPressed, wa::kPlayAt);
    button(Hit::Pause, wa::kPauseNormal, wa::kPausePressed, wa::kPauseAt);
    button(Hit::Stop, wa::kStopNormal, wa::kStopPressed, wa::kStopAt);
    button(Hit::Next, wa::kNextNormal, wa::kNextPressed, wa::kNextAt);
    button(Hit::Eject, wa::kEjectNormal, wa::kEjectPressed, wa::kEjectAt);

    const bool shuffle = controller_.shuffle();
    skin_.draw(painter,
               pressed_ == Hit::Shuffle
                   ? (shuffle ? wa::kShuffleOnPressed : wa::kShuffleOffPressed)
               : shuffle ? wa::kShuffleOn
                         : wa::kShuffleOff,
               wa::kShuffleAt);

    const bool repeat = controller_.repeat() != core::Repeat::Off;
    skin_.draw(painter,
               pressed_ == Hit::Repeat ? (repeat ? wa::kRepeatOnPressed : wa::kRepeatOffPressed)
               : repeat                ? wa::kRepeatOn
                                       : wa::kRepeatOff,
               wa::kRepeatAt);

    skin_.draw(painter,
               equalizer_visible && equalizer_visible() ? wa::kEqualizerOn : wa::kEqualizerOff,
               wa::kEqualizerAt);
    skin_.draw(painter,
               playlist_visible && playlist_visible() ? wa::kPlaylistOn : wa::kPlaylistOff,
               wa::kPlaylistAt);

    skin_.draw(painter, pressed_ == Hit::Minimize ? wa::kMinimizePressed : wa::kMinimizeNormal,
               wa::kMinimizeAt);
    skin_.draw(painter, pressed_ == Hit::Shade ? wa::kShadePressed : wa::kShadeNormal,
               wa::kShadeAt);
    skin_.draw(painter, pressed_ == Hit::Close ? wa::kClosePressed : wa::kCloseNormal,
               wa::kCloseAt);
}

// ------------------------------------------------------------------ entrada

MainPanel::Hit MainPanel::hit_test(const QPoint& p) const {
    const struct {
        QRect area;
        Hit hit;
    } regions[] = {
        {kPreviousArea, Hit::Previous},    {kPlayArea, Hit::Play},
        {kPauseArea, Hit::Pause},          {kStopArea, Hit::Stop},
        {kNextArea, Hit::Next},            {kEjectArea, Hit::Eject},
        {kShuffleArea, Hit::Shuffle},      {kRepeatArea, Hit::Repeat},
        {kEqualizerArea, Hit::Equalizer},  {kPlaylistArea, Hit::Playlist},
        {kVolumeArea, Hit::Volume},        {kBalanceArea, Hit::Balance},
        {kPositionArea, Hit::Position},    {kTimeArea, Hit::Time},
        {wa::kVisualization, Hit::Vis},    {kMinimizeArea, Hit::Minimize},
        {kShadeArea, Hit::Shade},          {kCloseArea, Hit::Close},
        {kTitlebarArea, Hit::Titlebar},
    };
    for (const auto& region : regions)
        if (region.area.contains(p)) return region.hit;
    return Hit::None;
}

void MainPanel::mousePressEvent(QMouseEvent* event) {
    const QPoint p = to_logical(event->pos());
    const Hit hit = hit_test(p);

    if (hit == Hit::Titlebar && event->type() == QEvent::MouseButtonDblClick) {
        if (on_toggle_compact) on_toggle_compact();  // AP-11, gesto classico
        return;
    }
    if (hit == Hit::Volume || hit == Hit::Balance) {
        dragging_ = hit;
        const QRect groove = hit == Hit::Volume ? kVolumeArea : kBalanceArea;
        const float fraction = std::clamp(
            static_cast<float>(p.x() - groove.x() - kThumbWidth / 2) / (groove.width() - kThumbWidth),
            0.0f, 1.0f);
        if (hit == Hit::Volume)
            engine_.set_volume(fraction);
        else
            engine_.set_balance(fraction * 2.0f - 1.0f);
        update();
        return;
    }
    if (hit == Hit::Position && snapshot_.seekable && snapshot_.duration_frames > 0) {
        dragging_ = hit;
        update();
        return;
    }
    if (hit == Hit::Titlebar) {
        // Wayland nao deixa o cliente mover a propria janela; o compositor faz.
        if (window()->windowHandle()) window()->windowHandle()->startSystemMove();
        return;
    }
    pressed_ = hit;
    update();
}

void MainPanel::mouseDoubleClickEvent(QMouseEvent* event) { mousePressEvent(event); }

void MainPanel::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_ == Hit::None) return;
    const QPoint p = to_logical(event->pos());
    if (dragging_ == Hit::Position) {
        update();  // a busca so confirma no release, sem disparar seek em serie
        return;
    }
    const QRect groove = dragging_ == Hit::Volume ? kVolumeArea : kBalanceArea;
    const float fraction = std::clamp(
        static_cast<float>(p.x() - groove.x() - kThumbWidth / 2) / (groove.width() - kThumbWidth),
        0.0f, 1.0f);
    if (dragging_ == Hit::Volume)
        engine_.set_volume(fraction);
    else
        engine_.set_balance(fraction * 2.0f - 1.0f);
    update();
}

void MainPanel::mouseReleaseEvent(QMouseEvent* event) {
    const QPoint p = to_logical(event->pos());

    if (dragging_ == Hit::Position) {
        const float fraction =
            std::clamp(static_cast<float>(p.x() - kPositionArea.x() - kPositionThumbWidth / 2) /
                           (kPositionArea.width() - kPositionThumbWidth),
                       0.0f, 1.0f);
        controller_.seek(static_cast<double>(fraction) * snapshot_.duration_frames /
                         engine_.sample_rate());
        dragging_ = Hit::None;
        update();
        return;
    }
    if (dragging_ != Hit::None) {
        dragging_ = Hit::None;
        return;
    }

    const Hit hit = hit_test(p);
    if (hit != pressed_) {
        pressed_ = Hit::None;
        update();
        return;
    }

    switch (hit) {
        case Hit::Previous: controller_.previous(); break;
        case Hit::Play:     controller_.play(); break;
        case Hit::Pause:    controller_.pause(); break;
        case Hit::Stop:     controller_.stop(); break;
        case Hit::Next:     controller_.next(); break;
        case Hit::Eject:    if (on_open) on_open(); break;
        case Hit::Shuffle:  controller_.set_shuffle(!controller_.shuffle()); break;
        case Hit::Repeat: {
            const core::Repeat current = controller_.repeat();
            controller_.set_repeat(current == core::Repeat::Off   ? core::Repeat::All
                                   : current == core::Repeat::All ? core::Repeat::Track
                                                                  : core::Repeat::Off);
            break;
        }
        case Hit::Equalizer: if (on_toggle_equalizer) on_toggle_equalizer(); break;
        case Hit::Playlist:  if (on_toggle_playlist) on_toggle_playlist(); break;
        case Hit::Time:      show_remaining_ = !show_remaining_; break;  // PL-15
        case Hit::Vis:
            set_visualization(visualization_ == Visualization::Spectrum ? Visualization::Scope
                              : visualization_ == Visualization::Scope  ? Visualization::Off
                                                                        : Visualization::Spectrum);
            break;
        case Hit::Minimize: window()->showMinimized(); break;
        case Hit::Shade:    if (on_toggle_compact) on_toggle_compact(); break;
        case Hit::Close:    window()->close(); break;
        default: break;
    }
    pressed_ = Hit::None;
    update();
}

// IN-02 — todos os controles operaveis por teclado.
void MainPanel::keyPressEvent(QKeyEvent* event) {
    // Combinacoes com Ctrl pertencem ao shell; deixar passar e o que faz o
    // atalho funcionar com qualquer painel focado.
    if (event->modifiers() & Qt::ControlModifier) {
        event->ignore();
        QWidget::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
        case Qt::Key_X: case Qt::Key_Space: controller_.play(); break;
        case Qt::Key_C: controller_.pause(); break;
        case Qt::Key_V: controller_.stop(); break;
        case Qt::Key_Z: controller_.previous(); break;
        case Qt::Key_B: controller_.next(); break;
        case Qt::Key_S: controller_.set_shuffle(!controller_.shuffle()); break;
        case Qt::Key_L: if (on_open) on_open(); break;
        case Qt::Key_Up:
            engine_.set_volume(std::min(1.0f, engine_.volume() + 0.05f));
            break;
        case Qt::Key_Down:
            engine_.set_volume(std::max(0.0f, engine_.volume() - 0.05f));
            break;
        case Qt::Key_Left:
            if (snapshot_.seekable)
                controller_.seek(std::max(
                    0.0, double(snapshot_.position_frames) / engine_.sample_rate() - 5.0));
            break;
        case Qt::Key_Right:
            if (snapshot_.seekable)
                controller_.seek(double(snapshot_.position_frames) / engine_.sample_rate() + 5.0);
            break;
        default:
            QWidget::keyPressEvent(event);
            return;
    }
    update();
}

}  // namespace pang::ui
