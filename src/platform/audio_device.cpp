#include "platform/audio_device.h"

#include "miniaudio.h"

namespace pang::platform {
namespace {

// Contexto de curta duracao para enumeracao.
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

// ------------------------------------------------------------------ AudioOutput

struct AudioOutput::Impl {
    ma_device device{};
    bool open = false;
    RenderFn render = nullptr;
    void* user = nullptr;

    static void data_callback(ma_device* dev, void* output, const void* /*input*/,
                              ma_uint32 frame_count) {
        auto* self = static_cast<Impl*>(dev->pUserData);
        if (self && self->render)
            self->render(self->user, static_cast<float*>(output), frame_count);
    }
};

AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}

AudioOutput::~AudioOutput() { close(); }

bool AudioOutput::start(int sample_rate, int channels, RenderFn render, void* user,
                        std::string& error) {
    close();
    impl_->render = render;
    impl_->user = user;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;  // AU-09 — ponto flutuante fim a fim
    cfg.playback.channels = static_cast<ma_uint32>(channels);
    cfg.sampleRate = static_cast<ma_uint32>(sample_rate);
    cfg.dataCallback = &Impl::data_callback;
    cfg.pUserData = impl_.get();

    if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) {
        error = "nao foi possivel abrir o dispositivo de saida";
        return false;
    }
    impl_->open = true;

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        error = "nao foi possivel iniciar o dispositivo de saida";
        close();
        return false;
    }
    return true;
}

void AudioOutput::suspend() {
    if (impl_->open) ma_device_stop(&impl_->device);
}

void AudioOutput::resume() {
    if (impl_->open) ma_device_start(&impl_->device);
}

void AudioOutput::close() {
    if (impl_->open) {
        ma_device_uninit(&impl_->device);
        impl_->open = false;
    }
}

bool AudioOutput::is_open() const { return impl_->open; }

int AudioOutput::sample_rate() const {
    return impl_->open ? static_cast<int>(impl_->device.sampleRate) : 0;
}

int AudioOutput::channels() const {
    return impl_->open ? static_cast<int>(impl_->device.playback.channels) : 0;
}

std::string AudioOutput::device_name() const {
    return impl_->open ? impl_->device.playback.name : std::string{};
}

}  // namespace pang::platform
