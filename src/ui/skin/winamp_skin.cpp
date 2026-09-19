#include "ui/skin/winamp_skin.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>

#include "core/util/log.h"
#include "ui/skin/zip.h"

namespace pang::ui::skin {
namespace {

// O formato nasceu com .bmp; skins mais novos as vezes usam .png.
const char* const kExtensions[] = {".bmp", ".BMP", ".png", ".PNG"};

// A cor magenta e usada como transparencia em algumas pecas do formato.
QPixmap from_bytes(const QByteArray& data) {
    QImage image;
    if (!image.loadFromData(data)) return {};
    return QPixmap::fromImage(image);
}

}  // namespace

bool WinampSkin::load(const QString& path, QString& error) {
    bitmaps_.clear();
    glyphs_.clear();
    tinted_.clear();
    advances_.clear();
    viscolor_.clear();
    name_ = QFileInfo(path).completeBaseName();

    const bool ok = QFileInfo(path).isDir() ? load_from_directory(path, error)
                                            : load_from_archive(path, error);
    if (!ok) return false;

    if (!bitmaps_.contains(QStringLiteral("main"))) {
        error = QStringLiteral("o skin nao traz main.bmp, que e obrigatorio");
        return false;
    }
    index_glyphs();
    return true;
}

bool WinampSkin::load_from_directory(const QString& path, QString& error) {
    const QDir dir(path);
    for (const char* name : winamp::kBitmaps)
        for (const char* extension : kExtensions) {
            const QString file = dir.filePath(QLatin1String(name) + QLatin1String(extension));
            if (!QFile::exists(file)) continue;
            QPixmap pixmap(file);
            if (!pixmap.isNull()) bitmaps_.insert(QLatin1String(name), pixmap);
            break;
        }

    QFile viscolor(dir.filePath(QStringLiteral("viscolor.txt")));
    if (viscolor.open(QIODevice::ReadOnly)) parse_viscolor(viscolor.readAll());

    if (bitmaps_.isEmpty()) {
        error = QStringLiteral("nenhum bitmap de skin encontrado em %1").arg(path);
        return false;
    }
    return true;
}

bool WinampSkin::load_from_archive(const QString& path, QString& error) {
    Zip zip;
    if (!zip.open(path, error)) return false;

    for (const char* name : winamp::kBitmaps)
        for (const char* extension : kExtensions) {
            const QString file = QLatin1String(name) + QLatin1String(extension);
            if (!zip.contains(file)) continue;
            const QPixmap pixmap = from_bytes(zip.read(file));
            if (!pixmap.isNull()) bitmaps_.insert(QLatin1String(name), pixmap);
            break;
        }

    if (zip.contains(QStringLiteral("viscolor.txt")))
        parse_viscolor(zip.read(QStringLiteral("viscolor.txt")));

    if (bitmaps_.isEmpty()) {
        error = QStringLiteral("%1 nao contem bitmap de skin algum").arg(path);
        return false;
    }
    return true;
}

// text.bmp: tres fileiras de 31 glifos de 5x6. Minusculas reusam o desenho das
// maiusculas, como o formato define.
void WinampSkin::index_glyphs() {
    const auto row = [this](const char* characters, int line) {
        const QString text = QString::fromUtf8(characters);
        for (int i = 0; i < text.size() && i < winamp::kGlyphsPerRow; ++i) {
            const QRect cell(i * winamp::kGlyphWidth, line * winamp::kGlyphHeight,
                             winamp::kGlyphWidth, winamp::kGlyphHeight);
            const QChar character = text.at(i);
            glyphs_.insert(character.unicode(), cell);
            if (character.isUpper()) glyphs_.insert(character.toLower().unicode(), cell);
        }
    };
    row(winamp::kTextRow0, 0);
    row(winamp::kTextRow1, 1);
    row(winamp::kTextRow2, 2);
}

void WinampSkin::parse_viscolor(const QByteArray& text) {
    viscolor_.clear();
    for (const QByteArray& raw : text.split('\n')) {
        // Linhas trazem "r,g,b" e, depois de "//", um comentario.
        QByteArray line = raw;
        const int comment = line.indexOf("//");
        if (comment >= 0) line = line.left(comment);
        const QList<QByteArray> parts = line.simplified().split(',');
        if (parts.size() < 3) continue;
        viscolor_.append(QColor(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(),
                                parts[2].trimmed().toInt()));
    }
}

QPixmap WinampSkin::bitmap(const char* name) const {
    return bitmaps_.value(QLatin1String(name));
}

QPixmap WinampSkin::sprite(const winamp::Sprite& sprite) const {
    const QPixmap source = bitmap(sprite.bitmap);
    if (source.isNull()) return {};
    // Recorta apenas o que existe: skins incompletos trazem bitmaps menores que
    // o esperado, e um copy() fora dos limites devolveria pixels vazios.
    const QRect clipped = sprite.source.intersected(source.rect());
    if (clipped.isEmpty()) return {};
    return source.copy(clipped);
}

QPixmap WinampSkin::frame(const char* bitmap_name, const QRect& first, int stride,
                          int index) const {
    const QPixmap source = bitmap(bitmap_name);
    if (source.isNull()) return {};
    const QRect cell(first.x(), first.y() + index * stride, first.width(), first.height());
    const QRect clipped = cell.intersected(source.rect());
    if (clipped.size() != cell.size()) return {};
    return source.copy(clipped);
}

QPixmap WinampSkin::glyph(QChar character) const {
    const QPixmap source = bitmap(winamp::kTextBitmap);
    if (source.isNull()) return {};
    auto it = glyphs_.constFind(character.unicode());
    if (it == glyphs_.constEnd()) it = glyphs_.constFind(u' ');
    if (it == glyphs_.constEnd()) return {};
    const QRect clipped = it.value().intersected(source.rect());
    if (clipped.size() != it.value().size()) return {};
    return source.copy(clipped);
}

void WinampSkin::set_scale(int scale) {
    const int clamped = qBound(1, scale, 4);
    if (clamped == scale_) return;
    scale_ = clamped;
    scaled_.clear();
    tinted_.clear();   // as versoes recoloridas sao por escala
}

// Reescala sob demanda e guarda. FastTransformation e vizinho mais proximo: e
// o que preserva pixel art; qualquer suavizacao borraria os chanfros de 1 px.
QPixmap WinampSkin::scaled(const char* name) const {
    const QString key = QLatin1String(name);
    const auto cached = scaled_.constFind(key);
    if (cached != scaled_.constEnd()) return cached.value();

    const QPixmap source = bitmaps_.value(key);
    if (source.isNull()) return {};
    const QPixmap result =
        scale_ == 1 ? source
                    : source.scaled(source.width() * scale_, source.height() * scale_,
                                    Qt::IgnoreAspectRatio, Qt::FastTransformation);
    scaled_.insert(key, result);
    return result;
}

void WinampSkin::draw(QPainter& painter, const winamp::Sprite& sprite, const QPoint& at) const {
    const QPixmap source = scaled(sprite.bitmap);
    if (source.isNull()) return;
    const QRect region(sprite.source.x() * scale_, sprite.source.y() * scale_,
                       sprite.source.width() * scale_, sprite.source.height() * scale_);
    if (!source.rect().contains(region)) return;
    painter.drawPixmap(at.x() * scale_, at.y() * scale_, source, region.x(), region.y(),
                       region.width(), region.height());
}

void WinampSkin::draw_frame(QPainter& painter, const char* bitmap_name, const QRect& first,
                            int stride, int index, const QPoint& at) const {
    const QPixmap source = scaled(bitmap_name);
    if (source.isNull()) return;
    const QRect region((first.x()) * scale_, (first.y() + index * stride) * scale_,
                       first.width() * scale_, first.height() * scale_);
    if (!source.rect().contains(region)) return;
    painter.drawPixmap(at.x() * scale_, at.y() * scale_, source, region.x(), region.y(),
                       region.width(), region.height());
}

int WinampSkin::text_width(const QString& text) const {
    return text.size() * winamp::kGlyphWidth;
}

// Versao recolorida do text.bmp: os pixels pretos viram transparentes e o
// resto vira a cor pedida. Fica em cache por cor — sao duas ou tres na janela
// inteira, e refazer a cada quadro custaria mais que guardar.
QPixmap WinampSkin::tinted_text(const QColor& ink) const {
    auto it = tinted_.constFind(ink.rgb());
    if (it != tinted_.constEnd()) return it.value();

    const QPixmap source = scaled(winamp::kTextBitmap);
    if (source.isNull()) return {};
    QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);

