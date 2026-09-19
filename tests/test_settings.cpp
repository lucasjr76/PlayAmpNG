// Ida e volta da configuracao.
//
// Gravar e ler sao dois codigos espelhados, escritos em lugares diferentes do
// arquivo. E a forma classica de divergencia: acrescenta-se o campo na
// gravacao e esquece-se da leitura, e a opcao simplesmente nao persiste sem
// que nada acuse. Este projeto ja teve um defeito dessa exata forma em
// probe/decoder.
//
// O teste escreve num diretorio temporario proprio — apontar XDG_CONFIG_HOME
// para ele impede que a suite mexa na configuracao real do usuario.

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

#include "core/util/check.h"
#include "ui/settings.h"

int main(int argc, char** argv) {
    QTemporaryDir home;
    PANG_CHECK(home.isValid(), "diretorio temporario criado");
    qputenv("XDG_CONFIG_HOME", home.path().toLocal8Bit());

    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PlayAmpNG-teste"));
    app.setOrganizationName(QStringLiteral("PlayAmpNG-teste"));

    using pang::ui::settings::AppState;

    AppState written;
    written.volume = 0.42f;
    written.balance = -0.25f;
    written.shuffle = true;
    written.repeat = 2;
    written.replaygain_mode = 1;
    written.autoplay_on_restore = true;
    written.audio_device = "Placa de teste (analogico)";
    written.scale = 3;
    written.visualization = 1;
    written.playlist_visible = false;
    written.detached = true;
    written.compact = true;
    written.equalizer_visible = true;
    written.main_geometry = {10, 20, 275, 116};
    written.playlist_geometry = {10, 140, 275, 232};
    written.equalizer_geometry = {10, 380, 275, 116};

    PANG_CHECK(pang::ui::settings::save(written), "configuracao gravada");
    const AppState read = pang::ui::settings::load();

    PANG_CHECK(read.volume == written.volume, "volume persiste");
    PANG_CHECK(read.balance == written.balance, "balanco persiste");
    PANG_CHECK(read.shuffle == written.shuffle, "aleatorio persiste");
    PANG_CHECK(read.repeat == written.repeat, "repeticao persiste");
    PANG_CHECK(read.replaygain_mode == written.replaygain_mode, "modo de ReplayGain persiste");
    PANG_CHECK(read.autoplay_on_restore == written.autoplay_on_restore,
               "retomada automatica persiste");
    // AU-11 — o campo novo desta etapa.
    PANG_CHECK(read.audio_device == written.audio_device, "dispositivo de saida persiste");
    PANG_CHECK(read.scale == written.scale, "escala persiste");
    PANG_CHECK(read.visualization == written.visualization, "visualizacao persiste");
    PANG_CHECK(read.playlist_visible == written.playlist_visible, "playlist visivel persiste");
    PANG_CHECK(read.detached == written.detached, "paineis destacados persiste");
    PANG_CHECK(read.compact == written.compact, "modo compacto persiste");
    PANG_CHECK(read.equalizer_visible == written.equalizer_visible, "equalizador visivel persiste");
    PANG_CHECK(read.main_geometry == written.main_geometry, "geometria principal persiste");
    PANG_CHECK(read.playlist_geometry == written.playlist_geometry, "geometria da playlist persiste");
    PANG_CHECK(read.equalizer_geometry == written.equalizer_geometry,
               "geometria do equalizador persiste");

    // IN-10 — arquivo corrompido nao impede o programa de abrir.
    {
        const QString path = QDir(home.path()).filePath(
            QStringLiteral("PlayAmpNG-teste/PlayAmpNG-teste/config.json"));
        QFile bad(path);
        PANG_CHECK(bad.exists(), "o arquivo de configuracao foi criado onde se espera");
        if (bad.open(QIODevice::WriteOnly)) {
            bad.write("{ isto nao e json");
            bad.close();
            const AppState fallback = pang::ui::settings::load();
            PANG_CHECK(fallback.volume == AppState{}.volume,
                       "configuracao invalida cai nos padroes em vez de falhar");
            PANG_CHECK(QFile::exists(path + QStringLiteral(".bad")),
                       "o arquivo invalido e preservado como .bad");
        }
    }

    return pang::check::exit_code();
}
