// IN-04, IN-05 — SMTC (SystemMediaTransportControls), a integracao de player
// do Windows.
//
// Faz pelo Windows o que o MPRIS faz pelo Linux: poe a faixa no painel de
// midia do sistema e, com isso, ganha as TECLAS DE MIDIA. Capturar a tecla por
// conta propria (RegisterHotKey) foi descartado de proposito: so funciona com
// a janela em foco ou rouba a tecla de todos os outros players.
//
// Uma diferenca de fundo em relacao ao MPRIS muda a forma do codigo: o SMTC
// nao e um servico do barramento, e sim um objeto preso a UMA janela. Por isso
// make_integration recebe o HWND — sem janela nativa nao ha o que registrar, e
// a integracao se declara indisponivel em vez de falhar.
//
// Nada aqui pode ser condicao para tocar.

#include "platform/integration.h"

#include <QCoreApplication>
#include <QMetaObject>

#include <windows.h>
#include <systemmediatransportcontrolsinterop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>

#include <chrono>
#include <utility>

namespace pang::platform {
namespace {

namespace wm = winrt::Windows::Media;
using winrt::Windows::Foundation::TimeSpan;

wm::MediaPlaybackStatus status_of(const NowPlaying& now) {
    if (now.stopped) return wm::MediaPlaybackStatus::Stopped;
    return now.playing ? wm::MediaPlaybackStatus::Playing : wm::MediaPlaybackStatus::Paused;
}

TimeSpan microseconds(std::int64_t us) {
    return std::chrono::duration_cast<TimeSpan>(std::chrono::microseconds(us < 0 ? 0 : us));
}

// O SMTC entrega os botoes numa thread do pool do WinRT. Os comandos do player
// tocam o controlador e a janela, que sao da thread da interface — executar
// direto ali seria corromper estado em paralelo. A ponte e a fila de eventos
// do Qt, exatamente como o resto do programa ja faz.
void on_ui_thread(std::function<void()> f) {
    if (!f) return;
    QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(f), Qt::QueuedConnection);
}

class Smtc : public Integration {
public:
    Smtc(const std::string& application_name, Commands commands, HWND window)
        : commands_(std::move(commands)) {
        (void)application_name;  // o SMTC usa o nome do executavel, nao um dado nosso
        if (!window) return;
        try {
            auto interop = winrt::get_activation_factory<wm::SystemMediaTransportControls,
                                                         ISystemMediaTransportControlsInterop>();
            winrt::check_hresult(interop->GetForWindow(
                window, winrt::guid_of<wm::SystemMediaTransportControls>(),
                winrt::put_abi(controls_)));
        } catch (const winrt::hresult_error&) {
            controls_ = nullptr;
            return;
        }

        controls_.IsEnabled(true);
        controls_.IsPlayEnabled(true);
        controls_.IsPauseEnabled(true);
        controls_.IsStopEnabled(true);
        controls_.DisplayUpdater().Type(wm::MediaPlaybackType::Music);

        token_ = controls_.ButtonPressed(
            [this](auto&&, const wm::SystemMediaTransportControlsButtonPressedEventArgs& args) {
                dispatch(args.Button());
            });
    }

    ~Smtc() override {
        if (!controls_) return;
        controls_.ButtonPressed(token_);
        controls_.IsEnabled(false);
    }

    bool available() const override { return controls_ != nullptr; }
    std::string backend() const override { return controls_ ? "SMTC" : "nenhuma"; }

    // Chamado a cada quadro do relogio da interface. Sem a comparacao abaixo o
    // painel do Windows seria reescrito varias vezes por segundo sem que nada
    // tivesse mudado — o mesmo cuidado que o lado do MPRIS ja toma.
    void publish(const NowPlaying& now) override {
        if (!controls_) return;

        controls_.IsNextEnabled(now.can_go_next);
        controls_.IsPreviousEnabled(now.can_go_previous);
        controls_.PlaybackStatus(status_of(now));

        if (!published_ || now.title != last_.title || now.artist != last_.artist ||
            now.album != last_.album || now.url != last_.url) {
            auto updater = controls_.DisplayUpdater();
            auto music = updater.MusicProperties();
            music.Title(winrt::to_hstring(now.title));
            music.Artist(winrt::to_hstring(now.artist));
            music.AlbumTitle(winrt::to_hstring(now.album));
            updater.Update();
        }

        // A barra de progresso do painel so aparece quando a duracao e
        // conhecida. MD-09 — transmissao ao vivo nao tem duracao, e mandar
        // zero desenharia uma barra que nunca anda.
        if (now.duration_us > 0) {
            wm::SystemMediaTransportControlsTimelineProperties timeline;
            timeline.StartTime(TimeSpan::zero());
            timeline.MinSeekTime(TimeSpan::zero());
            timeline.EndTime(microseconds(now.duration_us));
            timeline.MaxSeekTime(microseconds(now.duration_us));
            timeline.Position(microseconds(now.position_us));
            controls_.UpdateTimelineProperties(timeline);
        }

        last_ = now;
        published_ = true;
    }

private:
    void dispatch(wm::SystemMediaTransportControlsButton button) {
        using B = wm::SystemMediaTransportControlsButton;
        switch (button) {
            case B::Play:     on_ui_thread(commands_.play); break;
            case B::Pause:    on_ui_thread(commands_.pause); break;
            case B::Stop:     on_ui_thread(commands_.stop); break;
            case B::Next:     on_ui_thread(commands_.next); break;
            case B::Previous: on_ui_thread(commands_.previous); break;
            default: break;  // Record, FastForward, Rewind e Channel* nao se aplicam
        }
    }

    Commands commands_;
    wm::SystemMediaTransportControls controls_{nullptr};
    winrt::event_token token_{};
    NowPlaying last_{};
    bool published_ = false;
};

}  // namespace

std::unique_ptr<Integration> make_integration(const std::string& application_name,
                                              Commands commands, void* native_window) {
    return std::make_unique<Smtc>(application_name, std::move(commands),
                                  static_cast<HWND>(native_window));
}

}  // namespace pang::platform
