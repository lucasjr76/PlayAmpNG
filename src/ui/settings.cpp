#include "ui/settings.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include "core/util/log.h"

namespace pang::ui::settings {
namespace {

QString config_path() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    return directory + QStringLiteral("/config.json");
}

QJsonArray bands_to_json(const std::array<float, core::dsp::Equalizer::kBands>& bands) {
    QJsonArray array;
    for (float value : bands) array.append(value);
    return array;
}

std::array<float, core::dsp::Equalizer::kBands> bands_from_json(const QJsonArray& array) {
    std::array<float, core::dsp::Equalizer::kBands> bands{};
    for (int i = 0; i < core::dsp::Equalizer::kBands && i < array.size(); ++i)
        bands[static_cast<std::size_t>(i)] = static_cast<float>(array[i].toDouble());
    return bands;
}

}  // namespace

bool save(const AppState& state) {
    QJsonObject eq;
    eq[QStringLiteral("bypass")] = state.eq.bypass;
    eq[QStringLiteral("preamp_db")] = state.eq.preamp_db;
    eq[QStringLiteral("bands")] = bands_to_json(state.eq.bands);

    QJsonArray presets;
    for (const core::dsp::EqPreset& preset : state.user_presets) {
        QJsonObject object;
        object[QStringLiteral("name")] = QString::fromStdString(preset.name);
        object[QStringLiteral("preamp_db")] = preset.preamp_db;
        object[QStringLiteral("bands")] = bands_to_json(preset.bands);
        presets.append(object);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("volume")] = state.volume;
    root[QStringLiteral("balance")] = state.balance;
    root[QStringLiteral("shuffle")] = state.shuffle;
    root[QStringLiteral("repeat")] = state.repeat;
    root[QStringLiteral("replaygain_mode")] = state.replaygain_mode;
    root[QStringLiteral("autoplay_on_restore")] = state.autoplay_on_restore;
    root[QStringLiteral("equalizer")] = eq;
    root[QStringLiteral("user_presets")] = presets;

    // QSaveFile grava em temporario e renomeia no commit: ou o arquivo antigo
    // permanece inteiro, ou o novo aparece inteiro. Nunca um meio-termo.
    QSaveFile file(config_path());
    if (!file.open(QIODevice::WriteOnly)) {
        core::log::error("nao foi possivel abrir a configuracao para gravar");
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        core::log::error("nao foi possivel concluir a gravacao da configuracao");
        return false;
    }
    return true;
}

AppState load() {
    AppState state;

    QFile file(config_path());
    if (!file.exists()) return state;
    if (!file.open(QIODevice::ReadOnly)) {
        core::log::warn("configuracao ilegivel; usando padroes");
        return state;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        // IN-10 — preserva o arquivo problematico para diagnostico em vez de
        // sobrescrever, carrega os padroes e segue.
        const QString bad = config_path() + QStringLiteral(".bad");
        QFile::remove(bad);
        QFile::rename(config_path(), bad);
        core::log::warn("configuracao invalida movida para config.json.bad; usando padroes");
        return state;
    }

    const QJsonObject root = document.object();
    state.volume = static_cast<float>(root[QStringLiteral("volume")].toDouble(1.0));
    state.balance = static_cast<float>(root[QStringLiteral("balance")].toDouble(0.0));
    state.shuffle = root[QStringLiteral("shuffle")].toBool(false);
    state.repeat = root[QStringLiteral("repeat")].toInt(0);
    state.replaygain_mode = root[QStringLiteral("replaygain_mode")].toInt(0);
    state.autoplay_on_restore = root[QStringLiteral("autoplay_on_restore")].toBool(false);

    const QJsonObject eq = root[QStringLiteral("equalizer")].toObject();
    state.eq.bypass = eq[QStringLiteral("bypass")].toBool(false);
    state.eq.preamp_db = static_cast<float>(eq[QStringLiteral("preamp_db")].toDouble(0.0));
    state.eq.bands = bands_from_json(eq[QStringLiteral("bands")].toArray());

    for (const QJsonValue& value : root[QStringLiteral("user_presets")].toArray()) {
        const QJsonObject object = value.toObject();
        core::dsp::EqPreset preset;
        preset.name = object[QStringLiteral("name")].toString().toStdString();
        preset.preamp_db = static_cast<float>(object[QStringLiteral("preamp_db")].toDouble(0.0));
        preset.bands = bands_from_json(object[QStringLiteral("bands")].toArray());
        if (!preset.name.empty()) state.user_presets.push_back(preset);
    }

    return state;
}

}  // namespace pang::ui::settings
