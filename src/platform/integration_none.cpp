#include "platform/integration.h"

// Sistemas sem integracao implementada ainda (Windows e macOS, ate o M6-3 ser
// concluido neles). O player funciona igual — so nao aparece no painel do
// sistema nem responde a teclas de midia pelo caminho do ambiente.
//
// Existir como objeto valido, em vez de ponteiro nulo, e o que evita um teste
// de nulo em cada uso no chamador.
namespace pang::platform {
namespace {

class NoIntegration : public Integration {
public:
    bool available() const override { return false; }
    void publish(const NowPlaying&) override {}
    std::string backend() const override { return "nenhuma"; }
};

}  // namespace

std::unique_ptr<Integration> make_integration(const std::string&, Commands) {
    return std::make_unique<NoIntegration>();
}

}  // namespace pang::platform
