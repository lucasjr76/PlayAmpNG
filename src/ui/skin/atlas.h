#pragma once

#include <QColor>
#include <QHash>
#include <QPixmap>
#include <QRect>
#include <QString>

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

    // --- fonte bitmap 5x7. Minusculas viram maiusculas, como no classico.
    int glyph_width() const { return glyph_width_; }
    int glyph_height() const { return glyph_height_; }
    int text_width(const QString& text) const;
    void draw_text(QPainter& painter, const QString& text, int x, int y,
                   const QColor& tint = QColor()) const;

    // --- digitos 9x13 do mostrador de tempo
    int digit_width() const { return digit_width_; }
    int digit_height() const { return digit_height_; }
    void draw_time(QPainter& painter, const QString& text, int x, int y) const;

    QColor color(const QString& key) const;

private:
    void rescale();
    void draw_raw(QPainter& painter, const QString& name, int x, int y,
                  const QColor& tint) const;

    QPixmap source_;
    QPixmap scaled_;
    QHash<QString, QRect> sprites_;
    QHash<QString, QColor> palette_;
    int scale_ = 1;
    int glyph_width_ = 5;
    int glyph_height_ = 7;
    int digit_width_ = 9;
    int digit_height_ = 13;
};

}  // namespace pang::ui::skin
