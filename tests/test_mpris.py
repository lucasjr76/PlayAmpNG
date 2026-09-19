#!/usr/bin/env python3
"""IN-05, IN-04 — MPRIS no barramento de sessão.

Sobe o player de verdade, fala com ele por D-Bus e confere que ele responde.
Vale por dois requisitos: o caminho exercitado aqui é exatamente o que o
ambiente de trabalho usa para encaminhar as TECLAS DE MÍDIA ao player
registrado. Verificar o D-Bus verifica a tecla.

Pulado (77) sem barramento de sessão ou sem `dbus-send` — a ausência deles numa
máquina de integração não é defeito do player.

    test_mpris.py <binário> <assets>
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import wave

PULAR = 77
ROOT = "org.mpris.MediaPlayer2"
PLAYER = "org.mpris.MediaPlayer2.Player"
OBJ = "/org/mpris/MediaPlayer2"

falhas = []


def checar(condicao, descricao):
    if condicao:
        return True
    falhas.append(descricao)
    print(f"FALHA: {descricao}", file=sys.stderr)
    return False


def dbus(servico, interface, membro, *args, propriedade=False):
    alvo = "org.freedesktop.DBus.Properties.Get" if propriedade else f"{interface}.{membro}"
    cmd = ["dbus-send", "--session", f"--dest={servico}", "--print-reply", OBJ, alvo]
    if propriedade:
        cmd += [f"string:{interface}", f"string:{membro}"]
    cmd += list(args)
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
    return r.stdout if r.returncode == 0 else None


def servico_do_player(pid=None):
    r = subprocess.run(["dbus-send", "--session", "--dest=org.freedesktop.DBus",
                        "--print-reply", "/org/freedesktop/DBus",
                        "org.freedesktop.DBus.ListNames"],
                       capture_output=True, text=True, timeout=10)
    for linha in r.stdout.splitlines():
        if "org.mpris.MediaPlayer2.playampng" in linha:
            nome = linha.split('"')[1]
            # O nome do servico termina em instance<pid>: e assim que o teste
            # reconhece o player QUE ELE lancou, e nao outro que ja estivesse
            # no ar.
            if pid is None or nome.endswith(f"instance{pid}"):
                return nome
    return None


def esperar(condicao, limite=8.0):
    """Espera pelo evento observável em vez de amostrar num instante arbitrário.

    Devolve o VALOR da condição, não o estado — quem chama testa a verdade dela.
    """
    fim = time.time() + limite
    while time.time() < fim:
        resultado = condicao()
        if resultado:
            return resultado
        time.sleep(0.15)
    return condicao()


if shutil.which("dbus-send") is None or not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
    print("sem barramento de sessao ou sem dbus-send: teste pulado", file=sys.stderr)
    sys.exit(PULAR)

binario = sys.argv[1]
media = os.path.join(tempfile.gettempdir(), f"pang_mpris_{os.getpid()}.wav")

# Trinta segundos: longo o bastante para atravessar todas as verificações sem a
# faixa acabar no meio e mudar o estado por conta própria.
with wave.open(media, "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(44100)
    quadro = struct.pack("<hh", 3000, 3000)
    w.writeframes(quadro * (44100 * 30))

# Diretório de configuração próprio. A identidade da instância deriva do
# arquivo de configuração, então isto também dá ao teste uma instância própria:
# sem isso ele fala com o player que o usuário tiver aberto, e mede outra coisa.
perfil = tempfile.mkdtemp(prefix="pang_perfil_")
ambiente = dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_CONFIG_HOME=perfil)
proc = subprocess.Popen([binario, media], env=ambiente,
                        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
try:
    servico = esperar(lambda: servico_do_player(proc.pid), 15.0)
    if not servico:
        proc.terminate()
        erro = proc.stderr.read() if proc.stderr else ""
        if "dispositivo de audio" in erro.lower():
            print("sem dispositivo de audio: teste pulado", file=sys.stderr)
            sys.exit(PULAR)
        print(f"o player nao apareceu no barramento. stderr:\n{erro}", file=sys.stderr)
        sys.exit(1)
    print(f"  servico: {servico}")

    # Propriedades da raiz: e por elas que o ambiente decide se mostra o player
    # no painel e se oferece o botao de trazer a janela para a frente.
    identidade = dbus(servico, ROOT, "Identity", propriedade=True) or ""
    checar("PlayAmpNG" in identidade, "Identity informa o nome do programa")
    checar("true" in (dbus(servico, ROOT, "CanRaise", propriedade=True) or ""),
           "CanRaise verdadeiro: o ambiente pode trazer a janela para a frente")
    checar("true" in (dbus(servico, ROOT, "CanQuit", propriedade=True) or ""),
           "CanQuit verdadeiro")
    esquemas = dbus(servico, ROOT, "SupportedUriSchemes", propriedade=True) or ""
    for esquema in ("file", "http", "https"):
        checar(esquema in esquemas, f"esquema {esquema} anunciado")

    def status():
        saida = dbus(servico, PLAYER, "PlaybackStatus", propriedade=True) or ""
        for estado in ("Playing", "Paused", "Stopped"):
            if f'"{estado}"' in saida:
                return estado
        return None

    # IN-04 — cada um destes e o que uma tecla de midia dispara.
    #
    # Verificados como TRANSICAO, e nunca como estado final isolado: o player
    # abre tocando quando recebe arquivo por argumento, entao afirmar "depois
    # de Play esta tocando" passaria sem o comando fazer nada. A transicao so
    # tem valor se o estado anterior for diferente do alvo, e e isso que
    # `transicao` exige antes de emitir o comando.
    def transicao(membro, alvo):
        antes = esperar(lambda: status() is not None) and status()
        if antes == alvo:
            checar(False, f"{membro}: o player ja estava em {alvo}; a verificacao nao valeria")
            return
        dbus(servico, PLAYER, membro)
        checar(esperar(lambda: status() == alvo),
               f"{membro} pelo barramento leva de {antes} para {alvo}")

    # O arquivo veio por argumento, entao o player abre tocando. Comecar por
    # Pause garante que o primeiro Play parta de um estado diferente.
    checar(esperar(lambda: status() == "Playing"), "o player abre tocando o arquivo do argumento")
    transicao("Pause", "Paused")
    transicao("Play", "Playing")
    transicao("Pause", "Paused")
    transicao("PlayPause", "Playing")
    transicao("PlayPause", "Paused")

    metadados = dbus(servico, PLAYER, "Metadata", propriedade=True) or ""
    checar("mpris:trackid" in metadados, "metadados trazem identificador de faixa")
    checar("xesam:title" in metadados, "metadados trazem titulo")
    # MD-09 — arquivo local informa duracao; stream ao vivo nao traria este campo.
    checar("mpris:length" in metadados, "arquivo local informa duracao nos metadados")
    checar("true" in (dbus(servico, PLAYER, "CanSeek", propriedade=True) or ""),
           "CanSeek verdadeiro em arquivo local")

    transicao("Stop", "Stopped")

    # Quit pelo barramento: o ambiente precisa poder encerrar o player.
    dbus(servico, ROOT, "Quit")
    try:
        proc.wait(timeout=10)
        checar(True, "Quit pelo barramento encerra o player")
    except subprocess.TimeoutExpired:
        checar(False, "Quit pelo barramento encerra o player")
finally:
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    if os.path.exists(media):
        os.remove(media)

if falhas:
    print(f"{len(falhas)} verificacao(oes) falharam", file=sys.stderr)
    sys.exit(1)
print("todas as verificacoes passaram")
