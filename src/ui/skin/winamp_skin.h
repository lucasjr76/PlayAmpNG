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

    // ------------------------------------------------------------ desenho
    //
    // AP-12 — escala inteira, vizinho mais proximo. Os bitmaps sao reescalados
    // UMA vez por troca de escala; desenhar e blit 1:1 a partir dai. Toda
    // coordenada de desenho e logica, na grade de 275x116 do formato.
    void set_scale(int scale);
    int scale() const { return scale_; }

    void draw(QPainter& painter, const winamp::Sprite& sprite, const QPoint& at) const;
    void draw_frame(QPainter& painter, const char* bitmap, const QRect& first, int stride,
                    int index, const QPoint& at) const;

    // Texto na fonte de 5x6 do skin.
    int text_width(const QString& text) const;
    void draw_text(QPainter& painter, const QString& text, const QPoint& at) const;

    // Texto recortado: desenha so os pixels acesos do glifo, na cor pedida.
    //
    // O text.bmp do formato tem fundo preto, entao desenhar sobre a face da
    // janela levava junto uma caixa escura atras de cada palavra. A referencia
    // nao tem essas caixas: os rotulos assentam direto no painel. Recortar pela
    // luminancia do glifo resolve sem exigir um bitmap por cor.
    void draw_text(QPainter& painter, const QString& text, const QPoint& at,
                   const QColor& ink) const;

    // Texto com avanco PROPORCIONAL, para os rotulos da nossa interface.
    //
    // O formato avanca 5 px fixos por glifo. Como a tinta varia de 3 a 5 px,
    // "DIR" sai com um buraco depois do I e "IMP" com o M colado no P. Para o
    // titulo da faixa e para as linhas da playlist o avanco fixo tem de ficar
    // — e o que um .wsz espera —, mas rotulo de botao nosso pode respirar.
    int tight_text_width(const QString& text) const;
    void draw_tight_text(QPainter& painter, const QString& text, const QPoint& at,
                         const QColor& ink) const;

private:
    QPixmap scaled(const char* name) const;

    bool load_from_directory(const QString& path, QString& error);
    bool load_from_archive(const QString& path, QString& error);
    void index_glyphs();
    QPixmap tinted_text(const QColor& ink) const;
    int glyph_advance(QChar character) const;
    void parse_viscolor(const QByteArray& text);

    QHash<QString, QPixmap> bitmaps_;
    mutable QHash<QString, QPixmap> scaled_;
    int scale_ = 1;
    QHash<char16_t, QRect> glyphs_;
    mutable QHash<QRgb, QPixmap> tinted_;
    mutable QHash<char16_t, int> advances_;
    QVector<QColor> viscolor_;
    QString name_;
};

}  // namespace pang::ui::skin
