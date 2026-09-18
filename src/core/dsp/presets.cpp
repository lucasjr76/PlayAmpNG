#include "core/dsp/presets.h"

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

}  // namespace pang::core::dsp
