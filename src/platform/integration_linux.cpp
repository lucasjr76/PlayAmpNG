// IN-04, IN-05 — MPRIS 2, a integracao de player do Linux.
//
// Vale por dois requisitos: alem de por a faixa no painel do sistema, e por
// aqui que as TECLAS DE MIDIA chegam. O ambiente de trabalho as encaminha ao
// player registrado no barramento, e e assim que deve ser — um programa que
// captura a tecla por conta propria briga com os outros e so funciona com a
// janela em foco.
//
// Nada aqui pode ser condicao para tocar: sem barramento de sessao, a
// integracao apenas se declara indisponivel.

#include "platform/integration.h"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

namespace pang::platform {
namespace {

constexpr const char* kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char* kPlayerInterface = "org.mpris.MediaPlayer2.Player";
constexpr const char* kRootInterface = "org.mpris.MediaPlayer2";

// Um microssegundo em unidades de D-Bus e o mesmo que aqui; o MPRIS usa
// microssegundos em toda a interface.
QString status_of(const NowPlaying& now) {
    if (now.stopped) return QStringLiteral("Stopped");
    return now.playing ? QStringLiteral("Playing") : QStringLiteral("Paused");
}

// O MPRIS exige um id de faixa em forma de caminho de objeto. Usamos um
// contador: o que importa e que MUDE a cada faixa, para o cliente saber que a
// metadata se refere a outra coisa.
QDBusObjectPath track_id(unsigned counter) {
    return QDBusObjectPath(QStringLiteral("/br/playampng/track/%1").arg(counter));
}

class Mpris;

// ------------------------------------------------------- org.mpris.MediaPlayer2
class RootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    RootAdaptor(QObject* parent, Commands* commands, QString identity)
        : QDBusAbstractAdaptor(parent), commands_(commands), identity_(std::move(identity)) {}

    bool canQuit() const { return static_cast<bool>(commands_->quit); }
    bool canRaise() const { return static_cast<bool>(commands_->raise); }
    bool hasTrackList() const { return false; }  // a playlist nao e exposta no barramento
    QString identity() const { return identity_; }
    QString desktopEntry() const { return QStringLiteral("playampng"); }
    QStringList supportedUriSchemes() const {
        return {QStringLiteral("file"), QStringLiteral("http"), QStringLiteral("https")};
    }
    QStringList supportedMimeTypes() const {
        return {QStringLiteral("audio/mpeg"),  QStringLiteral("audio/flac"),
                QStringLiteral("audio/ogg"),   QStringLiteral("audio/opus"),
                QStringLiteral("audio/x-wav"), QStringLiteral("audio/mp4")};
    }

public slots:
    void Raise() { if (commands_->raise) commands_->raise(); }
    void Quit() { if (commands_->quit) commands_->quit(); }

private:
    Commands* commands_;
    QString identity_;
};

// ------------------------------------------------ org.mpris.MediaPlayer2.Player
class PlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canControl)
    Q_PROPERTY(bool CanPause READ canControl)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    PlayerAdaptor(QObject* parent, Commands* commands, const NowPlaying* now, unsigned* counter)
        : QDBusAbstractAdaptor(parent), commands_(commands), now_(now), counter_(counter) {}

    QString playbackStatus() const { return status_of(*now_); }
    double volume() const { return now_->volume; }
    void setVolume(double v) { if (commands_->set_volume) commands_->set_volume(float(v)); }
    qlonglong position() const { return now_->position_us; }
    double rate() const { return 1.0; }
    void setRate(double) {}  // AU-* — o player nao varia a velocidade
    bool canGoNext() const { return now_->can_go_next; }
    bool canGoPrevious() const { return now_->can_go_previous; }
    bool canSeek() const { return now_->can_seek; }
    bool canControl() const { return true; }

    QVariantMap metadata() const {
        QVariantMap map;
        map[QStringLiteral("mpris:trackid")] = QVariant::fromValue(track_id(*counter_));
        // MD-09 — duracao so vai para o barramento quando a fonte informa. Um
        // stream ao vivo sem esse campo aparece como indefinido no painel do
        // sistema, que e o correto.
        if (now_->duration_us >= 0)
            map[QStringLiteral("mpris:length")] = qlonglong(now_->duration_us);
        if (!now_->title.empty())
            map[QStringLiteral("xesam:title")] = QString::fromStdString(now_->title);
        if (!now_->artist.empty())
            map[QStringLiteral("xesam:artist")] =
                QStringList{QString::fromStdString(now_->artist)};
        if (!now_->album.empty())
            map[QStringLiteral("xesam:album")] = QString::fromStdString(now_->album);
        if (!now_->url.empty()) {
            const QString url = QString::fromStdString(now_->url);
            map[QStringLiteral("xesam:url")] = url.contains(QStringLiteral("://"))
                                                   ? url
                                                   : QUrl::fromLocalFile(url).toString();
        }
        return map;
    }

