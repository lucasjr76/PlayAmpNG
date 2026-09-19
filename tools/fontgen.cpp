// Extrai a fonte 5x6 do formato a partir da Silkscreen.
//
// Existe porque a tabela de glifos escrita a mao errou tres vezes — a ultima
// deixava o travessao do 'A' com 4 px, e "PLAYAMPNG" saia "PLRYRMPNG". Forma
// de letra nao da para validar por programa; o jeito de nao errar e nao
// desenhar. A saida vai para make_wsz.py.
#include <QGuiApplication>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <cstdio>

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const int id = QFontDatabase::addApplicationFont(QStringLiteral(FONT_PATH));
    if (id < 0) { std::fprintf(stderr, "fonte nao carregou\n"); return 1; }
    QFont font(QFontDatabase::applicationFontFamilies(id).value(0));
    font.setPixelSize(8);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setStyleStrategy(QFont::NoAntialias);

    // Janela da celula, medida: a caixa-alta ocupa as linhas 3..7 abaixo da
    // origem e a coluna 1..5; a sexta linha da celula guarda a descida.
    const int kOriginX = 8, kOriginY = 8;
    const int kCellX = kOriginX + 1, kCellY = kOriginY + 3;
    const QString chars =
        QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ\"@ 0123456789.:()-'!_+\\/[]^&%,=$#?*");

    QFontMetrics fm(font);
    int clipped = 0;
    std::printf("GLYPHS = {\n");
    int column = 0;
    for (QChar c : chars) {
        QImage img(24, 24, QImage::Format_ARGB32);
        img.fill(Qt::black);
        QPainter p(&img);
        p.setFont(font);
        p.setPen(Qt::white);
        p.drawText(kOriginX, kOriginY + fm.ascent(), QString(c));
        p.end();

        // Onde a tinta deste glifo comeca. Tres sinais da Silkscreen ('^', '&'
        // e '$') sao de 7 px e sobem uma linha acima da caixa-alta; para eles a
        // janela sobe junto, senao perdem o topo.
        int ink_top = 99, ink_bottom = -1;
        for (int y = 0; y < 24; ++y)
            for (int x = 0; x < 24; ++x)
                if (qRed(img.pixel(x, y)) > 127) {
                    ink_top = qMin(ink_top, y);
                    ink_bottom = qMax(ink_bottom, y);
                }
        const int shift = ink_top < kCellY ? ink_top - kCellY : 0;
        const int cell_y = kCellY + shift;

        QString bits;
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 5; ++x)
                bits += qRed(img.pixel(kCellX + x, cell_y + y)) > 127 ? QLatin1Char('#')
                                                                     : QLatin1Char('.');
        // Tinta fora da janela e glifo cortado: precisa aparecer, nao passar.
        for (int y = 0; y < 24; ++y)
            for (int x = 0; x < 24; ++x)
                if (qRed(img.pixel(x, y)) > 127 &&
                    (x < kCellX || x >= kCellX + 5 || y < cell_y || y >= cell_y + 6)) {
                    std::fprintf(stderr, "CORTADO '%s' em (%d,%d)\n", qPrintable(QString(c)),
                                 x - kCellX, y - cell_y);
                    ++clipped;
                    y = 24; break;
                }

        QString key = c == QLatin1Char('\\')  ? QStringLiteral("'\\\\'")
                      : c == QLatin1Char('\'') ? QStringLiteral("\"'\"")
                                               : QStringLiteral("'%1'").arg(c);
        std::printf(" %s:\"%s\",", qPrintable(key), qPrintable(bits));
        if (++column % 2 == 0) std::printf("\n");
    }
    if (column % 2) std::printf("\n");
    std::printf("}\n");
    std::fprintf(stderr, "%d glifo(s) cortado(s)\n", clipped);
    return clipped == 0 ? 0 : 1;
}