    // A cor de fundo vem da celula do ESPACO, que por definicao nao tem
    // desenho. Fixar preto funcionaria para o nosso skin, mas um .wsz de
    // terceiros pode ter o text.bmp sobre outra cor, e ai o recorte comeria a
    // letra em vez do fundo.
    QRgb key = qRgb(0, 0, 0);
    if (auto it = glyphs_.constFind(u' '); it != glyphs_.constEnd()) {
        const QPoint probe(it.value().x() * scale_, it.value().y() * scale_);
        if (image.rect().contains(probe)) key = image.pixel(probe) | 0xff000000u;
    }

    const QRgb opaque = qRgba(ink.red(), ink.green(), ink.blue(), 255);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            row[x] = ((row[x] | 0xff000000u) == key) ? qRgba(0, 0, 0, 0) : opaque;
    }
    const QPixmap result = QPixmap::fromImage(image);
    tinted_.insert(ink.rgb(), result);
    return result;
}

// Largura de tinta do glifo mais um pixel de vao, medida no proprio bitmap.
// A cor de fundo sai da celula do ESPACO, que por definicao nao tem desenho.
int WinampSkin::glyph_advance(QChar character) const {
    auto cached = advances_.constFind(character.unicode());
    if (cached != advances_.constEnd()) return cached.value();

    int advance = winamp::kGlyphWidth;
    const QPixmap source = bitmap(winamp::kTextBitmap);
    auto cell = glyphs_.constFind(character.unicode());
    if (cell == glyphs_.constEnd()) cell = glyphs_.constFind(u' ');
    if (!source.isNull() && cell != glyphs_.constEnd() &&
        source.rect().contains(cell.value())) {
        const QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
        QRgb key = qRgb(0, 0, 0);
        if (auto blank = glyphs_.constFind(u' '); blank != glyphs_.constEnd())
            key = image.pixel(blank.value().topLeft()) | 0xff000000u;

        int last = -1;
        for (int x = 0; x < cell.value().width(); ++x)
            for (int y = 0; y < cell.value().height(); ++y)
                if ((image.pixel(cell.value().x() + x, cell.value().y() + y) | 0xff000000u) != key) {
                    last = x;
                    break;
                }
        // Celula vazia (o espaco) vale quatro: menos que isso e as palavras
        // grudam, mais e o vao fica maior que uma letra.
        advance = last < 0 ? 4 : last + 2;
    }
    advances_.insert(character.unicode(), advance);
    return advance;
}

