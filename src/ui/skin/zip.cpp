#include "ui/skin/zip.h"

#include <QFile>
#include <QFileInfo>

#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace pang::ui::skin {
namespace {

constexpr quint32 kEndOfCentralDirectory = 0x06054b50;
constexpr quint32 kCentralFileHeader = 0x02014b50;
constexpr quint32 kLocalFileHeader = 0x04034b50;

quint16 read16(const QByteArray& data, int at) {
    if (at < 0 || at + 2 > data.size()) return 0;
    return static_cast<quint8>(data[at]) | (static_cast<quint8>(data[at + 1]) << 8);
}

quint32 read32(const QByteArray& data, int at) {
    if (at < 0 || at + 4 > data.size()) return 0;
    return static_cast<quint32>(static_cast<quint8>(data[at])) |
           (static_cast<quint32>(static_cast<quint8>(data[at + 1])) << 8) |
           (static_cast<quint32>(static_cast<quint8>(data[at + 2])) << 16) |
           (static_cast<quint32>(static_cast<quint8>(data[at + 3])) << 24);
}

}  // namespace

bool Zip::open(const QString& path, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("nao foi possivel abrir %1").arg(path);
        return false;
    }
    archive_ = file.readAll();
    file.close();

    if (archive_.size() < 22) {
        error = QStringLiteral("%1 e pequeno demais para ser um ZIP").arg(path);
        return false;
    }

    // O fim do diretorio central fica no fim do arquivo, possivelmente seguido
    // de um comentario de ate 64 KiB. Procura de tras para frente.
    int end = -1;
    const int limit = static_cast<int>(std::max<qsizetype>(0, archive_.size() - 22 - 65535));
    for (int at = static_cast<int>(archive_.size()) - 22; at >= limit; --at)
        if (read32(archive_, at) == kEndOfCentralDirectory) {
            end = at;
            break;
        }
    if (end < 0) {
        error = QStringLiteral("%1 nao tem diretorio central de ZIP").arg(path);
        return false;
    }

    const quint16 count = read16(archive_, end + 10);
    int at = static_cast<int>(read32(archive_, end + 16));

    entries_.clear();
    for (quint16 i = 0; i < count; ++i) {
        if (read32(archive_, at) != kCentralFileHeader) {
            error = QStringLiteral("entrada %1 do diretorio central esta corrompida").arg(i);
            return false;
        }
        Entry entry;
        entry.method = read16(archive_, at + 10);
        entry.compressed = read32(archive_, at + 20);
        entry.uncompressed = read32(archive_, at + 24);
        entry.header_offset = read32(archive_, at + 42);

        const quint16 name_length = read16(archive_, at + 28);
        const quint16 extra_length = read16(archive_, at + 30);
        const quint16 comment_length = read16(archive_, at + 32);
        const QString name = QString::fromUtf8(archive_.mid(at + 46, name_length));

        // Skins guardam os bitmaps na raiz ou dentro de uma pasta; o que
        // importa e o nome do arquivo. A comparacao e em minusculas porque o
        // formato nao e consistente entre skins.
        const QString key = QFileInfo(name).fileName().toLower();
        if (!key.isEmpty()) entries_.insert(key, entry);

        at += 46 + name_length + extra_length + comment_length;
    }
    return true;
}

QByteArray Zip::read(const QString& name) const {
    const auto it = entries_.constFind(name.toLower());
    if (it == entries_.constEnd()) return {};
    const Entry& entry = it.value();

    if (read32(archive_, static_cast<int>(entry.header_offset)) != kLocalFileHeader) return {};

    // O cabecalho local repete tamanhos de nome e extra, que podem diferir dos
    // do diretorio central — por isso sao lidos daqui.
    const int base = static_cast<int>(entry.header_offset);
    const quint16 name_length = read16(archive_, base + 26);
    const quint16 extra_length = read16(archive_, base + 28);
    const int data_at = base + 30 + name_length + extra_length;
    if (data_at + static_cast<int>(entry.compressed) > archive_.size()) return {};

    const QByteArray payload = archive_.mid(data_at, static_cast<int>(entry.compressed));
    if (entry.method == 0) return payload;
    if (entry.method != 8) return {};

    QByteArray out;
    out.resize(static_cast<int>(entry.uncompressed));

    z_stream stream{};
    // Janela negativa: deflate cru, sem cabecalho zlib — que e como o ZIP
    // guarda os dados.
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return {};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(payload.constData()));
    stream.avail_in = static_cast<uInt>(payload.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());

    const int status = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (status != Z_STREAM_END) return {};
    return out;
}

}  // namespace pang::ui::skin
