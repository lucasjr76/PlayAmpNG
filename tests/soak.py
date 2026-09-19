#!/usr/bin/env python3
"""RB-07 — execução prolongada: CPU, memória e interrupções de áudio.

Sobe o player de verdade tocando em laço, com visualização e equalizador
ativos, e amostra o processo a cada 10 s. Os limites são os definidos no
`PLAN.md` **antes** da execução, e não ajustados depois:

    8 h contínuas com ZERO interrupções de áudio
    CPU média abaixo de 3% de um núcleo
    Crescimento de memória residente abaixo de 5 MB

Usa um diretório de configuração próprio — a sessão do usuário não é tocada.

    soak.py <binário> <horas> [arquivo.wav]
"""

import json
import os
import re
import subprocess
import sys
import tempfile
import time

binario = sys.argv[1]
horas = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0
LIMITE_CPU = 3.0
LIMITE_RSS_MB = 5.0

base = tempfile.mkdtemp(prefix="pang_soak_")
config_dir = os.path.join(base, "PlayAmpNG", "PlayAmpNG")
os.makedirs(config_dir, exist_ok=True)

# Repetir a lista, equalizador ativo e visualização ligada: o cenário mais caro,
# que é o que o limite de CPU do plano descreve.
json.dump({
    "version": 1,
    "volume": 0.8,
    "repeat": 2,
    "replaygain_mode": 0,
    "equalizer": {"bypass": False, "preamp_db": 0.0,
                  "bands": [4, 2, -2, -3, -1, 2, 3, 4, 4, 4]},
    "layout": {"scale": 2, "visualization": 0, "playlist_visible": True,
               "equalizer_visible": True},
}, open(os.path.join(config_dir, "config.json"), "w"))

media = sys.argv[3] if len(sys.argv) > 3 else os.path.join(base, "soak.wav")
if not os.path.exists(media):
    import math
    import struct
    import wave
    with wave.open(media, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        quadros = []
        for i in range(44100 * 120):
            t = i / 44100
            v = (0.5 * math.sin(2 * math.pi * 110 * t) +
                 0.3 * math.sin(2 * math.pi * 440 * t) +
                 0.2 * math.sin(2 * math.pi * 3000 * t)) * 0.6
            a = int(max(-1, min(1, v)) * 20000)
            quadros.append(struct.pack("<hh", a, a))
        w.writeframes(b"".join(quadros))

ambiente = dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_CONFIG_HOME=base)
log = open(os.path.join(base, "player.log"), "w+")
proc = subprocess.Popen([binario, media], env=ambiente, stdout=log, stderr=subprocess.STDOUT)
print(f"pid {proc.pid}, {horas} h, base {base}", flush=True)

TICK = os.sysconf("SC_CLK_TCK")


def cpu_ticks():
    with open(f"/proc/{proc.pid}/stat") as f:
        campos = f.read().rsplit(")", 1)[1].split()
    return int(campos[11]) + int(campos[12])  # utime + stime


def rss_kb():
    with open(f"/proc/{proc.pid}/status") as f:
        for linha in f:
            if linha.startswith("VmRSS:"):
                return int(linha.split()[1])
    return 0


def xruns():
    """Xruns contados pelo PipeWire, quando ele estiver no ar."""
    try:
        r = subprocess.run(["pw-top", "-b", "-n", "1"], capture_output=True,
                           text=True, timeout=10)
        total = 0
        for linha in r.stdout.splitlines()[1:]:
            campos = linha.split()
            if len(campos) > 6 and campos[6].isdigit():
                total += int(campos[6])
        return total
    except Exception:
        return None


time.sleep(15)  # deixa estabilizar antes da primeira amostra
inicio = time.time()
cpu0, rss0, xrun0 = cpu_ticks(), rss_kb(), xruns()
rss_max = rss0
amostras = []

try:
    while time.time() - inicio < horas * 3600:
        time.sleep(10)
        if proc.poll() is not None:
            print(f"O PLAYER MORREU apos {time.time()-inicio:.0f} s, codigo {proc.returncode}",
                  file=sys.stderr)
            break
        decorrido = time.time() - inicio
        cpu = 100.0 * (cpu_ticks() - cpu0) / TICK / decorrido
        rss = rss_kb()
        rss_max = max(rss_max, rss)
        amostras.append((decorrido, cpu, rss))
        if len(amostras) % 30 == 0:  # a cada 5 min
            print(f"{decorrido/60:6.1f} min  cpu={cpu:.2f}%  rss={rss/1024:.1f} MB "
                  f"(+{(rss-rss0)/1024:.2f})", flush=True)
finally:
    decorrido = max(1.0, time.time() - inicio)
    cpu_media = 100.0 * (cpu_ticks() - cpu0) / TICK / decorrido if proc.poll() is None else -1
    rss_fim = rss_kb() if proc.poll() is None else rss_max
    xrun1 = xruns()
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
    log.seek(0)
    texto = log.read()

interrupcoes = 0
for m in re.finditer(r"interrupcoes de audio: (\d+)", texto):
    interrupcoes = max(interrupcoes, int(m.group(1)))

print("\n=== RB-07 — execucao prolongada ===")
print(f"duracao                {decorrido/3600:.2f} h")
print(f"cpu media              {cpu_media:.2f} %   (limite {LIMITE_CPU})")
print(f"rss inicial / final    {rss0/1024:.1f} / {rss_fim/1024:.1f} MB")
print(f"crescimento de rss     {(rss_fim-rss0)/1024:+.2f} MB   (limite {LIMITE_RSS_MB})")
print(f"interrupcoes de audio  {interrupcoes}   (limite 0)")
if xrun0 is not None and xrun1 is not None:
    print(f"xruns do PipeWire      {xrun1 - xrun0}")
else:
    print("xruns do PipeWire      indisponivel")

falhou = (cpu_media < 0 or cpu_media > LIMITE_CPU
          or (rss_fim - rss0) / 1024 > LIMITE_RSS_MB or interrupcoes > 0)
print("\nRESULTADO:", "REPROVADO" if falhou else "dentro dos limites")
sys.exit(1 if falhou else 0)