int WinampSkin::tight_text_width(const QString& text) const {
    int width = 0;
    for (QChar c : text) width += glyph_advance(c);
    return width;
}

void WinampSkin::draw_tight_text(QPainter& painter, const QString& text, const QPoint& at,
                                 const QColor& ink) const {
    const QPixmap source = tinted_text(ink);
    if (source.isNull()) return;
    int x = at.x();
    for (QChar c : text) {
        auto it = glyphs_.constFind(c.unicode());
        if (it == glyphs_.constEnd()) it = glyphs_.constFind(u' ');
        if (it != glyphs_.constEnd()) {
            const QRect cell(it.value().x() * scale_, it.value().y() * scale_,
                             it.value().width() * scale_, it.value().height() * scale_);
            if (source.rect().contains(cell))
                painter.drawPixmap(x * scale_, at.y() * scale_, source, cell.x(), cell.y(),
                                   cell.width(), cell.height());
        }
        x += glyph_advance(c);
    }
}

void WinampSkin::draw_text(QPainter& painter, const QString& text, const QPoint& at,
                           const QColor& ink) const {
    const QPixmap source = tinted_text(ink);
    if (source.isNull()) return;
    for (int i = 0; i < text.size(); ++i) {
        auto it = glyphs_.constFind(text.at(i).unicode());
        if (it == glyphs_.constEnd()) it = glyphs_.constFind(u' ');
        if (it == glyphs_.constEnd()) continue;
        const QRect cell(it.value().x() * scale_, it.value().y() * scale_,
                         it.value().width() * scale_, it.value().height() * scale_);
        if (!source.rect().contains(cell)) continue;
        painter.drawPixmap((at.x() + i * winamp::kGlyphWidth) * scale_, at.y() * scale_, source,
                           cell.x(), cell.y(), cell.width(), cell.height());
    }
}

void WinampSkin::draw_text(QPainter& painter, const QString& text, const QPoint& at) const {
    const QPixmap source = scaled(winamp::kTextBitmap);
    if (source.isNull()) return;

    for (int i = 0; i < text.size(); ++i) {
        auto it = glyphs_.constFind(text.at(i).unicode());
        if (it == glyphs_.constEnd()) it = glyphs_.constFind(u' ');
        if (it == glyphs_.constEnd()) continue;
        const QRect cell(it.value().x() * scale_, it.value().y() * scale_,
                         it.value().width() * scale_, it.value().height() * scale_);
        if (!source.rect().contains(cell)) continue;
        painter.drawPixmap((at.x() + i * winamp::kGlyphWidth) * scale_, at.y() * scale_, source,
                           cell.x(), cell.y(), cell.width(), cell.height());
    }
}

QStringList WinampSkin::missing() const {
    QStringList absent;
    for (const char* name : winamp::kBitmaps)
        if (!bitmaps_.contains(QLatin1String(name))) absent << QLatin1String(name);
    return absent;
}

}  // namespace pang::ui::skin
