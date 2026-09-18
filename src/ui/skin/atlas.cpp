#include "ui/skin/atlas.h"

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

namespace pang::ui::skin {
namespace {

QColor color_from(const QJsonArray& array, const QColor& fallback = Qt::black) {
    if (array.size() < 3) return fallback;
    return QColor(array[0].toInt(), array[1].toInt(), array[2].toInt());
}

}  // namespace

bool Atlas::load(const QString& directory, QString& error) {
    QFile metadata(directory + QStringLiteral("/atlas.json"));
    if (!metadata.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("nao foi possivel abrir %1/atlas.json").arg(directory);
        return false;
    }

    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(metadata.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("atlas.json invalido: %1").arg(parse.errorString());
        return false;
    }
    const QJsonObject root = document.object();

    QImage image(directory + QStringLiteral("/atlas.png"));
    if (image.isNull()) {
        error = QStringLiteral("nao foi possivel abrir %1/atlas.png").arg(directory);
        return false;
    }
    image = image.convertToFormat(QImage::Format_ARGB32);

    // A cor-chave vira transparencia. O PNG e gravado sem canal alfa de
    // proposito: um arquivo RGB simples e mais facil de inspecionar e de
    // regerar do que um com alfa pre-multiplicado.
    const QColor key = color_from(root[QStringLiteral("transparent")].toArray(), QColor(255, 0, 255));
    const QRgb key_rgb = key.rgb();
    for (int y = 0; y < image.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if ((line[x] | 0xff000000u) == key_rgb) line[x] = 0;
    }
    source_ = QPixmap::fromImage(image);

    sprites_.clear();
    const QJsonObject sprites = root[QStringLiteral("sprites")].toObject();
    for (auto it = sprites.begin(); it != sprites.end(); ++it) {
        const QJsonArray rect = it.value().toArray();
        if (rect.size() == 4)
            sprites_.insert(it.key(), QRect(rect[0].toInt(), rect[1].toInt(), rect[2].toInt(),
                                            rect[3].toInt()));
    }

    palette_.clear();
    const QJsonObject palette = root[QStringLiteral("palette")].toObject();
    for (auto it = palette.begin(); it != palette.end(); ++it)
        palette_.insert(it.key(), color_from(it.value().toArray()));

    const QJsonObject glyph = root[QStringLiteral("glyph")].toObject();
    glyph_width_ = glyph[QStringLiteral("width")].toInt(5);
    glyph_height_ = glyph[QStringLiteral("height")].toInt(7);
    const QJsonObject digit = root[QStringLiteral("digit")].toObject();
    digit_height_ = digit[QStringLiteral("height")].toInt(13);

    spectrum_.clear();
    for (const QJsonValue& value : root[QStringLiteral("spectrum")].toArray())
        spectrum_.append(color_from(value.toArray(), QColor(0, 237, 0)));
    if (spectrum_.isEmpty()) spectrum_.append(QColor(0, 237, 0));
    peak_ = color_from(root[QStringLiteral("peak")].toArray(), QColor(200, 200, 200));

    rescale();
    return true;
}

void Atlas::set_scale(int scale) {
    const int clamped = qBound(1, scale, 4);
    if (clamped == scale_ && !scaled_.isNull()) return;
    scale_ = clamped;
    rescale();
}

void Atlas::rescale() {
    if (source_.isNull()) return;
    // FastTransformation e vizinho mais proximo: e o que preserva pixel art.
    // Qualquer suavizacao aqui borraria os chanfros de um pixel.
    scaled_ = scale_ == 1 ? source_
                          : source_.scaled(source_.width() * scale_, source_.height() * scale_,
                                           Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

QSize Atlas::sprite_size(const QString& name) const {
    const auto it = sprites_.constFind(name);
    return it == sprites_.constEnd() ? QSize() : it.value().size();
}

void Atlas::draw_raw(QPainter& painter, const QString& name, int x, int y,
                     const QColor& tint) const {
    const auto it = sprites_.constFind(name);
    if (it == sprites_.constEnd() || scaled_.isNull()) return;

    const QRect source(it.value().x() * scale_, it.value().y() * scale_,
                       it.value().width() * scale_, it.value().height() * scale_);
    const QRect target(x * scale_, y * scale_, source.width(), source.height());

    if (!tint.isValid()) {
        painter.drawPixmap(target, scaled_, source);
        return;
    }
    // Recolore preservando a forma: usado para texto desabilitado e destaque.
    QPixmap piece = scaled_.copy(source);
    QPainter colorizer(&piece);
    colorizer.setCompositionMode(QPainter::CompositionMode_SourceIn);
    colorizer.fillRect(piece.rect(), tint);
    colorizer.end();
    painter.drawPixmap(target, piece);
}

void Atlas::draw(QPainter& painter, const QString& name, int x, int y) const {
    draw_raw(painter, name, x, y, QColor());
}

void Atlas::draw_tiled(QPainter& painter, const QString& name, const QRect& area) const {
    const QSize piece = sprite_size(name);
    if (piece.isEmpty()) return;
    painter.save();
    painter.setClipRect(QRect(area.x() * scale_, area.y() * scale_, area.width() * scale_,
                              area.height() * scale_));
    for (int y = area.top(); y < area.bottom(); y += piece.height())
        for (int x = area.left(); x < area.right(); x += piece.width())
            draw_raw(painter, name, x, y, QColor());
    painter.restore();
}

int Atlas::text_width(const QString& text) const {
    if (text.isEmpty()) return 0;
    return text.size() * (glyph_width_ + 1) - 1;
}

void Atlas::draw_text(QPainter& painter, const QString& text, int x, int y,
                      const QColor& tint) const {
    for (int i = 0; i < text.size(); ++i) {
        const QChar character = text.at(i).toUpper();
        // O cast para int e obrigatorio: `unicode()` devolve char16_t, e a
        // sobrecarga de arg() para QChar insere o PROPRIO caractere, montando
        // "font/P" em vez de "font/80". Todo texto saia como fallback.
        QString name = QStringLiteral("font/%1").arg(static_cast<int>(character.unicode()));
        if (!sprites_.contains(name)) name = QStringLiteral("font/fallback");
        draw_raw(painter, name, x + i * (glyph_width_ + 1), y, tint);
    }
}

int Atlas::time_width(const QString& text) const {
    int total = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QString name =
            QStringLiteral("digit/%1").arg(static_cast<int>(text.at(i).unicode()));
        total += sprite_size(name).width();
    }
    return total;
}

void Atlas::draw_time(QPainter& painter, const QString& text, int x, int y) const {
    int cursor = x;
    for (int i = 0; i < text.size(); ++i) {
        const QString name =
            QStringLiteral("digit/%1").arg(static_cast<int>(text.at(i).unicode()));
        const QSize piece = sprite_size(name);
        if (piece.isEmpty()) continue;
        draw_raw(painter, name, cursor, y, QColor());
        cursor += piece.width();
    }
}

QColor Atlas::color(const QString& key) const {
    return palette_.value(key, QColor(0, 255, 127));
}

}  // namespace pang::ui::skin
