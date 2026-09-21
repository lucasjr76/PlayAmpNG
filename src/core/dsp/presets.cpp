#include "core/dsp/presets.h"

#include <algorithm>

namespace pang::core::dsp {

const std::vector<EqPreset>& builtin_presets() {
    // Ordem das bandas: 60, 170, 310, 600 Hz, 1, 3, 6, 12, 14, 16 kHz.
    static const std::vector<EqPreset> kPresets{
        {"Plano",            0.0f, {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0}},
        {"Rock",             0.0f, {  5,  3, -3, -5, -2,  2,  5,  7,  7,  7}},
        {"Pop",             -1.0f, { -2,  3,  5,  5,  3,  0, -2, -2, -2, -2}},
        {"Jazz",             0.0f, {  4,  3,  1,  2, -2, -2,  0,  1,  3,  4}},
        {"Classico",         0.0f, {  0,  0,  0,  0,  0,  0, -5, -5, -5, -7}},
        {"Grave forte",     -3.0f, {  8,  8,  8,  5,  1, -3, -6, -8, -8, -8}},
        {"Agudo forte",     -3.0f, { -8, -8, -8, -5,  1,  6,  9,  9,  9,  9}},
        {"Grave e agudo",   -3.0f, {  7,  6,  3, -4, -6, -4,  2,  7,  9,  9}},
        {"Dance",           -1.0f, {  6,  5,  2,  0,  0, -4, -5, -5,  0,  0}},
        {"Voz",              0.0f, { -3, -5, -3,  2,  5,  5,  3,  1,  0, -2}},
        {"Notebook",        -2.0f, {  5,  9,  7,  1, -2,  0,  3,  6,  8,  9}},
    };
    return kPresets;
}

void apply(Equalizer& equalizer, const EqState& state) {
    equalizer.set_preamp_db(state.preamp_db);
    for (int b = 0; b < Equalizer::kBands; ++b)
        equalizer.set_band_db(b, state.bands[static_cast<std::size_t>(b)]);
    equalizer.set_bypass(state.bypass);
}

EqState capture(const Equalizer& equalizer) {
    EqState state;
    state.bypass = equalizer.bypass();
    state.preamp_db = equalizer.preamp_db();
    for (int b = 0; b < Equalizer::kBands; ++b)
        state.bands[static_cast<std::size_t>(b)] = equalizer.band_db(b);
    return state;
}

EqState from_preset(const EqPreset& preset) {
    EqState state;
    state.preamp_db = preset.preamp_db;
    state.bands = preset.bands;
    return state;
}

namespace {

std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

// So ASCII: tolower com acentos depende da localidade da maquina, e um nome
// que colide numa maquina e nao em outra seria pior que nao ignorar a caixa.
bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        const auto lx = (x >= 'A' && x <= 'Z') ? x + 32 : x;
        const auto ly = (y >= 'A' && y <= 'Z') ? y + 32 : y;
        if (lx != ly) return false;
    }
    return true;
}

}  // namespace

SaveResult save_user_preset(std::vector<EqPreset>& presets, EqPreset preset) {
    preset.name = trimmed(preset.name);
    if (preset.name.empty()) return SaveResult::EmptyName;

    for (const EqPreset& builtin : builtin_presets())
        if (same_name(builtin.name, preset.name)) return SaveResult::BuiltinName;

    for (EqPreset& existing : presets)
        if (same_name(existing.name, preset.name)) {
            existing = std::move(preset);
            return SaveResult::Replaced;
        }

    presets.push_back(std::move(preset));
    return SaveResult::Added;
}

bool remove_user_preset(std::vector<EqPreset>& presets, const std::string& name) {
    const auto it = std::find_if(presets.begin(), presets.end(),
                                 [&](const EqPreset& p) { return same_name(p.name, name); });
    if (it == presets.end()) return false;
    presets.erase(it);
    return true;
}

}  // namespace pang::core::dsp
