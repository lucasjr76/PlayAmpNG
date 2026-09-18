#pragma once

#include <QColor>
#include <QHash>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QVector>

class QPainter;

namespace pang::ui::skin {

// Atlas de sprites e fonte bitmap.
//
// Toda coordenada de desenho e LOGICA, na escala 1x de 275x116. O atlas
// multiplica pela escala corrente na hora de blitar, entao o codigo dos paineis
// nunca lida com escala.
//
// AP-12 — so escala inteira, com vizinho mais proximo. O atlas inteiro e
// reescalado uma vez a cada troca de escala; blitar dele fica 1:1, sem
// reamostragem por sprite e sem borrao.
class Atlas {
public:
    bool load(const QString& directory, QString& error);
    bool is_loaded() const { return !scaled_.isNull(); }

    void set_scale(int scale);
    int scale() const { return scale_; }

    bool has(const QString& name) const { return sprites_.contains(name); }
    QSize sprite_size(const QString& name) const;

    void draw(QPainter& painter, const QString& name, int x, int y) const;

    // Preenche uma area repetindo o sprite: molduras e trilhos de qualquer
    // tamanho saem de uma peca so.
    void draw_tiled(QPainter& painter, const QString& name, const QRect& area) const;

    // --- texto.
    //
    // Os glifos vem da Silkscreen (SIL Open Font License), uma fonte de pixel
    // desenhada para 8 px, e nao mais de um desenho proprio glifo a glifo. Sao
    // rasterizados UMA vez na carga, em 1x e sem suavizacao, para um atlas
    // proprio; dai em diante o desenho e blit, igual aos demais sprites.
    //
    // Desenhar 60 glifos a mao foi trabalho jogado fora: o resultado era
    // grosseiro e existem fontes de pixel prontas e bem desenhadas.
    int glyph_height() const { return text_height_; }
    int text_width(const QString& text) const;
    void draw_text(QPainter& painter, const QString& text, int x, int y,
                   const QColor& tint = QColor()) const;

    // --- digitos do mostrador de tempo
    //
    // A largura varia por caractere: o dois-pontos e mais estreito que um
    // digito, como no classico. draw_time avanca pela largura real de cada
    // peca, e time_width devolve o total.
    int digit_height() const { return digit_height_; }
    int time_width(const QString& text) const;
    void draw_time(QPainter& painter, const QString& text, int x, int y) const;

    QColor color(const QString& key) const;

    // Degrade vertical do espectro, da base para o topo.
    const QVector<QColor>& spectrum() const { return spectrum_; }
    QColor peak_color() const { return peak_; }

private:
    void rescale();
    void draw_raw(QPainter& painter, const QString& name, int x, int y,
                  const QColor& tint) const;

    bool build_text_atlas(const QString& directory, QString& error);

    struct Glyph {
        QRect cell;     // no atlas de texto, em 1x
        int advance = 0;
    };

    QPixmap source_;
    QPixmap scaled_;
    QPixmap text_source_;
    QPixmap text_scaled_;
    QHash<char16_t, Glyph> glyphs_;
    int text_height_ = 5;
    QHash<QString, QRect> sprites_;
    QHash<QString, QColor> palette_;
    int scale_ = 1;
    int digit_height_ = 13;
    QVector<QColor> spectrum_;
    QColor peak_{200, 200, 200};
};

}  // namespace pang::ui::skin
