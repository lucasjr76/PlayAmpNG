// M6-2 — selecao de dispositivo e recuperacao de perda.
//
// A politica de recuperacao e verificada aqui de ponta a ponta sem placa de
// som, que e a razao de ela existir separada do codigo do miniaudio: se
// estivesse misturada, so daria para exercita-la desconectando um fone na mao.

#include <cstdio>
#include <algorithm>
#include <set>
#include <string>

#include "core/util/check.h"
#include "platform/audio_device.h"
#include "platform/device_recovery.h"

namespace {

using pang::platform::DeviceRecovery;
using Action = DeviceRecovery::Action;

void policy_is_idle_while_healthy() {
    DeviceRecovery r;
    PANG_CHECK(r.healthy(), "comeca saudavel");
    for (int i = 0; i < 10; ++i)
        PANG_CHECK(r.tick(100) == Action::Idle, "dispositivo saudavel nao gera acao");
    PANG_CHECK(r.attempts() == 0, "nenhuma tentativa sem perda");
}

void policy_waits_before_retrying() {
    DeviceRecovery r;
    r.lost();
    PANG_CHECK(!r.healthy(), "perda tira do estado saudavel");
    // O intervalo existe para nao martelar um dispositivo que ainda esta
    // sumindo. Antes de venci-lo, nada de reabrir.
    PANG_CHECK(r.tick(DeviceRecovery::kDelayMs - 1) == Action::Wait, "espera o intervalo");
    PANG_CHECK(r.attempts() == 0, "ainda nao tentou");
    PANG_CHECK(r.tick(1) == Action::Reopen, "vencido o intervalo, manda reabrir");
    PANG_CHECK(r.attempts() == 1, "primeira tentativa contada");
}

void policy_gives_up_after_the_limit() {
    DeviceRecovery r;
    r.lost();
    for (int attempt = 1; attempt <= DeviceRecovery::kMaxAttempts; ++attempt) {
        PANG_CHECK(r.tick(DeviceRecovery::kDelayMs) == Action::Reopen,
                   "manda reabrir dentro do limite");
        PANG_CHECK(r.attempts() == attempt, "tentativa contada");
        r.failed();
    }
    PANG_CHECK(r.exhausted(), "esgota depois do limite");
    // Esgotado, insiste em GiveUp: o dono precisa poder relatar o erro e nao
    // ficar tentando para sempre.
    for (int i = 0; i < 5; ++i)
        PANG_CHECK(r.tick(DeviceRecovery::kDelayMs) == Action::GiveUp, "permanece esgotado");
}

void policy_recovers_and_forgets() {
    DeviceRecovery r;
    r.lost();
    PANG_CHECK(r.tick(DeviceRecovery::kDelayMs) == Action::Reopen, "tenta");
    r.succeeded();
    PANG_CHECK(r.healthy(), "sucesso volta a saudavel");
    PANG_CHECK(r.attempts() == 0, "contagem zerada apos sucesso");

    // Uma nova perda tem direito ao limite inteiro de novo.
    r.lost();
    for (int attempt = 1; attempt <= DeviceRecovery::kMaxAttempts; ++attempt) {
        PANG_CHECK(r.tick(DeviceRecovery::kDelayMs) == Action::Reopen, "limite renovado");
        r.failed();
    }
    PANG_CHECK(r.exhausted(), "esgota de novo");
}

void repeated_loss_does_not_restart_the_count() {
    // Um dispositivo que pisca manda varias notificacoes da mesma perda. Se
    // cada uma reiniciasse a contagem, o player tentaria reabrir para sempre.
    DeviceRecovery r;
    r.lost();
    PANG_CHECK(r.tick(DeviceRecovery::kDelayMs) == Action::Reopen, "tenta");
    r.failed();
    r.lost();
    r.lost();
    PANG_CHECK(r.attempts() == 1, "notificacao repetida nao zera a contagem");
    for (int attempt = 2; attempt <= DeviceRecovery::kMaxAttempts; ++attempt) {
        PANG_CHECK(r.tick(DeviceRecovery::kDelayMs) == Action::Reopen, "segue contando");
        r.failed();
    }
    PANG_CHECK(r.exhausted(), "esgota como se nao houvesse repeticao");
}

void reset_allows_a_fresh_start() {
    DeviceRecovery r;
    r.lost();
    for (int i = 0; i < DeviceRecovery::kMaxAttempts; ++i) {
        r.tick(DeviceRecovery::kDelayMs);
        r.failed();
    }
    PANG_CHECK(r.exhausted(), "esgotado");
    // Sem reset, trocar de dispositivo na mao nao tiraria o player do mudo.
    r.reset();
    PANG_CHECK(r.healthy(), "reset devolve ao estado saudavel");
    PANG_CHECK(r.tick(1000) == Action::Idle, "e nao manda reabrir sozinho");
}

// AU-11 — enumeracao. Numa maquina sem placa de som a lista vem vazia, e isso
// nao e falha: o que nao pode e a lista ser inconsistente.
void enumeration_is_consistent() {
    std::string error;
    const auto devices = pang::platform::list_playback_devices(error);
    std::printf("  backend: %s, %zu dispositivo(s)\n",
                pang::platform::backend_name().c_str(), devices.size());

    int defaults = 0;
    std::set<std::string> names;
    for (const auto& d : devices) {
        PANG_CHECK(!d.name.empty(), "dispositivo enumerado tem nome");
        names.insert(d.name);
        defaults += d.is_default ? 1 : 0;
    }
    PANG_CHECK(names.size() == devices.size(), "nomes de dispositivo nao se repetem");
    PANG_CHECK(defaults <= 1, "no maximo um dispositivo padrao");
    // Nao se exige que haja um padrao: numa maquina de integracao sem placa
    // real o backend enumera um dispositivo nulo e nao marca nenhum padrao.
    // O invariante que interessa ao player e "no maximo um", ja conferido.
}

// AU-12, AR-05 — parar de proposito nao e perder o dispositivo.
//
// O defeito que este bloco guarda so apareceu quando o log passou a registrar
// as transicoes da saida: a cada troca de faixa o controlador suspende o
// dispositivo, o miniaudio avisa "stopped", e o player reabria o dispositivo
// achando que ele tinha caido. Duas vezes em toda abertura do programa.
//
// Precisa de dispositivo de verdade; sem um, nao ha o que exercitar e o bloco
// se declara pulado em vez de passar calado.
void suspend_is_not_a_loss() {
    using pang::platform::AudioOutput;
    AudioOutput output;
    std::string error;
    auto silence = [](void*, float* out, std::uint32_t frames) {
        std::fill(out, out + frames * 2, 0.0f);
    };
    if (!output.start(44100, 2, silence, nullptr, error)) {
        std::printf("  suspender/retomar: PULADO, sem dispositivo (%s)\n", error.c_str());
        return;
    }

    // Tres ciclos, como tres trocas de faixa. O aviso do backend chega DENTRO
    // da parada, entao o primeiro poll depois de retomar ja o enxerga — nao
    // ha espera arbitraria aqui.
    for (int ciclo = 0; ciclo < 3; ++ciclo) {
        output.suspend();
        output.resume();
        PANG_CHECK(output.poll(100) == AudioOutput::Health::Ok,
                   "suspender e retomar nao e lido como perda do dispositivo");
    }
    output.close();
}

}  // namespace

int main() {
    policy_is_idle_while_healthy();
    policy_waits_before_retrying();
    policy_gives_up_after_the_limit();
    policy_recovers_and_forgets();
    repeated_loss_does_not_restart_the_count();
    reset_allows_a_fresh_start();
    enumeration_is_consistent();
    suspend_is_not_a_loss();
    return pang::check::exit_code();
}
