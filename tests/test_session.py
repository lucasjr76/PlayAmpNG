#!/usr/bin/env python3
"""IN-09, IN-11 — a sessão sobrevive ao encerramento, e volta PARADA.

Verifica três coisas que só existem entre uma execução e a seguinte:

  1. A playlist é gravada ao encerrar, em M3U8 — o mesmo formato que o usuário
     importa e exporta.
  2. Ela é restaurada na execução seguinte.
  3. Restaurar NÃO inicia o áudio. Abrir o player não é pedir para tocar; só a
     preferência explícita muda isso.

O encerramento é por SIGTERM, que é como um logout ou um desligamento pedem ao
programa que termine — e era exatamente o caso em que nada era gravado, porque
`aboutToQuit` do Qt não dispara com sinal.

    test_session.py <binário> <assets>
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
import time

PULAR = 77
falhas = []


def checar(condicao, descricao):
    if not condicao:
        falhas.append(descricao)
        print(f"FALHA: {descricao}", file=sys.stderr)
    return condicao


def estado_mpris(pid):
    """Estado de reproducao lido do barramento.

    E o unico observavel honesto para "nao comecou a tocar". A primeira versao
    deste teste procurava a AUSENCIA de uma palavra no log — e palavra que
    nunca aparece torna a verificacao vazia: com o player comecando a tocar de
    proposito, ela passava do mesmo jeito.
    """
    if not shutil.which("dbus-send") or not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
        return None
    r = subprocess.run(["dbus-send", "--session", "--dest=org.freedesktop.DBus",
                        "--print-reply", "/org/freedesktop/DBus",
                        "org.freedesktop.DBus.ListNames"],
                       capture_output=True, text=True, timeout=10)
    alvo = next((l.split('"')[1] for l in r.stdout.splitlines()
                 if l.strip().strip('"').endswith(f"playampng.instance{pid}")), None)
    if not alvo:
        return None
    r = subprocess.run(["dbus-send", "--session", f"--dest={alvo}", "--print-reply",
                        "/org/mpris/MediaPlayer2",
                        "org.freedesktop.DBus.Properties.Get",
                        "string:org.mpris.MediaPlayer2.Player", "string:PlaybackStatus"],
                       capture_output=True, text=True, timeout=10)
    for estado in ("Playing", "Paused", "Stopped"):
        if f'"{estado}"' in r.stdout:
            return estado
    return None


def rodar(perfil, *args, segundos=6):
    ambiente = dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_CONFIG_HOME=perfil)
    proc = subprocess.Popen([binario, *args], env=ambiente,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(segundos)
    estado = estado_mpris(proc.pid)
    proc.terminate()   # SIGTERM: o caminho que se quer verificar
    try:
        saida = proc.communicate(timeout=10)[0]
    except subprocess.TimeoutExpired:
        proc.kill()
        saida = proc.communicate()[0]
    return saida, estado


binario = sys.argv[1]
assets = sys.argv[2]
perfil = tempfile.mkdtemp(prefix="pang_sessao_")
config = os.path.join(perfil, "PlayAmpNG")

# Faixas LONGAS de propósito. Os arquivos do repositório têm meio segundo, e
# com eles a verificação de "voltou tocando" media outra coisa: aos seis
# segundos a faixa já teria acabado e o estado seria Stopped com razão.
primeiro = os.path.join(perfil, "um.wav")
segundo = os.path.join(perfil, "dois.wav")
for caminho, amplitude in ((primeiro, 6000), (segundo, 3000)):
    with wave.open(caminho, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(struct.pack("<hh", amplitude, amplitude) * (44100 * 60))

saida, _ = rodar(perfil, primeiro, segundo)
if "dispositivo de audio" in saida.lower():
    print("sem dispositivo de audio: teste pulado", file=sys.stderr)
    sys.exit(PULAR)

m3u = os.path.join(config, "session.m3u8")
if checar(os.path.exists(m3u), "a playlist da sessao e gravada ao receber SIGTERM"):
    texto = open(m3u).read()
    checar(texto.startswith("#EXTM3U"), "gravada em M3U8, formato que o usuario ja usa")
    checar(os.path.basename(primeiro) in texto and os.path.basename(segundo) in texto,
           "as duas faixas estao na playlist gravada")

def duracoes(caminho):
    """Segundos de cada #EXTINF. -1 significa que ninguem varreu o arquivo."""
    return [int(l.split(":", 1)[1].split(",", 1)[0])
            for l in open(caminho).read().splitlines() if l.startswith("#EXTINF:")]


antes = duracoes(m3u)
checar(antes and all(d > 0 for d in antes),
       f"a primeira execucao varre e grava as duracoes (viu {antes})")

saida, estado = rodar(perfil, segundos=6)
checar("sessao restaurada: 2 faixa(s)" in saida,
       "a sessao seguinte restaura as duas faixas")

# A sessao restaurada precisa passar pela varredura de metadados como qualquer
# outra insercao. Sem isso ela aparece inteira com duracao "--:--" e total
# zerado — defeito que so se ve depois de fechar e abrir o player.
depois = duracoes(m3u)
checar(depois == antes,
       f"a sessao restaurada mantem as duracoes (antes {antes}, depois {depois})")
# IN-11 — o ponto do requisito: restaurar nao pode comecar a tocar.
if estado is None:
    print("  sem barramento: o estado de reproducao nao pode ser verificado", file=sys.stderr)
else:
    checar(estado == "Stopped",
           f"a sessao restaurada NAO inicia o audio sozinha (estado: {estado})")

# Com a preferencia explicita, ai sim.
import json
cfg = json.load(open(os.path.join(config, "config.json")))
cfg["autoplay_on_restore"] = True
json.dump(cfg, open(os.path.join(config, "config.json"), "w"))
saida, estado = rodar(perfil, segundos=6)
checar("sessao restaurada" in saida,
       "a sessao continua sendo restaurada com a preferencia ligada")
if estado is not None:
    checar(estado == "Playing",
           f"com autoplay_on_restore ligado, a sessao volta tocando (estado: {estado})")

shutil.rmtree(perfil, ignore_errors=True)
if falhas:
    print(f"{len(falhas)} verificacao(oes) falharam", file=sys.stderr)
    sys.exit(1)
print("todas as verificacoes passaram")
