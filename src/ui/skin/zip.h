#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

namespace pang::ui::skin {

// Leitor de ZIP minimo, so o necessario para abrir um .wsz.
//
// Um .wsz e um ZIP comum com bitmaps dentro. Escrever o leitor evita depender
// de minizip — que esta instalado nesta maquina, mas nao esta garantido no
// Windows nem no macOS — e de QZipReader, que e API privada do Qt e muda entre
// versoes. O zlib ja vem junto com o Qt, entao a descompressao sai de graca.
//
// Suporta os dois metodos que aparecem em ZIP de verdade: armazenado (0) e
// deflate (8). Nao suporta ZIP64, criptografia nem arquivos divididos — e um
// .wsz nao usa nada disso.
class Zip {
public:
    // Le o diretorio central. Em falha devolve false e preenche error.
    bool open(const QString& path, QString& error);

    // Nomes dos arquivos, em minusculas e sem diretorio.
    QStringList names() const { return entries_.keys(); }
    bool contains(const QString& name) const { return entries_.contains(name.toLower()); }

    // Conteudo descomprimido. Vazio quando o nome nao existe ou a entrada esta
    // corrompida.
    QByteArray read(const QString& name) const;

private:
    struct Entry {
        quint16 method = 0;
        quint32 compressed = 0;
        quint32 uncompressed = 0;
        quint32 header_offset = 0;
    };

    QByteArray archive_;
    QHash<QString, Entry> entries_;
};

}  // namespace pang::ui::skin
