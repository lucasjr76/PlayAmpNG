// Verifica a diagramacao das janelas peca a peca, e imprime a grade.
//
// Existe porque corrigir posicao olhando a tela nao converge: cada ajuste
// soltava outro elemento, e a mesma janela acumulou sobreposicao, margem
// desigual e vao zero em rodadas seguidas. As regras abaixo sao a grade, e o
// teste reprova quem sair dela — nao ha como um ajuste local passar sem que o
// resto seja conferido junto.
//
// A saida lista cada fileira com as pecas em ordem e o vao entre elas, de modo
// que o relatorio serve tanto de verificacao quanto de desenho da janela.

#include <QPoint>
#include <QRect>
#include <QSize>

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

#include "core/util/check.h"
#include "ui/skin/winamp_layout.h"

namespace wa = pang::ui::skin::winamp;

namespace {

// A grade. Duas medidas, e tudo o mais decorre delas.
constexpr int kMargin = 14;
constexpr int kMinimumGap = 3;
constexpr int kWindowWidth = 275;
constexpr int kContentLeft = kMargin;
constexpr int kContentRight = kWindowWidth - 1 - kMargin;

struct Piece {
    std::string name;
    QRect area;
};

int right(const QRect& r) { return r.x() + r.width() - 1; }
int bottom(const QRect& r) { return r.y() + r.height() - 1; }

QRect placed(const QPoint& at, const wa::Sprite& sprite) {
    return QRect(at, sprite.source.size());
}

// Uma fileira: pecas que dividem a mesma faixa vertical. Sao elas que precisam
// caber lado a lado sem se tocar.
void row(const char* label, std::vector<Piece> pieces, bool touching_pair = false) {
    std::sort(pieces.begin(), pieces.end(),
              [](const Piece& a, const Piece& b) { return a.area.x() < b.area.x(); });

    std::printf("  %s\n", label);
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        const QRect& r = pieces[i].area;
        std::printf("    %-22s %3d..%-3d  (%3d px)", pieces[i].name.c_str(), r.x(), right(r),
                    r.width());
        if (i + 1 < pieces.size()) {
            const int gap = pieces[i + 1].area.x() - right(r) - 1;
            std::printf("   vao %d", gap);
            // Um par colado e explicito: EQ e PL, e aleatorio e repetir, sao
            // desenhados encostados no formato e assim devem ficar.
            const bool pair_allowed = touching_pair && gap >= 0 && gap <= 1;
            if (!pair_allowed)
                PANG_CHECK(gap >= kMinimumGap,
                           (pieces[i].name + " e " + pieces[i + 1].name + ": vao insuficiente")
                               .c_str());
            else
                PANG_CHECK(gap >= 0,
                           (pieces[i].name + " e " + pieces[i + 1].name + ": sobrepostos").c_str());
        }
        std::printf("\n");

        PANG_CHECK(r.x() >= kContentLeft, (pieces[i].name + " fura a margem esquerda").c_str());
        PANG_CHECK(right(r) <= kContentRight,
                   (pieces[i].name + " fura a margem direita").c_str());
    }
}

// Duas pecas que nao dividem fileira nao podem, ainda assim, se cruzar.
void no_overlap(const std::vector<Piece>& pieces) {
    for (std::size_t i = 0; i < pieces.size(); ++i)
        for (std::size_t j = i + 1; j < pieces.size(); ++j)
            PANG_CHECK(!pieces[i].area.intersects(pieces[j].area),
                       (pieces[i].name + " cruza " + pieces[j].name).c_str());
}

