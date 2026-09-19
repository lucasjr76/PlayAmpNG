#include "platform/audio_device.h"

#include "miniaudio.h"
#include "platform/device_recovery.h"

#include <atomic>
#include <cstdio>

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

    // Parametros da ultima abertura, para reabrir igual depois de uma perda.
    int sample_rate = 0;
    int channels = 0;
    std::string requested_device;   // vazio = padrao do sistema
    std::string error;

    DeviceRecovery recovery;
    std::atomic<bool> lost{false};  // escrito pelo callback do miniaudio

    // AU-12 — o miniaudio avisa quando o dispositivo some ou e trocado pelo
    // sistema. So marcamos a bandeira: reabrir de dentro de um callback do
    // proprio dispositivo e receita de travamento.
    static void notification_callback(const ma_device_notification* note) {
        if (!note || !note->pDevice) return;
        auto* self = static_cast<Impl*>(note->pDevice->pUserData);
        if (!self) return;
        switch (note->type) {
            case ma_device_notification_type_stopped:
            case ma_device_notification_type_interruption_began:
                self->lost.store(true, std::memory_order_relaxed);
                break;
            default:
                break;
        }
    }

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
                        std::string& error, const std::string& device) {
    close();
    impl_->render = render;
    impl_->user = user;
    impl_->sample_rate = sample_rate;
    impl_->channels = channels;
    impl_->requested_device = device;
    impl_->lost.store(false, std::memory_order_relaxed);

    // AU-11 — resolve o nome pedido para um id. Nome vazio ou que nao existe
    // mais cai no padrao do sistema; o dispositivo salvo na sessao anterior
    // pode ter sido desconectado, e recusar-se a tocar por isso seria pior que
    // tocar no padrao.
    Context c;
    ma_device_id id{};
    bool have_id = false;
    if (!device.empty() && c.ok) {
        ma_device_info* infos = nullptr;
        ma_uint32 count = 0;
        if (ma_context_get_devices(&c.ctx, &infos, &count, nullptr, nullptr) == MA_SUCCESS)
            for (ma_uint32 i = 0; i < count; ++i)
                if (device == infos[i].name) {
                    id = infos[i].id;
                    have_id = true;
                    break;
                }
        if (!have_id)
            std::fprintf(stderr, "dispositivo \"%s\" nao encontrado; usando o padrao\n",
                         device.c_str());
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;  // AU-09 — ponto flutuante fim a fim
    cfg.playback.channels = static_cast<ma_uint32>(channels);
    cfg.playback.pDeviceID = have_id ? &id : nullptr;
    cfg.sampleRate = static_cast<ma_uint32>(sample_rate);
    cfg.dataCallback = &Impl::data_callback;
    cfg.notificationCallback = &Impl::notification_callback;
    cfg.pUserData = impl_.get();

    if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) {
        error = "nao foi possivel abrir o dispositivo de saida";
        impl_->error = error;
        return false;
    }
    impl_->open = true;

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        error = "nao foi possivel iniciar o dispositivo de saida";
        impl_->error = error;
        close();
        return false;
    }
    impl_->recovery.succeeded();
    impl_->error.clear();
    return true;
}

AudioOutput::Health AudioOutput::poll(int elapsed_ms) {
    if (impl_->lost.exchange(false, std::memory_order_relaxed)) impl_->recovery.lost();

    switch (impl_->recovery.tick(elapsed_ms)) {
        case DeviceRecovery::Action::Reopen: {
            // AU-12 — a posicao nao precisa ser salva: ela vive no Engine, que
            // nao e tocado aqui. Reabrir o dispositivo e voltar a puxar do
            // mesmo lugar e o que preserva a reproducao.
            std::string error;
            if (start(impl_->sample_rate, impl_->channels, impl_->render, impl_->user, error,
                      impl_->requested_device)) {
                impl_->recovery.succeeded();
            } else {
                impl_->error = error;
                impl_->recovery.failed();
            }
            break;
        }
        case DeviceRecovery::Action::GiveUp:
            if (impl_->error.empty())
                impl_->error = "dispositivo de saida perdido e nao restabelecido";
            break;
        default:
            break;
    }
    return health();
}

AudioOutput::Health AudioOutput::health() const {
    if (impl_->recovery.exhausted()) return Health::Failed;
    return impl_->recovery.healthy() ? Health::Ok : Health::Recovering;
}

std::string AudioOutput::last_error() const { return impl_->error; }

bool AudioOutput::switch_to(const std::string& device, std::string& error) {
    impl_->recovery.reset();
    if (start(impl_->sample_rate, impl_->channels, impl_->render, impl_->user, error, device))
        return true;
    // Sem isto health() diria Ok com o dispositivo fechado, porque reset()
    // acabou de zerar a politica.
    impl_->recovery.lost();
    impl_->recovery.failed();
    impl_->recovery.failed();
    impl_->recovery.failed();
    impl_->error = error;
    return false;
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
