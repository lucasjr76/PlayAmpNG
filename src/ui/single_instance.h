#pragma once

#include <QObject>
#include <QStringList>

#include <functional>
#include <memory>

class QLocalServer;

namespace pang::ui {

// IN-08 — instancia unica.
//
// Um segundo lancamento nao abre outra janela: entrega os arquivos a instancia
// que ja esta no ar e sai. E o que o usuario espera ao dar duplo clique num
// arquivo com o player aberto — duas janelas tocando ao mesmo tempo seriam
// ruido, nao funcionalidade.
//
// O canal e um socket local nomeado (QLocalServer), e nao um arquivo de trava:
// trava sobrevive a queda do processo e deixa o programa inutilizavel ate
// alguem apagar o arquivo na mao.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    // Tenta assumir o papel de instancia primaria.
    explicit SingleInstance(QString key, QObject* parent = nullptr);
    ~SingleInstance() override;

    // Verdadeiro quando ESTE processo e a instancia primaria. Falso significa
    // que ja havia outra: o chamador deve entregar os argumentos e encerrar.
    bool primary() const { return primary_; }

    // Entrega os caminhos a instancia primaria. So faz sentido quando
    // primary() e falso. Devolve falso se a entrega nao chegou — nesse caso o
    // chamador deve seguir e abrir a propria janela, e nao sumir em silencio.
    bool send(const QStringList& paths) const;

    // Chamado na primaria a cada entrega recebida.
    std::function<void(const QStringList&)> on_files;

private:
    QString key_;
    QLocalServer* server_ = nullptr;
    bool primary_ = false;
};

}  // namespace pang::ui