public slots:
    void Play() { if (commands_->play) commands_->play(); }
    void Pause() { if (commands_->pause) commands_->pause(); }
    void PlayPause() { if (commands_->play_pause) commands_->play_pause(); }
    void Stop() { if (commands_->stop) commands_->stop(); }
    void Next() { if (commands_->next) commands_->next(); }
    void Previous() { if (commands_->previous) commands_->previous(); }
    void Seek(qlonglong offset_us) { if (commands_->seek) commands_->seek(offset_us); }
    void SetPosition(const QDBusObjectPath&, qlonglong position_us) {
        if (commands_->set_position) commands_->set_position(position_us);
    }
    void OpenUri(const QString&) {}  // abrir por D-Bus nao e oferecido

signals:
    void Seeked(qlonglong position_us);

private:
    Commands* commands_;
    const NowPlaying* now_;
    unsigned* counter_;
};

// ---------------------------------------------------------------- integracao
class Mpris : public QObject, public Integration {
public:
    Mpris(const std::string& name, Commands commands)
        : commands_(std::move(commands)),
          bus_(QDBusConnection::sessionBus()) {
        if (!bus_.isConnected()) return;

        new RootAdaptor(this, &commands_, QString::fromStdString(name));
        player_ = new PlayerAdaptor(this, &commands_, &now_, &track_counter_);

        if (!bus_.registerObject(QLatin1String(kObjectPath), this)) return;
        // O sufixo com o pid permite mais de uma instancia no barramento; o
        // MPRIS preve exatamente essa forma.
        const QString service = QStringLiteral("org.mpris.MediaPlayer2.playampng.instance%1")
                                    .arg(QCoreApplication::applicationPid());
        if (!bus_.registerService(service)) {
            bus_.unregisterObject(QLatin1String(kObjectPath));
            return;
        }
        service_ = service;
        available_ = true;
    }

    ~Mpris() override {
        if (!service_.isEmpty()) {
            bus_.unregisterService(service_);
            bus_.unregisterObject(QLatin1String(kObjectPath));
        }
    }

    bool available() const override { return available_; }
    std::string backend() const override { return available_ ? "MPRIS 2" : "MPRIS indisponivel"; }

    void publish(const NowPlaying& now) override {
        if (!available_) return;

        const bool track_changed = now.url != now_.url || now.title != now_.title ||
                                   now.artist != now_.artist || now.album != now_.album ||
                                   now.duration_us != now_.duration_us;
        const bool status_changed =
            now.playing != now_.playing || now.stopped != now_.stopped;
        const bool caps_changed = now.can_seek != now_.can_seek ||
                                  now.can_go_next != now_.can_go_next ||
                                  now.can_go_previous != now_.can_go_previous;
        const bool volume_changed = now.volume != now_.volume;

        // Salto de posicao: o MPRIS pede Seeked para isso e proibe anunciar
        // Position em PropertiesChanged. Anunciar posicao a cada quadro
        // encheria o barramento sem nada mudar na tela de quem escuta.
        const std::int64_t delta = now.position_us - now_.position_us;
        const bool seeked = !track_changed && (delta < 0 || delta > 2'000'000);

        now_ = now;
        if (track_changed) ++track_counter_;

        QVariantMap changed;
        if (track_changed) changed[QStringLiteral("Metadata")] = player_->metadata();
        if (status_changed) changed[QStringLiteral("PlaybackStatus")] = status_of(now_);
        if (volume_changed) changed[QStringLiteral("Volume")] = double(now_.volume);
        if (caps_changed) {
            changed[QStringLiteral("CanSeek")] = now_.can_seek;
            changed[QStringLiteral("CanGoNext")] = now_.can_go_next;
            changed[QStringLiteral("CanGoPrevious")] = now_.can_go_previous;
        }
        if (!changed.isEmpty()) emit_properties_changed(changed);
        if (seeked) emit player_->Seeked(now_.position_us);
    }

private:
    void emit_properties_changed(const QVariantMap& changed) {
        QDBusMessage signal = QDBusMessage::createSignal(
            QLatin1String(kObjectPath), QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"));
        signal << QLatin1String(kPlayerInterface) << changed << QStringList{};
        bus_.send(signal);
    }

    Commands commands_;
    QDBusConnection bus_;
    PlayerAdaptor* player_ = nullptr;
    NowPlaying now_;
    unsigned track_counter_ = 0;
    QString service_;
    bool available_ = false;
};

}  // namespace

std::unique_ptr<Integration> make_integration(const std::string& application_name,
                                              Commands commands, void*) {
    return std::make_unique<Mpris>(application_name, std::move(commands));
}

}  // namespace pang::platform

#include "integration_linux.moc"
