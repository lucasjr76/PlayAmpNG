#include "ui/skin/winamp_skin.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

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

QStringList WinampSkin::missing() const {
    QStringList absent;
    for (const char* name : winamp::kBitmaps)
        if (!bitmaps_.contains(QLatin1String(name))) absent << QLatin1String(name);
    return absent;
}

}  // namespace pang::ui::skin
