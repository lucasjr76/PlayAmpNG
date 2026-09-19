#include "ui/single_instance.h"

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>

#include "core/util/log.h"

namespace pang::ui {
namespace {

constexpr int kConnectTimeoutMs = 500;
constexpr int kTransferTimeoutMs = 1000;

}  // namespace

SingleInstance::SingleInstance(QString key, QObject* parent)
    : QObject(parent), key_(std::move(key)) {
    // Se ja ha alguem escutando, este processo e secundario.
    QLocalSocket probe;
    probe.connectToServer(key_);
    if (probe.waitForConnected(kConnectTimeoutMs)) {
        probe.disconnectFromServer();
        primary_ = false;
        return;
    }

    server_ = new QLocalServer(this);
    // Um socket orfao fica no sistema de arquivos quando o processo anterior
    // cai sem fechar. Sem esta remocao, o player nunca mais subiria — e foi por
    // isso que nao se usou arquivo de trava, que teria o mesmo problema sem
    // nem oferecer o canal de entrega.
    QLocalServer::removeServer(key_);
    if (!server_->listen(key_)) {
        core::log::warn("instancia unica indisponivel: " +
                        server_->errorString().toStdString());
        primary_ = true;  // sem canal, e melhor abrir do que nao abrir
        return;
    }
    primary_ = true;

    connect(server_, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* client = server_->nextPendingConnection()) {
            connect(client, &QLocalSocket::readyRead, client, [this, client] {
                QDataStream stream(client);
                QStringList paths;
                stream >> paths;
                if (stream.status() == QDataStream::Ok && on_files && !paths.isEmpty())
                    on_files(paths);
                client->disconnectFromServer();
            });
            connect(client, &QLocalSocket::disconnected, client, &QLocalSocket::deleteLater);
        }
    });
}

SingleInstance::~SingleInstance() {
    if (server_) server_->close();
}

bool SingleInstance::send(const QStringList& paths) const {
    QLocalSocket socket;
    socket.connectToServer(key_);
    if (!socket.waitForConnected(kConnectTimeoutMs)) return false;

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << paths;
    socket.write(payload);
    if (!socket.waitForBytesWritten(kTransferTimeoutMs)) return false;
    socket.disconnectFromServer();
    return true;
}

}  // namespace pang::ui
