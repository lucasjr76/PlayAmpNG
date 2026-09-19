#include "platform/audio_device.h"

#include "miniaudio.h"
#include "platform/device_recovery.h"

#include <atomic>
#include <cstdio>
#include <thread>

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

// Um canal por ABERTURA de dispositivo.
//
// Existe para que um dispositivo abandonado (ver Impl::abandon) possa continuar
// chamando seu callback sem escrever no estado do dispositivo atual. Sem isso,
// o contador de quadros do morto mascararia a parada do vivo.
struct Channel {
    // Atomico porque abandon() o anula do thread da interface enquanto o
    // callback de audio pode estar lendo. `user` nao precisa: e escrito uma
    // vez antes de o dispositivo iniciar e nunca mais.
    std::atomic<AudioOutput::RenderFn> render{nullptr};
    void* user = nullptr;
    std::atomic<std::uint64_t> frames{0};
    std::atomic<bool> lost{false};
};

struct AudioOutput::Impl {
    std::unique_ptr<ma_device> device;
    std::shared_ptr<Channel> channel;
    bool open = false;

    // Parametros da ultima abertura, para reabrir igual depois de uma perda.
    int sample_rate = 0;
    int channels = 0;
    std::string requested_device;   // vazio = padrao do sistema
    std::string error;

    DeviceRecovery recovery;

    // AU-12 — detector de parada. O callback de dados e o unico sinal de vida
    // em que da para confiar: se o dispositivo esta vivo, ele PUXA. Medido
    // neste projeto: ao remover um sink do PulseAudio em uso, o callback de
    // notificacao do miniaudio NAO dispara — os quadros simplesmente param e o
    // dispositivo segue se dizendo saudavel indefinidamente. Confiar so na
    // notificacao deixaria o player mudo sem nada acusar.
    std::uint64_t last_seen_frames = 0;
    int stalled_ms = 0;
    bool running = false;  // iniciado e nao suspenso

    // Tolerancia ate declarar parada. Precisa ser maior que o maior intervalo
    // plausivel entre dois callbacks; um segundo de silencio com o dispositivo
    // supostamente tocando ja e defeito em qualquer backend.
    static constexpr int kStallMs = 1000;

    // A reabertura acontece num trabalhador proprio, e nao no thread que chama
    // poll(): mesmo sem o uninit, abrir dispositivo demora o bastante para
    // engasgar a janela.
    std::thread worker;
    enum class Reopen : std::uint8_t { Idle, Working, Succeeded, Failed };
    std::atomic<Reopen> reopen{Reopen::Idle};
    std::string worker_error;

    void join_worker() {
        if (worker.joinable()) worker.join();
    }

    // Larga o dispositivo atual SEM destrui-lo.
    //
    // Medido neste projeto: com o sink do PulseAudio removido, ma_device_uninit
    // nao retorna — foram 25 s de espera sem resposta, e nao ha razao para
    // crer que terminaria. Destruir o morto antes de abrir o vivo travava a
    // recuperacao inteira.
    //
    // O dispositivo largado vaza, e o vazamento e deliberado: acontece uma vez
    // por perda de hardware, que e evento raro, e o preco de alguns descritores
    // e menor que o de um player que nunca mais toca. A tentativa de liberar
    // segue num thread solto, que termina se o sistema de audio voltar.
    void abandon() {
        if (!device) return;
        if (channel) channel->render.store(nullptr, std::memory_order_release);
        ma_device* dead = device.release();
        std::shared_ptr<Channel> dead_channel = channel;  // mantem vivo o alvo do callback
        std::thread([dead, dead_channel] {
            ma_device_uninit(dead);
            delete dead;
        }).detach();
        channel.reset();
        open = false;
        running = false;
    }

    // AU-12 — o miniaudio avisa quando o dispositivo some ou e trocado pelo
    // sistema. So marcamos a bandeira: reabrir de dentro de um callback do
    // proprio dispositivo e receita de travamento. Nem todo backend emite esse
    // aviso, e por isso ele NAO e a unica deteccao.
    static void notification_callback(const ma_device_notification* note) {
        if (!note || !note->pDevice) return;
        auto* ch = static_cast<Channel*>(note->pDevice->pUserData);
        if (!ch) return;
        switch (note->type) {
            case ma_device_notification_type_stopped:
            case ma_device_notification_type_interruption_began:
                ch->lost.store(true, std::memory_order_relaxed);
                break;
            default:
                break;
        }
    }

    static void data_callback(ma_device* dev, void* output, const void* /*input*/,
                              ma_uint32 frame_count) {
        auto* ch = static_cast<Channel*>(dev->pUserData);
        if (!ch) return;
        ch->frames.fetch_add(frame_count, std::memory_order_relaxed);
        if (const AudioOutput::RenderFn fn = ch->render.load(std::memory_order_acquire))
            fn(ch->user, static_cast<float*>(output), frame_count);
    }
};


AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}

AudioOutput::~AudioOutput() {
    impl_->join_worker();  // o trabalhador toca impl_; nao pode sobreviver a ele
    close();
}

