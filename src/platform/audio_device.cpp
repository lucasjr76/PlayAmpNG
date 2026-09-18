#include "platform/audio_device.h"

#include "miniaudio.h"

namespace pang::platform {
namespace {

// Contexto de curta duracao: o M0 so enumera. O engine (M1) mantera um
// contexto vivo pelo tempo da aplicacao.
struct Context {
    ma_context ctx{};
    bool ok = false;

    Context() { ok = ma_context_init(nullptr, 0, nullptr, &ctx) == MA_SUCCESS; }
    ~Context() {
        if (ok) ma_context_uninit(&ctx);
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};

}  // namespace

std::vector<PlaybackDevice> list_playback_devices(std::string& error) {
    Context c;
    if (!c.ok) {
        error = "nao foi possivel inicializar o contexto de audio";
        return {};
    }

    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&c.ctx, &infos, &count, nullptr, nullptr) != MA_SUCCESS) {
        error = "nao foi possivel enumerar dispositivos de saida";
        return {};
    }

    std::vector<PlaybackDevice> out;
    out.reserve(count);
    for (ma_uint32 i = 0; i < count; ++i)
        out.push_back({infos[i].name, infos[i].isDefault != 0});
    return out;
}

std::string backend_name() {
    Context c;
    return c.ok ? ma_get_backend_name(c.ctx.backend) : "indisponivel";
}

}  // namespace pang::platform
