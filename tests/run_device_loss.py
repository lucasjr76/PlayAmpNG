#!/usr/bin/env python3
"""Cria um sink virtual, roda o teste e remove o sink no meio da reprodução.

É o único jeito de exercitar a perda de dispositivo sem desconectar hardware na
mão. Depende de `pactl` e de um servidor PulseAudio/PipeWire no ar; sem eles o
teste é PULADO (código 77), porque a ausência de servidor de áudio numa máquina
de integração não é defeito do player.

    run_device_loss.py <binário>
"""

import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

PULAR = 77
SINK = "pang_perda_teste"
DESCRICAO = "PlayAmpNGPerdaTeste"


def pactl(*args):
    return subprocess.run(["pactl", *args], capture_output=True, text=True)


if shutil.which("pactl") is None or pactl("info").returncode != 0:
    print("sem pactl ou sem servidor de audio: teste pulado", file=sys.stderr)
    sys.exit(PULAR)

criado = pactl("load-module", "module-null-sink",
               f"sink_name={SINK}",
               f"sink_properties=device.description={DESCRICAO}")
if criado.returncode != 0:
    print(f"nao foi possivel criar o sink de teste: {criado.stderr.strip()}", file=sys.stderr)
    sys.exit(PULAR)

modulo = criado.stdout.strip()
media = os.path.join(tempfile.gettempdir(), f"pang_perda_{os.getpid()}.wav")
removido = threading.Event()


def remover_depois(segundos):
    # Dois segundos: o bastante para o player abrir, encher o buffer e tocar,
    # de modo que a parada seja inequivocamente perda e nao arranque lento.
    time.sleep(segundos)
    pactl("unload-module", modulo)
    removido.set()


try:
    time.sleep(1)  # deixa o sink aparecer na enumeracao
    threading.Thread(target=remover_depois, args=(2,), daemon=True).start()
    # Travamento é uma das falhas que este teste existe para pegar: destruir o
    # dispositivo morto em vez de abandoná-lo prende o processo indefinidamente.
    # Sem o prazo aqui, isso apareceria como suíte pendurada em vez de reprovada.
    processo = subprocess.Popen([sys.argv[1], DESCRICAO, media])
    try:
        codigo = processo.wait(timeout=60)
    except subprocess.TimeoutExpired:
        processo.kill()
        processo.wait()
        print("o teste travou: a recuperacao do dispositivo nao terminou em 60 s",
              file=sys.stderr)
        codigo = 1
finally:
    if not removido.is_set():
        pactl("unload-module", modulo)
    if os.path.exists(media):
        os.remove(media)

sys.exit(codigo)
