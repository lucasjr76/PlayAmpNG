// Carregador de skin no formato do Winamp 2.x.
//
// Verifica os dois caminhos — .wsz compactado e pasta com os bitmaps soltos —
// e que as pecas saem com a geometria que o formato define. E essa geometria
// que faz as proporcoes baterem; ela e especificacao, nao gosto.

#include <QGuiApplication>
#include <QPixmap>

#include <cstdio>

#include "core/util/check.h"
#include "ui/skin/winamp_skin.h"

using namespace pang::ui::skin;

namespace {

void check_geometry(const WinampSkin& skin, const char* origin) {
    const auto label = [origin](const char* what) {
        return std::string(what) + " (" + origin + ")";
    };

    PANG_CHECK(skin.is_loaded(), label("skin carregado"));

    // Fundo da janela: a dimensao do formato, nao uma escolha nossa.
    const QPixmap main = skin.sprite(winamp::kMainBackground);
    PANG_CHECK(main.size() == QSize(275, 116), label("main.bmp tem 275x116"));

    // Botoes de transporte: 23x18, e o eject menor.
    for (const auto& piece : {winamp::kPreviousNormal, winamp::kPlayNormal,
                              winamp::kPauseNormal, winamp::kStopNormal, winamp::kNextNormal})
        PANG_CHECK(skin.sprite(piece).size() == QSize(23, 18),
                   label("botao de transporte tem 23x18"));
    PANG_CHECK(skin.sprite(winamp::kEjectNormal).size() == QSize(22, 16),
               label("eject tem 22x16"));
    PANG_CHECK(skin.sprite(winamp::kPlayPressed).size() == QSize(23, 18),
               label("estado pressionado existe e tem o mesmo tamanho"));

    // Barra de titulo e botoes da janela.
    PANG_CHECK(skin.sprite(winamp::kTitlebarActive).size() == QSize(275, 14),
               label("barra de titulo ativa tem 275x14"));
    PANG_CHECK(skin.sprite(winamp::kCloseNormal).size() == QSize(9, 9),
               label("botao de fechar tem 9x9"));

    // Cursor e fundos do volume: o quadro escolhido representa o VALOR.
    PANG_CHECK(skin.sprite(winamp::kVolumeThumbNormal).size() == QSize(14, 11),
               label("cursor de volume tem 14x11"));
    const QPixmap quiet = skin.frame("volume", QRect(0, 0, 68, 13), 15, 0);
    const QPixmap loud = skin.frame("volume", QRect(0, 0, 68, 13), 15, 27);
    PANG_CHECK(quiet.size() == QSize(68, 13) && loud.size() == QSize(68, 13),
               label("fundos de volume tem 68x13"));

    // E precisam ser DIFERENTES entre si: e a cor que diz o nivel.
    PANG_CHECK(quiet.toImage() != loud.toImage(),
               label("o fundo do volume muda com o nivel"));

    // Barra de posicao: fundo de 248 e cursor de 29.
    PANG_CHECK(skin.sprite(winamp::kPositionBackground).size() == QSize(248, 10),
               label("fundo da barra de posicao tem 248x10"));
    PANG_CHECK(skin.sprite(winamp::kPositionThumbNormal).size() == QSize(29, 10),
               label("cursor da barra de posicao tem 29x10"));

    // Shuffle e repeat, ligado e desligado.
    PANG_CHECK(skin.sprite(winamp::kShuffleOff).size() == QSize(47, 15),
               label("shuffle tem 47x15"));
    PANG_CHECK(skin.sprite(winamp::kRepeatOn).size() == QSize(28, 15),
               label("repeat tem 28x15"));
    PANG_CHECK(skin.sprite(winamp::kEqualizerOff).size() == QSize(23, 12),
               label("botao do equalizador tem 23x12"));

    // Fonte de 5x6, com minusculas reusando o desenho das maiusculas.
    PANG_CHECK(skin.glyph(u'A').size() == QSize(5, 6), label("glifo tem 5x6"));
    PANG_CHECK(skin.glyph(u'a').toImage() == skin.glyph(u'A').toImage(),
               label("minuscula reusa o desenho da maiuscula"));
    PANG_CHECK(skin.glyph(u'0').toImage() != skin.glyph(u'1').toImage(),
               label("digitos diferentes tem desenhos diferentes"));

    // Caractere fora do mapa cai no espaco, sem devolver peca vazia.
    PANG_CHECK(!skin.glyph(u'ç').isNull(),
               label("caractere desconhecido cai no espaco em vez de sumir"));

    // Digitos do mostrador de tempo.
    PANG_CHECK(skin.frame("numbers", QRect(0, 0, 9, 13), 0, 0).size() == QSize(9, 13),
               label("digito do mostrador tem 9x13"));

    // Equalizador.
    PANG_CHECK(skin.sprite(winamp::kEqualizerBackground).size() == QSize(275, 116),
               label("fundo do equalizador tem 275x116"));
    PANG_CHECK(skin.sprite(winamp::kEqualizerThumbNormal).size() == QSize(11, 11),
               label("cursor do equalizador tem 11x11"));

    // Cores da visualizacao: 24 entradas, sendo 2..17 o degrade do espectro.
    PANG_CHECK(skin.visualization_colors().size() >= 18,
               label("viscolor.txt traz ao menos 18 cores"));
    if (skin.visualization_colors().size() >= 18) {
        const QColor top = skin.visualization_colors()[2];
        const QColor base = skin.visualization_colors()[17];
        PANG_CHECK(top != base, label("o degrade do espectro tem topo e base diferentes"));
    }
}

}  // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    const QString directory = QStringLiteral(PLAYAMPNG_SKIN_DIR "/default");
    const QString archive = QStringLiteral(PLAYAMPNG_SKIN_DIR "/default.wsz");

    WinampSkin from_directory;
    QString error;
    PANG_CHECK(from_directory.load(directory, error),
               "carrega skin de uma pasta com os bitmaps soltos");
    if (from_directory.is_loaded()) check_geometry(from_directory, "pasta");

    WinampSkin from_archive;
    PANG_CHECK(from_archive.load(archive, error), "carrega skin de um .wsz");
    if (from_archive.is_loaded()) check_geometry(from_archive, "wsz");

    // Os dois caminhos tem de produzir exatamente a mesma coisa.
    if (from_directory.is_loaded() && from_archive.is_loaded())
        PANG_CHECK(from_directory.sprite(winamp::kMainBackground).toImage() ==
                       from_archive.sprite(winamp::kMainBackground).toImage(),
                   "pasta e .wsz produzem bitmaps identicos");

    // Caminho inexistente falha com mensagem, sem travar.
    WinampSkin missing;
    QString missing_error;
    PANG_CHECK(!missing.load(QStringLiteral("/caminho/que/nao/existe.wsz"), missing_error),
               "skin inexistente falha em vez de carregar vazio");
    PANG_CHECK(!missing_error.isEmpty(), "a falha traz mensagem de diagnostico");

    std::printf("  bitmaps ausentes no skin padrao: %s\n",
                from_directory.missing().isEmpty()
                    ? "nenhum dos obrigatorios"
                    : qPrintable(from_directory.missing().join(QStringLiteral(", "))));

    return pang::check::exit_code();
}