void main_window() {
    std::printf("\nJANELA PRINCIPAL  (margem %d, conteudo %d..%d)\n", kMargin, kContentLeft,
                kContentRight);

    const QRect display(14, 23, 85, 39);      // poco do mostrador
    const QRect title(103, 23, 158, 14);      // poco do titulo da faixa

    row("barra de titulo", {{"minimizar", placed(wa::kMinimizeAt, wa::kMinimizeNormal)},
                            {"modo barra", placed(wa::kShadeAt, wa::kShadeNormal)},
                            {"fechar", placed(wa::kCloseAt, wa::kCloseNormal)}},
        // Os tres botoes da janela sao um grupo: encostam de proposito.
        /*touching_pair=*/true);

    row("mostrador e titulo", {{"poco do mostrador", display}, {"poco do titulo", title}});

    row("informacao da faixa", {{"bitrate", wa::kBitrate},
                                {"taxa", wa::kSampleRate},
                                {"mono", placed(wa::kMonoAt, wa::kMonoOn)},
                                {"estereo", placed(wa::kStereoAt, wa::kStereoOn)}},
        /*touching_pair=*/true);

    row("controles", {{"volume", QRect(wa::kVolumeAt, wa::kVolumeSize)},
                      {"balanco", QRect(wa::kBalanceAt, wa::kBalanceSize)},
                      {"botao EQ", placed(wa::kEqualizerAt, wa::kEqualizerOff)},
                      {"botao PL", placed(wa::kPlaylistAt, wa::kPlaylistOff)}},
        /*touching_pair=*/true);

    row("barra de posicao", {{"posicao", placed(wa::kPositionAt, wa::kPositionBackground)}});

    row("transporte", {{"anterior", placed(wa::kPreviousAt, wa::kPreviousNormal)},
                       {"tocar", placed(wa::kPlayAt, wa::kPlayNormal)},
                       {"pausar", placed(wa::kPauseAt, wa::kPauseNormal)},
                       {"parar", placed(wa::kStopAt, wa::kStopNormal)},
                       {"proxima", placed(wa::kNextAt, wa::kNextNormal)},
                       {"ejetar", placed(wa::kEjectAt, wa::kEjectNormal)},
                       {"aleatorio", placed(wa::kShuffleAt, wa::kShuffleOff)},
                       {"repetir", placed(wa::kRepeatAt, wa::kRepeatOff)},
                       {"logotipo", QRect(241, 88, 20, 20)}},
        /*touching_pair=*/true);

    // O conteudo do mostrador precisa caber DENTRO do poco, centrado. Um dos
    // defeitos relatados foi exatamente este: o poco crescia e o conteudo
    // ficava encostado num lado.
    const QRect interior(display.x() + 1, display.y() + 1, display.width() - 2,
                         display.height() - 2);
    const QRect indicator = placed(wa::kIndicatorAt, wa::kIndicatorPlay);
    const QRect digits(wa::kTimeSignAt.x(), wa::kTimeSignAt.y(),
                       wa::kTimeSecondUnitsAt.x() + wa::kDigitWidth - wa::kTimeSignAt.x(),
                       wa::kDigitHeight);
    const QRect clock_block = indicator.united(digits);

    std::printf("  mostrador (interior %d..%d)\n", interior.x(), right(interior));
    for (const auto& [name, r] : std::initializer_list<std::pair<const char*, QRect>>{
             {"indicador + digitos", clock_block}, {"espectro", wa::kVisualization}}) {
        const int before = r.x() - interior.x();
        const int after = right(interior) - right(r);
        std::printf("    %-22s %3d..%-3d  folga %d / %d\n", name, r.x(), right(r), before, after);
        PANG_CHECK(interior.contains(r), (std::string(name) + " nao cabe no poco").c_str());
        // Meio pixel de diferenca e inevitavel quando a folga total e impar.
        PANG_CHECK(std::abs(before - after) <= 1,
                   (std::string(name) + " nao esta centrado no poco").c_str());
    }
    PANG_CHECK(!indicator.intersects(digits), "indicador encostado nos digitos");
    PANG_CHECK(digits.x() - right(indicator) - 1 >= 2, "indicador colado nos digitos");
    PANG_CHECK(title.contains(wa::kSongTitle), "titulo da faixa nao cabe no poco");

    no_overlap({{"poco do mostrador", display},
                {"poco do titulo", title},
                {"volume", QRect(wa::kVolumeAt, wa::kVolumeSize)},
                {"balanco", QRect(wa::kBalanceAt, wa::kBalanceSize)},
                {"botao EQ", placed(wa::kEqualizerAt, wa::kEqualizerOff)},
                {"botao PL", placed(wa::kPlaylistAt, wa::kPlaylistOff)},
                {"posicao", placed(wa::kPositionAt, wa::kPositionBackground)},
                {"transporte", QRect(wa::kPreviousAt, QSize(23 * 5, 18))},
                {"logotipo", QRect(241, 88, 20, 20)}});

    // Arestas que precisam bater entre fileiras diferentes.
    std::printf("  arestas\n");
    const std::set<int> left{display.x(), wa::kPositionAt.x(), wa::kPreviousAt.x()};
    const std::set<int> right_edges{right(title), right(placed(wa::kPlaylistAt, wa::kPlaylistOff)),
                                    right(placed(wa::kPositionAt, wa::kPositionBackground)),
                                    260};
    std::printf("    esquerdas: ");
    for (int e : left) std::printf("%d ", e);
    std::printf("  direitas: ");
    for (int e : right_edges) std::printf("%d ", e);
    std::printf("\n");
    PANG_CHECK(left.size() == 1, "mostrador, barra de posicao e transporte com margem diferente");
    PANG_CHECK(right_edges.size() == 1, "titulo, PL, posicao e logotipo com aresta direita diferente");
}

