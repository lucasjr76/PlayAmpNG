#!/usr/bin/env python3
"""Servidor HTTP mínimo para o teste de fonte de rede (M6-1).

Serve quatro comportamentos que um player precisa tratar e que não dá para
exercitar sem um servidor de verdade:

    /tone.wav   arquivo com Content-Length — tem duração e é pesquisável
    /redir      302 para /tone.wav — verifica que o redirecionamento é seguido
    /stream     MP3 contínuo sem Content-Length, com cabeçalhos ICY — ao vivo
    /corta      começa a enviar e fecha a conexão no meio, e recusa daí em
                diante — exercita o limite de tentativas de reconexão
    /icy        rádio com metadados DENTRO do fluxo (protocolo ICY, a cada
                `icy-metaint` bytes), trocando de música no meio — MD-05
    /engasga    rádio que manda um trecho e depois fica muda por 30 s — a rede
                parada, que é o caso em que o buffering precisa aparecer (MD-04)
    /trava      aceita a conexão e nunca responde — a abertura de rede presa
                que o cancelamento de AR-09 precisa interromper

Imprime a porta escolhida na primeira linha da saída padrão e fica no ar até
receber SIGTERM. A porta é escolhida pelo sistema para que a suíte possa rodar
em paralelo com qualquer outra coisa.
"""

import http.server
import socket
import socketserver
import sys
import threading
import time
from pathlib import Path

ASSETS = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("assets/test")
WAV = (ASSETS / "tone.wav").read_bytes()

# As rotas de stream servem MP3, não WAV, e a razão é do formato: o cabeçalho
# WAV carrega o tamanho do bloco de dados, então o libavformat calcula duração
# mesmo sem Content-Length — e a fonte deixa de se parecer com rádio. MP3 é
# fluxo de quadros: concatena sem emenda e não informa duração nenhuma.
MP3 = (ASSETS / "tone.mp3").read_bytes()

# Depois da primeira conexão cortada, /corta passa a recusar. É o que força o
# player a esgotar as tentativas e declarar erro em vez de reconectar para
# sempre.
cortadas = 0
trava = threading.Lock()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass  # silencia; a saída do teste é que importa

    def do_GET(self):
        if self.path.startswith("/tone.wav"):
            # Atende Range: sem isso o libavformat pede um trecho, recebe o
            # arquivo inteiro e reclama de deslocamento inesperado. Um arquivo
            # servido por HTTP tem de ser pesquisável de verdade.
            inicio, fim = 0, len(WAV) - 1
            faixa = self.headers.get("Range", "")
            parcial = faixa.startswith("bytes=")
            if parcial:
                pedido = faixa[len("bytes="):].split("-")
                if pedido[0]:
                    inicio = int(pedido[0])
                if len(pedido) > 1 and pedido[1]:
                    fim = min(int(pedido[1]), fim)
            corpo = WAV[inicio:fim + 1]
            self.send_response(206 if parcial else 200)
            self.send_header("Content-Type", "audio/wav")
            self.send_header("Content-Length", str(len(corpo)))
            self.send_header("Accept-Ranges", "bytes")
            if parcial:
                self.send_header("Content-Range",
                                 f"bytes {inicio}-{fim}/{len(WAV)}")
            self.end_headers()
            self.wfile.write(corpo)

        elif self.path.startswith("/redir"):
            self.send_response(302)
            self.send_header("Location", "/tone.wav")
            self.send_header("Content-Length", "0")
            self.end_headers()

        elif self.path.startswith("/stream"):
            # Sem Content-Length e sem Accept-Ranges: é assim que uma rádio se
            # apresenta, e é o que faz o player tratá-la como fonte ao vivo.
            self.send_response(200)
            self.send_header("Content-Type", "audio/wav")
            self.send_header("icy-name", "Radio de Teste")
            self.send_header("icy-br", "128")
            self.end_headers()
            try:
                for _ in range(200):
                    self.wfile.write(MP3)
                    self.wfile.flush()
                    time.sleep(0.02)
            except (BrokenPipeError, ConnectionResetError):
                pass

        elif self.path.startswith("/icy"):
            # Protocolo ICY: o servidor so intercala metadados se o cliente
            # pediu, com "Icy-MetaData: 1". Sem o pedido, o fluxo sai puro — e o
            # teste reprova, que e o sinal certo de que o player parou de pedir.
            pediu = self.headers.get("Icy-MetaData") == "1"
            intervalo = 4096
            self.send_response(200)
            self.send_header("Content-Type", "audio/mpeg")
            self.send_header("icy-name", "Radio de Teste")
            if pediu:
                self.send_header("icy-metaint", str(intervalo))
            self.end_headers()

            def bloco(titulo):
                texto = f"StreamTitle='{titulo}';".encode("utf-8")
                tamanho = (len(texto) + 15) // 16
                return bytes([tamanho]) + texto.ljust(tamanho * 16, b"\0")

            enviados = 0
            fluxo = MP3 * 400
            try:
                for inicio in range(0, len(fluxo), intervalo):
                    self.wfile.write(fluxo[inicio:inicio + intervalo])
                    if pediu:
                        # A musica muda depois de ~200 KB, cerca de um segundo
                        # no ritmo de envio: o bastante para o primeiro titulo
                        # ser visto antes da troca.
                        titulo = ("Artista A - Musica Um" if enviados < 200000
                                  else "Artista B - Musica Dois")
                        self.wfile.write(bloco(titulo))
                    enviados += intervalo
                    self.wfile.flush()
                    time.sleep(0.02)
            except (BrokenPipeError, ConnectionResetError):
                pass

        elif self.path.startswith("/engasga"):
            # Um trecho e depois silencio de rede. A primeira versao deste
            # caminho so enviava devagar, e isso nao separava o player com
            # defeito do correto: com dados chegando aos pouquinhos, o
            # decodificador rodava de vez em quando e acabava declarando o
            # buffering por acaso. Com a rede PARADA, so quem consome o audio
            # pode perceber que o buffer secou.
            self.send_response(200)
            self.send_header("Content-Type", "audio/mpeg")
            self.end_headers()
            try:
                # 4 s: mais que os 2 s que a analise inicial do fluxo le antes
                # de decidir o formato. Com menos, a radio nem comeca a tocar
                # e o teste mediria a abertura, nao o engasgo no meio.
                self.wfile.write(MP3 * 8)
                self.wfile.flush()
                time.sleep(30)
            except (BrokenPipeError, ConnectionResetError):
                pass

        elif self.path.startswith("/trava"):
            # Recebe o pedido e nao responde nada, por muito mais tempo do que
            # qualquer teste espera. So o cancelamento do player tira a
            # abertura daqui.
            try:
                time.sleep(120)
            except Exception:
                pass

        elif self.path.startswith("/corta"):
            global cortadas
            with trava:
                cortadas += 1
                vez = cortadas
            if vez > 1:
                # Tentativas seguintes falham: o player tem de desistir.
                self.send_response(503)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            self.send_response(200)
            self.send_header("Content-Type", "audio/wav")
            self.send_header("icy-name", "Radio Instavel")
            self.end_headers()
            try:
                # O bastante para o player abrir e comecar a tocar, e entao a
                # conexao cai — que e como uma rádio some na pratica.
                for _ in range(4):
                    self.wfile.write(MP3)
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass
            self.close_connection = True  # corta no meio

        else:
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True
    address_family = socket.AF_INET


if __name__ == "__main__":
    with Server(("127.0.0.1", 0), Handler) as httpd:
        print(httpd.server_address[1], flush=True)
        httpd.serve_forever()
