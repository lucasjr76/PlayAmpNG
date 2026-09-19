#!/usr/bin/env python3
"""IN-08 — um segundo lançamento enfileira na janela já aberta.

Sobe o player, lança um segundo processo com outro arquivo e confere, pelo
MPRIS, que a playlist da instância original cresceu — e que o segundo processo
saiu sozinho em vez de abrir outra janela.

Pulado (77) sem barramento de sessão: a verificação depende do MPRIS para
enxergar o estado da primeira instância de fora.

    test_single_instance.py <binário>
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
PLAYER = "org.mpris.MediaPlayer2.Player"
OBJ = "/org/mpris/MediaPlayer2"
falhas = []


def checar(condicao, descricao):
    if not condicao:
        falhas.append(descricao)
        print(f"FALHA: {descricao}", file=sys.stderr)
    return condicao


def wav(caminho, segundos, amplitude):
    with wave.open(caminho, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(struct.pack("<hh", amplitude, amplitude) * (44100 * segundos))


def servico(pid=None):
    r = subprocess.run(["dbus-send", "--session", "--dest=org.freedesktop.DBus",
                        "--print-reply", "/org/freedesktop/DBus",
                        "org.freedesktop.DBus.ListNames"],
                       capture_output=True, text=True, timeout=10)
    nomes = [l.split('"')[1] for l in r.stdout.splitlines()
             if "org.mpris.MediaPlayer2.playampng" in l]
    if pid is None:
        return nomes
    return [n for n in nomes if n.endswith(f"instance{pid}")]


def propriedade(svc, nome):
    r = subprocess.run(["dbus-send", "--session", f"--dest={svc}", "--print-reply", OBJ,
                        "org.freedesktop.DBus.Properties.Get",
                        f"string:{PLAYER}", f"string:{nome}"],
                       capture_output=True, text=True, timeout=10)
    return r.stdout if r.returncode == 0 else ""


def metadata(svc):
    r = subprocess.run(["dbus-send", "--session", f"--dest={svc}", "--print-reply", OBJ,
                        "org.freedesktop.DBus.Properties.Get",
                        f"string:{PLAYER}", "string:Metadata"],
                       capture_output=True, text=True, timeout=10)
    return r.stdout if r.returncode == 0 else ""


def esperar(condicao, limite=10.0):
    fim = time.time() + limite
    while time.time() < fim:
        if condicao():
            return True
        time.sleep(0.2)
    return bool(condicao())


if shutil.which("dbus-send") is None or not os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
    print("sem barramento de sessao: teste pulado", file=sys.stderr)
    sys.exit(PULAR)

binario = sys.argv[1]
tmp = tempfile.gettempdir()
primeiro = os.path.join(tmp, f"pang_um_{os.getpid()}.wav")
segundo = os.path.join(tmp, f"pang_dois_{os.getpid()}.wav")
wav(primeiro, 30, 3000)
wav(segundo, 30, 1500)

# Diretório de configuração próprio. A identidade da instância deriva do
# arquivo de configuração, então isto também dá ao teste uma instância própria:
# sem isso ele fala com o player que o usuário tiver aberto, e mede outra coisa.
perfil = tempfile.mkdtemp(prefix="pang_perfil_")
ambiente = dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_CONFIG_HOME=perfil)
proc = subprocess.Popen([binario, primeiro], env=ambiente,
                        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
try:
    if not esperar(lambda: len(servico(proc.pid)) >= 1, 15.0):
        proc.terminate()
        erro = proc.stderr.read() if proc.stderr else ""
        if "dispositivo de audio" in erro.lower():
            print("sem dispositivo de audio: teste pulado", file=sys.stderr)
            sys.exit(PULAR)
        print(f"a primeira instancia nao apareceu. stderr:\n{erro}", file=sys.stderr)
        sys.exit(1)

    svc = servico(proc.pid)[0]
    # O servico aparece no barramento ANTES de o arquivo ser aberto e
    # publicado. Afirmar o metadado na hora e corrida, e foi o que tornou este
    # teste instavel: ele falhava em 0,7 s, sem chegar perto do prazo.
    checar(esperar(lambda: os.path.basename(primeiro) in metadata(svc)),
           "a primeira instancia esta tocando o arquivo que recebeu")

    # O segundo lancamento deve ENTREGAR e SAIR, nao abrir outra janela.
    inicio = time.time()
    try:
        dois = subprocess.run([binario, segundo], env=ambiente, capture_output=True,
                              text=True, timeout=30)
        checar(dois.returncode == 0, "o segundo lancamento sai com sucesso")
        checar(time.time() - inicio < 15,
               "o segundo lancamento sai rapido, sem subir interface")
    except subprocess.TimeoutExpired:
        # Nao sair e exatamente o defeito que este teste existe para pegar: o
        # segundo lancamento abriu a propria janela em vez de entregar.
        checar(False, "o segundo lancamento nao terminou: abriu janela propria")
    checar(len(servico(proc.pid)) == 1,
           "continua havendo UMA instancia no barramento, nao duas")

    # A faixa entregue foi enfileirada. Como a primeira segue tocando, ela
    # aparece ao avancar — enfileirar nao pode cortar o que ja tocava.
    checar(os.path.basename(primeiro) in metadata(svc),
           "a faixa em reproducao nao foi interrompida pela entrega")  # ja estabelecido acima

    # A entrega e assincrona do lado de quem recebe: pedir Next antes de ela
    # ser processada nao avanca nada, e o teste reprovaria por sincronia e nao
    # por defeito. CanGoNext so fica verdadeiro quando a playlist passa de uma
    # faixa, entao ele E o sinal de que a entrega chegou.
    checar(esperar(lambda: "true" in propriedade(svc, "CanGoNext")),
           "a entrega chegou: a playlist passou a ter mais de uma faixa")

    subprocess.run(["dbus-send", "--session", f"--dest={svc}", "--print-reply", OBJ,
                    f"{PLAYER}.Next"], capture_output=True, timeout=10)
    checar(esperar(lambda: os.path.basename(segundo) in metadata(svc)),
           "o arquivo entregue foi enfileirado e toca ao avancar")
finally:
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    for caminho in (primeiro, segundo):
        if os.path.exists(caminho):
            os.remove(caminho)

if falhas:
    print(f"{len(falhas)} verificacao(oes) falharam", file=sys.stderr)
    sys.exit(1)
print("todas as verificacoes passaram")
