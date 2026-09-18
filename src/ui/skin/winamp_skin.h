#pragma once

#include <QHash>
#include <QPixmap>
#include <QString>
#include <QVector>

#include "ui/skin/winamp_layout.h"

namespace pang::ui::skin {

// Carrega um skin no formato do Winamp 2.x, de um .wsz ou de uma pasta.
//
// Nao ha selecao de skin na interface, nem importacao, nem troca em tempo de
// execucao — a secao 2 da especificacao proibe isso. O que se adota aqui e o
// FORMATO: as coordenadas dele sao a especificacao que faz as proporcoes
// baterem, e o arquivo carregado e a aparencia fixa do player.
class WinampSkin {
public:
    // Aceita caminho de .wsz ou de pasta com os bitmaps soltos.
    bool load(const QString& path, QString& error);
    bool is_loaded() const { return !bitmaps_.isEmpty(); }
    QString name() const { return name_; }

    // Bitmap inteiro, pelo nome do formato ("main", "cbuttons", ...).
    QPixmap bitmap(const char* name) const;

    // Uma peca, recortada pela geometria do formato.
    QPixmap sprite(const winamp::Sprite& sprite) const;

    // Quadro de uma tira: usado nos fundos de volume, balanco e sliders do
    // equalizador, onde o quadro escolhido representa o VALOR.
    QPixmap frame(const char* bitmap, const QRect& first, int stride, int index) const;

    // Glifo da fonte de 5x6 de text.bmp. Caractere ausente devolve o espaco.
    QPixmap glyph(QChar character) const;

    // Cores da visualizacao, de viscolor.txt: 0 fundo, 1 grade, 2..17 degrade
    // do espectro (topo para a base), 18..23 osciloscopio.
    const QVector<QColor>& visualization_colors() const { return viscolor_; }

    // Bitmaps que o skin nao traz. Um skin incompleto e comum; quem desenha
    // decide o que fazer com a ausencia.
    QStringList missing() const;

private:
    bool load_from_directory(const QString& path, QString& error);
    bool load_from_archive(const QString& path, QString& error);
    void index_glyphs();
    void parse_viscolor(const QByteArray& text);

    QHash<QString, QPixmap> bitmaps_;
    QHash<char16_t, QRect> glyphs_;
    QVector<QColor> viscolor_;
    QString name_;
};

}  // namespace pang::ui::skin