bool AudioOutput::start(int sample_rate, int channels, RenderFn render, void* user,
                        std::string& error, const std::string& device) {
    close();
    impl_->sample_rate = sample_rate;
    impl_->channels = channels;
    impl_->requested_device = device;

    // Canal novo a cada abertura: o contador de quadros recomeca do zero e o
    // dispositivo anterior, se foi abandonado, nao escreve mais aqui.
    auto channel = std::make_shared<Channel>();
    channel->render.store(render, std::memory_order_relaxed);
    channel->user = user;

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
    cfg.pUserData = channel.get();

    auto fresh = std::make_unique<ma_device>();
    if (ma_device_init(nullptr, &cfg, fresh.get()) != MA_SUCCESS) {
        error = "nao foi possivel abrir o dispositivo de saida";
        impl_->error = error;
        return false;
    }
    if (ma_device_start(fresh.get()) != MA_SUCCESS) {
        error = "nao foi possivel iniciar o dispositivo de saida";
        impl_->error = error;
        ma_device_uninit(fresh.get());
        return false;
    }

    impl_->device = std::move(fresh);
    impl_->channel = std::move(channel);
    impl_->open = true;
    impl_->running = true;
    impl_->last_seen_frames = 0;
    impl_->stalled_ms = 0;
    impl_->error.clear();
    return true;
}

AudioOutput::Health AudioOutput::poll(int elapsed_ms) {
    // Colhe o resultado da tentativa anterior antes de decidir qualquer coisa.
    switch (impl_->reopen.load(std::memory_order_acquire)) {
        case Impl::Reopen::Working:
            return Health::Recovering;  // tentativa em curso; nada a decidir
        case Impl::Reopen::Succeeded:
            impl_->join_worker();
            impl_->reopen.store(Impl::Reopen::Idle, std::memory_order_relaxed);
            impl_->recovery.succeeded();
            break;
        case Impl::Reopen::Failed:
            impl_->join_worker();
            impl_->reopen.store(Impl::Reopen::Idle, std::memory_order_relaxed);
            impl_->error = impl_->worker_error;
            impl_->recovery.failed();
            break;
        case Impl::Reopen::Idle:
            break;
    }

    if (impl_->channel && impl_->channel->lost.exchange(false, std::memory_order_relaxed))
        impl_->recovery.lost();

    // Parada: o dispositivo deveria estar puxando quadros e nao esta.
    if (impl_->running && impl_->open && impl_->channel) {
        const std::uint64_t now = impl_->channel->frames.load(std::memory_order_relaxed);
        if (now != impl_->last_seen_frames) {
            impl_->last_seen_frames = now;
            impl_->stalled_ms = 0;
        } else {
            impl_->stalled_ms += elapsed_ms;
            if (impl_->stalled_ms >= Impl::kStallMs) {
                impl_->stalled_ms = 0;
                impl_->recovery.lost();
            }
        }
    }

    switch (impl_->recovery.tick(elapsed_ms)) {
        case DeviceRecovery::Action::Reopen: {
            // AU-12 — a posicao nao precisa ser salva: ela vive no Engine, que
            // nao e tocado aqui. Reabrir a saida e voltar a puxar do mesmo
            // lugar e o que preserva a reproducao.
            impl_->join_worker();
            impl_->reopen.store(Impl::Reopen::Working, std::memory_order_relaxed);
            Impl* impl = impl_.get();
            RenderFn render = impl->channel ? impl->channel->render.load(std::memory_order_relaxed) : nullptr;
            void* user = impl->channel ? impl->channel->user : nullptr;
            impl->abandon();  // o morto fica para tras; destrui-lo travaria
            impl->worker = std::thread([this, impl, render, user] {
                std::string error;
                const bool ok = start(impl->sample_rate, impl->channels, render, user, error,
                                      impl->requested_device);
                impl->worker_error = error;
                impl->reopen.store(ok ? Impl::Reopen::Succeeded : Impl::Reopen::Failed,
                                   std::memory_order_release);
            });
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
    RenderFn render =
        impl_->channel ? impl_->channel->render.load(std::memory_order_relaxed) : nullptr;
    void* user = impl_->channel ? impl_->channel->user : nullptr;
    if (start(impl_->sample_rate, impl_->channels, render, user, error, device))
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
    // running vira falso ANTES de parar: pausado, o dispositivo deixa de puxar
    // de proposito, e o detector de parada nao pode confundir isso com perda.
    impl_->running = false;
    if (impl_->open && impl_->device) ma_device_stop(impl_->device.get());
}

void AudioOutput::resume() {
    if (impl_->open && impl_->device) ma_device_start(impl_->device.get());
    impl_->last_seen_frames =
        impl_->channel ? impl_->channel->frames.load(std::memory_order_relaxed) : 0;
    impl_->stalled_ms = 0;
    impl_->running = impl_->open;
}

void AudioOutput::close() {
    impl_->running = false;
    if (impl_->open && impl_->device) {
        ma_device_uninit(impl_->device.get());
        impl_->device.reset();
        impl_->channel.reset();
        impl_->open = false;
    }
}

bool AudioOutput::is_open() const { return impl_->open; }

int AudioOutput::sample_rate() const {
    return impl_->open ? static_cast<int>(impl_->device->sampleRate) : 0;
}

int AudioOutput::channels() const {
    return impl_->open ? static_cast<int>(impl_->device->playback.channels) : 0;
}

std::string AudioOutput::device_name() const {
    return impl_->open ? impl_->device->playback.name : std::string{};
}

}  // namespace pang::platform
