#!/usr/bin/env python3
"""Sobe o servidor de teste, roda o binário com a porta, derruba o servidor.

O servidor pede a porta 0 ao sistema e imprime a escolhida; fixar uma porta
faria a suíte falhar em máquina onde ela já estivesse em uso. O servidor é
derrubado no `finally`, de modo que uma falha do teste não deixa processo solto.

    run_stream_test.py <servidor.py> <assets> <binario>
"""

import subprocess
import sys

servidor, assets, binario = sys.argv[1:4]

proc = subprocess.Popen([sys.executable, servidor, assets],
                        stdout=subprocess.PIPE, text=True)
try:
    linha = proc.stdout.readline().strip()
    if not linha.isdigit():
        print(f"servidor nao informou porta: {linha!r}", file=sys.stderr)
        sys.exit(1)
    sys.exit(subprocess.call([binario, linha]))
finally:
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