void equalizer_window() {
    std::printf("\nJANELA DO EQUALIZADOR  (margem %d, conteudo %d..%d)\n", kMargin, kContentLeft,
                kContentRight);

    row("botoes", {{"ligar", QRect(wa::kEqOnAt, QSize(26, 12))},
                   {"restaurar", QRect(wa::kEqAutoAt, QSize(32, 12))},
                   {"predefinicoes", QRect(wa::kEqPresetsAt, QSize(44, 12))}});

    std::vector<Piece> sliders{{"preamp", QRect(wa::kPreampAt, wa::kEqSliderSize)}};
    for (int band = 0; band < 10; ++band)
        sliders.push_back({"banda " + std::to_string(band),
                           QRect(wa::band_at(band), wa::kEqSliderSize)});
    row("sliders", sliders);

    // O traco de fechamento depois da ultima banda e a peca mais a direita da
    // janela e precisa parar na mesma coluna que o botao de predefinicoes.
    const int closing_tick = wa::kBandsOrigin.x() - 4 + 10 * wa::kBandSpacing + 2;
    std::printf("  traco de fechamento termina em %d; predefinicoes em %d\n", closing_tick,
                wa::kEqPresetsAt.x() + 43);
    PANG_CHECK(closing_tick == kContentRight, "traco de fechamento fora da margem");
    PANG_CHECK(wa::kEqPresetsAt.x() + 43 == kContentRight, "predefinicoes fora da margem");
    PANG_CHECK(wa::kEqOnAt.x() == kContentLeft, "botao ligar fora da margem");

    // O mostrador da curva fica centrado na janela.
    const int centre = (wa::kEqCurve.x() + right(wa::kEqCurve)) / 2;
    std::printf("  curva %d..%d, centro %d (janela %d)\n", wa::kEqCurve.x(), right(wa::kEqCurve),
                centre, (kWindowWidth - 1) / 2);
    PANG_CHECK(std::abs(centre - (kWindowWidth - 1) / 2) <= 1, "curva fora do centro da janela");
}

void frame_strips() {
    PANG_CHECK(wa::kVolumeFrameStride >= wa::kVolumeSize.height(),
               "passo dos quadros de volume cabe a altura");
    PANG_CHECK(wa::kVolumeFrames == wa::kBalanceFrames,
               "volume e balanco com o mesmo numero de quadros");
    PANG_CHECK(wa::kEqSliderFrames == wa::kVolumeFrames,
               "sliders do EQ com o mesmo numero de quadros");
}

}  // namespace

int main() {
    main_window();
    equalizer_window();
    frame_strips();
    std::printf("\n");
    return pang::check::exit_code();
}
