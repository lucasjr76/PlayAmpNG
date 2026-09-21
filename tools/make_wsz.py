#!/usr/bin/env python3
"""Gera um skin proprio no FORMATO do Winamp 2.x.

O formato define onde cada peca vive dentro de cada bitmap e onde cada peca e
desenhada na janela. Adotar essa geometria e o que faz as proporcoes baterem —
independentemente de quem desenhou os pixels. Aqui os pixels sao nossos; o
arranjo e o do formato.

Gera tanto a pasta com os .bmp soltos quanto um .wsz (ZIP), para exercitar os
dois caminhos do carregador.

    python3 tools/make_wsz.py assets/skin/default
"""

import os
import struct
import sys
import zipfile

# ------------------------------------------------------------------ paleta
#
# Toda cor aqui foi MEDIDA na captura de referencia do Winamp que o usuario
# forneceu, nao escolhida. Os valores saiam de amostragem por area lisa e por
# busca de pixel saturado; estao anotados com onde foram lidos. A paleta
# anterior era cinza e verde-neon, inventada por mim, e essa era a maior
# diferenca visivel que sobrava.
BG          = ( 58,  58,  86)   # face do painel
BG_LIGHT    = ( 78,  78, 108)   # realce do relevo
BG_SHADE    = ( 45,  45,  68)   # base do degrade da face
BG_DARK     = (  0,   0,   0)   # fundo dos mostradores
BEVEL_LIGHT = (108, 108, 142)
BEVEL_DARK  = ( 26,  26,  40)
OUTLINE     = ( 16,  16,  26)   # contorno preto-azulado dos botoes

# Botoes de transporte: face CLARA com simbolo escuro, como na referencia.
FACE_TOP    = (214, 222, 230)
FACE_BOTTOM = (166, 174, 190)
FACE_LIGHT  = (239, 248, 250)
FACE_SHADE  = (120, 128, 146)
FACE_DOWN_TOP    = (150, 158, 176)
FACE_DOWN_BOTTOM = (186, 194, 208)
BTN_INK     = ( 26,  30,  46)   # desenho dentro dos botoes claros

CREAM       = (252, 251, 233)   # listras e rotulos da barra de titulo
LABEL       = (211, 211, 221)   # rotulos de banda e tracos de escala
LABEL_DIM   = (122, 122, 142)   # rotulo inativo (mono/stereo apagado)

# Texto do mostrador. text.bmp tem UMA cor so; no formato ela e a do LCD, e os
# rotulos de botao sao gravados nos proprios sprites.
GREEN       = (145, 201,  98)
GREEN_TEXT  = (145, 201,  98)
GREEN_DIM   = ( 78, 110,  54)
LED_ON      = ( 84, 142,  38)
LED_OFF     = ( 34,  38,  50)

# Degrade de valor das barras: verde no minimo, amarelo no meio, laranja no
# alto. Os tres extremos sao medidos; o meio e interpolado.
YELLOW      = (217, 203,  71)
GREENBAR    = ( 84, 142,  38)
ORANGE      = (195, 117,  49)
GOLD        = (217, 203,  71)   # rotulos de dB do equalizador e logotipo
DISC        = ( 38,  76,  78)   # disco do logotipo, medido na referencia
DISC_EDGE   = ( 26,  52,  54)


def _ramp(a, b, steps):
    return [tuple(round(a[i] + (b[i] - a[i]) * n / (steps - 1)) for i in range(3))
            for n in range(steps)]


VALUE_GRADIENT = _ramp(GREENBAR, YELLOW, 8) + _ramp(YELLOW, ORANGE, 9)[1:]


class Bitmap:
    def __init__(self, width, height, fill=BG):
        self.width, self.height = width, height
        self.pixels = [list(fill) * width for _ in range(height)]

    def set(self, x, y, color):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.pixels[y][x * 3:x * 3 + 3] = list(color)

    def rect(self, x, y, w, h, color):
        for j in range(y, y + h):
            for i in range(x, x + w):
                self.set(i, j, color)

    def hline(self, x, y, w, color): self.rect(x, y, w, 1, color)
    def vline(self, x, y, h, color): self.rect(x, y, 1, h, color)

    def gradient(self, x, y, w, h, top, bottom):
        for j in range(h):
            t = j / max(1, h - 1)
            self.hline(x, y + j, w, tuple(int(top[c] + (bottom[c] - top[c]) * t) for c in range(3)))

    def bevel(self, x, y, w, h, top, bottom, raised=True):
        light, dark = (BEVEL_LIGHT, BEVEL_DARK) if raised else (BEVEL_DARK, BEVEL_LIGHT)
        self.gradient(x, y, w, h, top, bottom)
        self.hline(x, y, w, light); self.vline(x, y, h, light)
        self.hline(x, y + h - 1, w, dark); self.vline(x + w - 1, y, h, dark)
        if w > 3 and h > 3:
            inner_light = tuple(min(255, c + 26) for c in top)
            inner_dark = tuple(max(0, c - 22) for c in bottom)
            a, b = (inner_light, inner_dark) if raised else (inner_dark, inner_light)
            self.hline(x + 1, y + 1, w - 2, a); self.vline(x + 1, y + 1, h - 2, a)
            self.hline(x + 1, y + h - 2, w - 2, b); self.vline(x + w - 2, y + 1, h - 2, b)

    def well(self, x, y, w, h):
        self.rect(x, y, w, h, BG_DARK)
        self.hline(x, y, w, BEVEL_DARK); self.vline(x, y, h, BEVEL_DARK)
        self.hline(x, y + h - 1, w, BEVEL_LIGHT); self.vline(x + w - 1, y, h, BEVEL_LIGHT)

    def blit(self, other, x, y):
        for j in range(other.height):
            for i in range(other.width):
                self.set(x + i, y + j, tuple(other.pixels[j][i * 3:i * 3 + 3]))

    def write_bmp(self, path):
        row_bytes = (self.width * 3 + 3) & ~3
        body = bytearray()
        for y in range(self.height - 1, -1, -1):   # BMP e de baixo para cima
            row = bytearray()
            for x in range(self.width):
                r, g, b = self.pixels[y][x * 3:x * 3 + 3]
                row += bytes((b, g, r))
            row += b'\x00' * (row_bytes - len(row))
            body += row
        header = struct.pack('<2sIHHI', b'BM', 14 + 40 + len(body), 0, 0, 14 + 40)
        info = struct.pack('<IiiHHIIiiII', 40, self.width, self.height, 1, 24, 0,
                           len(body), 2835, 2835, 0, 0)
        with open(path, 'wb') as handle:
            handle.write(header + info + bytes(body))


# ------------------------------------------------------------------ fonte 5x6
#
# text.bmp: tres fileiras de 31 glifos, na ordem que o formato define.
#
# A tabela abaixo NAO foi desenhada a mao. Ela e extraida da Silkscreen por
# tools/fontgen.cpp, porque as tres versoes escritas a mao sairam com forma de
# letra errada — a ultima deixava o travessao do 'A' com 4 px e "PLAYAMPNG"
# aparecia como "PLRYRMPNG". Contagem de coluna da para validar por programa;
# forma de letra nao da. O jeito de nao errar e nao desenhar.
#
# Para regerar:  ./build/pang_fontgen > tabela.py  e colar aqui.
#
# Aproximacao conhecida: '&' e '$' tem 7 px de altura na Silkscreen e a celula
# do formato tem 6; os dois perdem a linha de baixo. Nenhum outro glifo perde
# tinta — fontgen falha se perder.
GLYPHS = {
 'A':".##..#..#.####.#..#.#..#......", 'B':"###..#..#.####.#..#.###.......",
 'C':".##..#..#.#....#..#..##.......", 'D':"###..#..#.#..#.#..#.###.......",
 'E':"###..#....###..#....###.......", 'F':"###..#....###..#....#.........",
 'G':".###.#....#.##.#..#..##.......", 'H':"#..#.#..#.####.#..#.#..#......",
 'I':"#....#....#....#....#.........", 'J':"...#....#....#.#..#..##.......",
 'K':"#..#.#.#..##...#.#..#..#......", 'L':"#....#....#....#....###.......",
 'M':"#...###.###.#.##...##...#.....", 'N':"#...###..##.#.##..###...#.....",
 'O':".##..#..#.#..#.#..#..##.......", 'P':"###..#..#.###..#....#.........",
 'Q':".##..#..#.#..#.#..#..##.....#.", 'R':"###..#..#.###..#.#..#..#......",
 'S':".###.#.....##.....#.###.......", 'T':"###...#....#....#....#........",
 'U':"#..#.#..#.#..#.#..#..##.......", 'V':"#...##...#.#.#..#.#...#.......",
 'W':"#...##.#.##.#.##.#.#.#.#......", 'X':"#...#.#.#...#...#.#.#...#.....",
 'Y':"#...#.#.#...#....#....#.......", 'Z':"###....#...#...#....###.......",
 '"':"#.#..#.#......................", '@':".###.#.#.##.##.#.....###......",
 ' ':"..............................", '0':".##..#..#.#..#.#..#..##.......",
 '1':"##....#....#....#...###.......", '2':"###.....#..##..#....####......",
 '3':"###.....#..##.....#.###.......", '4':"#.#..#.#..####...#....#.......",
 '5':"####.#....###.....#.###.......", '6':".##..#....###..#..#..##.......",
 '7':"####....#...#...#....#........", '8':".##..#..#..##..#..#..##.......",
 '9':".##..#..#..###....#..##.......", '.':"....................#.........",
 ':':".....#.........#..............", '(':".#...#....#....#.....#........",
 ')':"#.....#....#....#...#.........", '-':"..........###.................",
 "'":"#....#........................", '!':"#....#....#.........#.........",
 '_':".........................####.", '+':".#....#...####..#....#........",
 '\\':"#....#.....#.....#....#.......", '/':"..#....#...#...#....#.........",
 '[':"##...#....#....#....##........", ']':"##....#....#....#...##........",
 '^':".#...#.#......................", '&':"..#...###.#.....##..#.....###.",
 '%':"##.#.##.#...#...#.##.#.##.....", ',':".....................#...#....",
 '=':".....###.......###............", '$':"..#...###.#.....##.....#.###..",
 '#':".#.#.#####.#.#.#####.#.#......", '?':"###.....#..##........#........",
 '*':"..#..#.#.#.###.#.#.#..#.......",
}
ROWS = ["ABCDEFGHIJKLMNOPQRSTUVWXYZ\"@ ",
        "0123456789.:()-'!_+\\/[]^&%,=$#",
        "ÅÖÄ?* "]


def draw_glyph(bmp, character, x, y, color=GREEN_TEXT):
    bits = (GLYPHS.get(character.upper(), GLYPHS[' ']) + '.' * 30)[:30]
    for j in range(6):
        for i in range(5):
            if bits[j * 5 + i] == '#':
                bmp.set(x + i, y + j, color)


def glyph_advance(character):
    """Largura de tinta do glifo mais um pixel de vao.

    A celula do formato tem 5 px e o avanco tambem, entao glifos de 5 colunas
    (M, N, V, W, X, Y, @, %, # e *) encostam no proximo: "AMP" saia colado e
    "PLAYAMPNG" lia-se como "PAYAMPING". Nos rotulos que gravamos no bitmap o
    avanco pode ser proporcional; o texto desenhado em tempo de execucao
    continua com o avanco fixo do formato, que e o que um .wsz espera.
    """
    bits = (GLYPHS.get(character.upper(), GLYPHS[' ']) + '.' * 30)[:30]
    columns = [i for i in range(5) if any(bits[j * 5 + i] == '#' for j in range(6))]
    return (max(columns) + 2) if columns else 4


def text_width(text):
    return sum(glyph_advance(c) for c in text)


def draw_text(bmp, text, x, y, color=GREEN_TEXT):
    for character in text:
        draw_glyph(bmp, character, x, y, color)
        x += glyph_advance(character)


# ------------------------------------------------------------- digitos 9x13
SEGMENTS = {
 '0': (1,1,1,0,1,1,1), '1': (0,0,1,0,0,1,0), '2': (1,0,1,1,1,0,1),
 '3': (1,0,1,1,0,1,1), '4': (0,1,1,1,0,1,0), '5': (1,1,0,1,0,1,1),
 '6': (1,1,0,1,1,1,1), '7': (1,0,1,0,0,1,0), '8': (1,1,1,1,1,1,1),
 '9': (1,1,1,1,0,1,1),
}


def draw_digit(bmp, character, x, y, color=GREEN):
    if character not in SEGMENTS:
        return
    top, ul, ur, mid, ll, lr, bot = SEGMENTS[character]
    if top: bmp.rect(x + 2, y + 0, 5, 2, color)
    if ul:  bmp.rect(x + 0, y + 2, 2, 4, color)
    if ur:  bmp.rect(x + 7, y + 2, 2, 4, color)
    if mid: bmp.rect(x + 2, y + 6, 5, 1, color)
    if ll:  bmp.rect(x + 0, y + 7, 2, 4, color)
    if lr:  bmp.rect(x + 7, y + 7, 2, 4, color)
    if bot: bmp.rect(x + 2, y + 11, 5, 2, color)


def value_color(fraction):
    index = int(max(0.0, min(1.0, fraction)) * (len(VALUE_GRADIENT) - 1))
    return VALUE_GRADIENT[index]


# ============================================================ os bitmaps
#
# Cada funcao produz um bitmap do formato, com as pecas nas posicoes que a
# especificacao define.

# Raio do logotipo, 11x9. Aparece duas vezes: pequeno na ponta esquerda da
# barra de titulo e dentro do disco no canto inferior direito da janela.
BOLT = (
    "......####,",
    ".....###...",
    "....###....",
    "...######..",
    "..######...",
    "....###....",
    "...###.....",
    "..###......",
    ".###.......",
)


def draw_bolt(b, x, y, color):
    for j, row in enumerate(BOLT):
        for i, ch in enumerate(row):
            if ch == '#':
                b.set(x + i, y + j, color)


def draw_logo_disc(b, x, y, size=20):
    """Disco escuro com o raio dourado, o logotipo do canto inferior direito."""
    r = size / 2.0 - 0.5
    cx = cy = r
    for j in range(size):
        for i in range(size):
            d = ((i - cx) ** 2 + (j - cy) ** 2) ** 0.5
            if d <= r - 1.2:
                b.set(x + i, y + j, DISC)
            elif d <= r:
                b.set(x + i, y + j, DISC_EDGE)
    draw_bolt(b, x + 5, y + 5, GOLD)


def make_main():
    """main.bmp 275x116 — o fundo da janela principal."""
    b = Bitmap(275, 116)
    # Face com degrade vertical suave, como na referencia: chapada ela fica
    # com cara de retangulo pintado, nao de painel.
    b.gradient(0, 14, 275, 102, BG, BG_SHADE)
    title_stripes(b, 0, 0, 275, 14, "PLAYAMP NG", CREAM)

    # UM poco para o mostrador inteiro: indicador, tempo e visualizacao
    # dividem a mesma area preta. Dois pocos cruzados deixavam canto quebrado —
    # o indicador vive em 26..34 e a borda de um deles caia em 33.
    #
    # Interior 16..106, 24..60 — dimensionado para CENTRAR o conteudo e para
    # as arestas baterem com o resto da janela.
    #
    # O conteudo vai de 24 a 99 em x (centro 61,5) e de 26 a 58 em y (centro
    # 42); o interior do poco tem centro 61 e 42. A versao medida na captura,
    # de 9 a 103, deixava 15 px de preto morto a esquerda contra 4 a direita —
    # era o que se via como tempo e espectro fora de centro.
    #
    # A aresta esquerda do interior cai em 16, a mesma do transporte e da barra
    # de posicao. A direita para em 106, encostada no volume, que comeca em
    # 107: ir ate 107 punha a borda do poco dentro do controle de volume, e o
    # teste de fundo liso acusou.
    b.well(14, 23, 85, 39)

    # Grade pontilhada sobre TODO o poco, nao so sobre a area do espectro.
    # Na referencia ela e arte de fundo e cobre o mostrador inteiro; desenhar
    # so dentro do retangulo da visualizacao deixava metade do bloco chapada.
    # Colunas e linhas PARES. A grade desenhada em tempo de execucao dentro do
    # retangulo da visualizacao conta em coordenada absoluta par; se a gravada
    # aqui ficar em coluna impar, as duas se cruzam na borda do retangulo e os
    # pontos viram tracos justamente ali.
    for gy in range(24, 61, 2):
        for gx in range(16, 98, 2):
            b.set(gx, gy, (28, 28, 44))

    # Dois-pontos do relogio: faz parte do FUNDO, nao dos digitos.
    b.rect(67, 30, 2, 2, GREEN)
    b.rect(67, 35, 2, 2, GREEN)

    # Poco do titulo: 107..258. A esquerda alinha com o volume e com o poco do
    # mostrador; a direita, com o botao PL, o logotipo e o fim da barra de
    # posicao — todos em 258, a 16 px da borda, igual a margem da esquerda.
    b.well(103, 23, 158, 14)

    # Os campos numericos precisam de poco proprio. O texto do formato sai de
    # text.bmp, que tem fundo preto: desenhado sobre a face da janela, cada
    # rotulo carrega junto uma caixa preta. Ou o campo e preto de proposito, ou
    # a caixa aparece como sujeira. O formato faz a primeira coisa.
    #
    # O campo de bitrate do formato tem 15 px, o bastante para "320". Arquivo
    # sem perda informa quatro digitos ("1411"), que ocupam 20 px; o poco
    # acompanha, e o rotulo comeca depois dele.
    # Pocos de 13 linhas (41..53), interior 42..52: com 11 linhas de interior a
    # tinta do glifo, que tem 5, centra em 45..49 sem sobrar meia linha.
    b.well(106, 41, 27, 13)         # bitrate: interior 107..131, cinco digitos
    draw_text(b, "kbps", 135, 45, LABEL)
    b.well(158, 41, 19, 13)         # taxa: interior 159..175, tres digitos
    draw_text(b, "khz", 179, 45, LABEL)

    # Nenhuma moldura de controle aqui. No formato, volume.bmp, balance.bmp e
    # posbar.bmp ja trazem o fundo inteiro dos seus controles; desenhar um poco
    # atras deles empilhava duas bordas e era o que aparecia como "fundo
    # errado e parcial".

    # Disco do logotipo: 241..260 x 88..107. A aresta direita bate com a do
    # poco do titulo e a do botao PL, e a faixa vertical e a do transporte.
    draw_logo_disc(b, 241, 88)

    # Borda externa.
    b.hline(0, 0, 275, BEVEL_LIGHT); b.vline(0, 0, 116, BEVEL_LIGHT)
    b.hline(0, 115, 275, BEVEL_DARK); b.vline(274, 0, 116, BEVEL_DARK)
    return b


def draw_symbol(b, name, x, y, w, h, color=BTN_INK):
    cx, cy = x + w // 2, y + h // 2

    def triangle(x0, direction, size):
        for i in range(size):
            span = size - i
            for j in range(-span + 1, span):
                b.set(x0 + direction * i, cy + j, color)

    if name == 'play':     triangle(cx - 2, 1, 4)
    elif name == 'pause':  b.rect(cx - 3, cy - 3, 2, 7, color); b.rect(cx + 1, cy - 3, 2, 7, color)
    elif name == 'stop':   b.rect(cx - 3, cy - 3, 7, 7, color)
    elif name == 'previous': b.rect(cx - 4, cy - 3, 2, 7, color); triangle(cx + 2, -1, 4)
    elif name == 'next':   b.rect(cx + 3, cy - 3, 2, 7, color); triangle(cx - 2, 1, 4)
    elif name == 'eject':
        for i in range(3): b.hline(cx - i, cy - 3 + i, 1 + i * 2, color)
        b.rect(cx - 3, cy + 2, 7, 2, color)


def face_button(b, x, y, w, h, pressed=False):
    """Botao de face clara com contorno escuro, como na referencia.

    Os botoes de transporte nao sao da cor do painel: sao claros, quase
    brancos, com o simbolo escuro por cima. Era a diferenca mais visivel entre
    a nossa janela e a de referencia depois da paleta.
    """
    top, bottom = (FACE_DOWN_TOP, FACE_DOWN_BOTTOM) if pressed else (FACE_TOP, FACE_BOTTOM)
    b.rect(x, y, w, h, OUTLINE)
    b.gradient(x + 1, y + 1, w - 2, h - 2, top, bottom)
    b.hline(x + 1, y + 1, w - 2, FACE_LIGHT if not pressed else FACE_SHADE)
    b.vline(x + 1, y + 1, h - 2, FACE_LIGHT if not pressed else FACE_SHADE)
    b.hline(x + 1, y + h - 2, w - 2, FACE_SHADE if not pressed else FACE_LIGHT)
    b.vline(x + w - 2, y + 1, h - 2, FACE_SHADE if not pressed else FACE_LIGHT)


def make_cbuttons():
    """cbuttons.bmp 136x36 — cinco botoes de 23x18 e o eject de 22x16."""
    b = Bitmap(136, 36, BG)
    for index, name in enumerate(['previous', 'play', 'pause', 'stop', 'next']):
        x = index * 23
        for row, pressed in enumerate((False, True)):
            face_button(b, x, row * 18, 23, 18, pressed)
            # Pressionado, o desenho anda um pixel para baixo e para a direita:
            # e o que da a sensacao de afundar.
            draw_symbol(b, name, x + (1 if pressed else 0), row * 18 + (1 if pressed else 0),
                        23, 18)
    for row, pressed in enumerate((False, True)):
        face_button(b, 114, row * 16, 22, 16, pressed)
        draw_symbol(b, 'eject', 114 + (1 if pressed else 0), row * 16 + (1 if pressed else 0),
                    22, 16)
    return b


def make_titlebar():
    """titlebar.bmp 344x87 — barras e os tres botoes da janela.

    A largura e 344, e nao 275: a barra de 275 px comeca em x=27, depois dos
    botoes, entao o bitmap precisa de 27+275 no minimo. Fazer 275 recortava a
    barra pela direita — o teste de geometria pegou isso.
    """
    b = Bitmap(344, 87, BG)
    for row, active in ((0, False), (15, True)):
        # Inativa, a barra nao perde as listras: elas so escurecem. Apagar as
        # listras fazia a janela sem foco parecer de outro programa.
        title_stripes(b, 27, row, 275, 14, "PLAYAMP NG",
                      CREAM if active else LABEL_DIM, CREAM if active else LABEL_DIM)

    for name, x in (('shade', 0), ('minimize', 9), ('close', 18)):
        for row, pressed in enumerate((False, True)):
            y = row * 9
            face_button(b, x, y, 9, 9, pressed)
            o = 1 if pressed else 0
            if name == 'minimize':
                b.rect(x + 2 + o, y + 5 + o, 5, 2, BTN_INK)
            elif name == 'shade':
                b.rect(x + 2 + o, y + 3 + o, 5, 1, BTN_INK)
                b.rect(x + 2 + o, y + 5 + o, 5, 1, BTN_INK)
            else:
                for i in range(5):
                    b.set(x + 2 + i + o, y + 2 + i + o, BTN_INK)
                    b.set(x + 6 - i + o, y + 2 + i + o, BTN_INK)

    # AP-11 — modo compacto, nas celulas que o formato Winamp reserva para ele
    # (conferido no Webamp): fundo com e sem foco em (27,29) e (27,42), botao
    # de sair do compacto em (0,27), barra de posicao em (0,36).
    for y, active in ((29, True), (42, False)):
        compact_bar(b, 27, y, active)

    # Sair do compacto: o simbolo e o do "compacto" invertido — uma linha so,
    # a barra a que a janela foi reduzida, virando a janela inteira de volta.
    for col, pressed in enumerate((False, True)):
        x = col * 9
        face_button(b, x, 27, 9, 9, pressed)
        o = 1 if pressed else 0
        b.rect(x + 2 + o, 27 + 2 + o, 5, 5, BTN_INK)
        b.rect(x + 3 + o, 27 + 4 + o, 3, 2, FACE_TOP)

    # Barra de posicao: poco de 17x7 e cursor de 3x7.
    b.rect(0, 36, 17, 7, BG_DARK)
    b.hline(0, 36, 17, OUTLINE)
    b.vline(0, 36, 7, OUTLINE)
    b.hline(0, 42, 17, FACE_SHADE)
    b.vline(16, 36, 7, FACE_SHADE)
    face_button(b, 20, 36, 3, 7)
    return b


def compact_bar(b, x, y, active):
    """Fundo do modo compacto, 275x14, com os mini-controles desenhados.

    Os mini-controles vem PRONTOS no fundo, como no formato Winamp: a interface
    so desenha o que muda — titulo, tempo, barra de posicao e os tres botoes da
    janela. Posicoes do formato, conferidas no Webamp.
    """
    ink = CREAM if active else LABEL_DIM
    b.gradient(x, y, 275, 14, BG_LIGHT, BG_SHADE)
    for j in (4, 6, 8):
        b.hline(x, y + j, 275, ink)
        b.hline(x, y + j + 1, 275, BG_SHADE)
    b.rect(x + 1, y + 1, 16, 12, BG_SHADE)
    draw_bolt(b, x + 3, y + 3, GOLD if active else LABEL_DIM)

    # Pocos do titulo (18..121) e do tempo (125..153), para o texto de
    # text.bmp — que tem fundo preto — nao carregar caixa preta sobre a face.
    b.well(x + 18, y + 2, 104, 10)
    b.well(x + 125, y + 2, 29, 10)
    # Liso por tras dos controles e dos botoes, para as listras nao passarem
    # entre eles.
    b.rect(x + 166, y + 1, 108, 12, BG_SHADE)

    for name, left, width in (('previous', 169, 7), ('play', 176, 10), ('pause', 186, 9),
                              ('stop', 195, 9), ('next', 204, 10), ('eject', 215, 10)):
        face_button(b, x + left, y + 2, width, 10)
        mini_symbol(b, name, x + left, y + 2, width, 10)


def mini_symbol(b, name, x, y, w, h, color=BTN_INK):
    """Simbolos de transporte em tamanho de 10 px de altura."""
    cx, cy = x + w // 2, y + h // 2

    def triangle(x0, direction):
        for i in range(3):
            b.vline(x0 + direction * i, cy - 2 + i, 5 - 2 * i, color)

    if name == 'play':       triangle(cx - 1, 1)
    elif name == 'pause':    b.rect(cx - 2, cy - 2, 1, 5, color); b.rect(cx + 1, cy - 2, 1, 5, color)
    elif name == 'stop':     b.rect(cx - 2, cy - 2, 4, 5, color)
    elif name == 'previous': b.vline(cx - 2, cy - 2, 5, color); triangle(cx + 1, -1)
    elif name == 'next':     b.vline(cx + 2, cy - 2, 5, color); triangle(cx - 1, 1)
    elif name == 'eject':
        for i in range(3): b.hline(cx - i, cy - 2 + i, 1 + i * 2, color)
        b.hline(cx - 2, cy + 2, 5, color)


def make_numbers():
    """numbers.bmp 108x13 — dez digitos e uma celula vazia."""
    b = Bitmap(108, 13, BG_DARK)
    for index, character in enumerate('0123456789'):
        draw_digit(b, character, index * 9, 0)
    return b


def make_text():
    """text.bmp 155x18 — a fonte de 5x6 em tres fileiras."""
    b = Bitmap(155, 18, BG_DARK)
    for line, characters in enumerate(ROWS):
        for index, character in enumerate(characters[:31]):
            draw_glyph(b, character, index * 5, line * 6)
    return b


# A face das janelas tem degrade vertical das linhas 14 a 115. Todo sprite que
# mostra fundo proprio precisa casar com essa cor na linha em que e desenhado —
# um retangulo chapado sobre o degrade aparece como emenda.
FACE_TOP_ROW = 14
FACE_ROWS = 102


def face_at(row):
    t = min(max(row - FACE_TOP_ROW, 0), FACE_ROWS - 1) / (FACE_ROWS - 1)
    return tuple(int(BG[c] + (BG_SHADE[c] - BG[c]) * t) for c in range(3))


def fill_face(b, x, y, w, h, window_row):
    """Preenche uma area do sprite com a face da janela, linha a linha."""
    for j in range(h):
        b.hline(x, y + j, w, face_at(window_row + j))


def key_block(b, x, y, w, h, grips="vertical", pressed=False):
    """Polegar claro com contorno escuro, como o da referencia.

    Todo cursor do skin e a mesma peca: face clara, contorno preto-azulado de
    um pixel e marcas de pega no meio. E o que faz o cursor se destacar da
    barra colorida sem depender de a barra ser escura.
    """
    top, bottom = (FACE_DOWN_TOP, FACE_DOWN_BOTTOM) if pressed else (FACE_TOP, FACE_BOTTOM)
    b.rect(x, y, w, h, OUTLINE)
    b.gradient(x + 1, y + 1, w - 2, h - 2, top, bottom)
    b.hline(x + 1, y + 1, w - 2, FACE_LIGHT)
    b.vline(x + 1, y + 1, h - 2, FACE_LIGHT)
    b.hline(x + 1, y + h - 2, w - 2, FACE_SHADE)
    b.vline(x + w - 2, y + 1, h - 2, FACE_SHADE)
    if grips == "vertical":
        for i in (-2, 0, 2):
            b.vline(x + w // 2 + i, y + 3, h - 6, BTN_INK)
    else:
        for i in (-1, 1):
            b.hline(x + 3, y + h // 2 + i, w - 6, BTN_INK)


def color_bar(b, x, y, w, h, color):
    """Barra de pontas arredondadas, com contorno escuro em cima.

    Medida na referencia: contorno escuro de 1 px, cinco linhas de cor e uma
    de sombra — nao sete linhas chapadas. A barra assenta direto sobre a face
    do painel; um poco atras dela era o que deixava o controle com moldura
    dupla.
    """
    # Iluminacao de peca EMBUTIDA: sombra em cima, luz embaixo. Ao contrario
    # a barra parece saltar do painel, e no original ela e um encaixe.
    light = tuple(min(255, round(c * 1.18 + 12)) for c in color)
    mid = tuple(min(255, round(c * 1.05)) for c in color)
    shade = tuple(round(c * 0.58) for c in color)
    b.hline(x + 1, y, w - 2, BEVEL_DARK)
    for j in range(1, h - 1):
        row = shade if j == 1 else (color if j < h - 2 else mid)
        b.hline(x, y + j, w, row)
    b.vline(x, y + 1, 1, BEVEL_DARK)
    b.vline(x + w - 1, y + 1, 1, BEVEL_DARK)
    b.hline(x + 1, y + h - 1, w - 2, light)


def make_slider_frames(width, frames, thumb_width=14):
    """Tira de fundos onde o quadro escolhido representa o VALOR.

    A barra INTEIRA muda de cor, e quem diz o valor e a posicao do polegar.
    Preenchimento parcial foi uma tentativa minha de por dois sinais no mesmo
    controle; o resultado foi um buraco preto ocupando a maior parte da barra,
    e nao e o que o formato faz.
    """
    b = Bitmap(width, 433, BG)
    for index in range(frames):
        y = index * 15
        # O controle mora na linha 57 da janela: o fundo do quadro precisa ser
        # a face daquela faixa, senao o quadro aparece como retangulo chapado
        # sobre o degrade.
        fill_face(b, 0, y, width, 13, 57)
        # A barra e o polegar precisam do MESMO centro. A celula tem 13 linhas
        # (centro 6) e o polegar tem 11, desenhado na linha 1 — centro 6. A
        # barra tem 7 linhas, entao vai da linha 3 a 9, centro 6. Desenhada na
        # linha 4 ela ficava com centro 7 e o polegar aparecia uma linha e meia
        # acima da barra.
        color_bar(b, 1, y + 3, width - 2, 7, value_color(index / max(1, frames - 1)))
    for index, pressed in enumerate((True, False)):
        key_block(b, index * 15, 422, thumb_width, 11, "vertical", pressed)
    return b


def make_posbar():
    """posbar.bmp 307x10 — fundo de 248 e dois cursores de 29."""
    b = Bitmap(307, 10, BG)
    # Canal embutido: duas linhas escuras em cima, duas claras embaixo. Um poco
    # preto de 10 px de altura aparecia como faixa preta larga atravessando a
    # janela, porque o cursor cobre a altura inteira da barra.
    # Medido na referencia: a barra inteira e um canal recuado, com o topo
    # escuro e a base clara. A versao anterior tinha um fio no meio de uma
    # faixa da cor da face, e lia-se como risco, nao como encaixe.
    # O fundo do canal precisa ser bem mais escuro que a face. Quase da cor
    # dela, a barra nao lia como encaixe: parecia um risco flutuando no meio
    # do painel.
    b.rect(0, 0, 247, 10, tuple(round(c * 0.52) for c in BG))
    b.hline(0, 0, 247, BEVEL_DARK)
    b.hline(0, 1, 247, tuple(round(c * 0.40) for c in BG))
    b.vline(0, 0, 10, BEVEL_DARK)
    b.vline(246, 0, 10, BG_LIGHT)
    b.hline(0, 9, 247, BEVEL_LIGHT)
    b.hline(0, 8, 247, BG_LIGHT)
    for index, pressed in enumerate((False, True)):
        key_block(b, 248 + index * 30, 0, 29, 10, "vertical", pressed)
    return b


def led(b, x, y, lit):
    """Quadradinho de estado no canto do botao, como na referencia.

    Um botao de alternancia precisa dizer o estado sem depender de o texto
    estar mais claro ou mais escuro — a diferenca entre dois tons nao se le
    a 1x.
    """
    b.rect(x, y, 5, 5, LED_ON if lit else LED_OFF)
    b.hline(x, y, 5, BEVEL_DARK)
    b.vline(x, y, 5, BEVEL_DARK)


def toggle_button(b, x, y, w, h, label, lit, pressed):
    """Botao de alternancia: face do painel, borda clara, LED e rotulo creme.

    Nao usa a face clara dos botoes de transporte. Na referencia os dois tipos
    sao distintos: transporte e claro com simbolo escuro, alternancia e da cor
    do painel com rotulo claro.
    """
    b.gradient(x, y, w, h, BG_LIGHT if not pressed else BG_SHADE,
               BG_SHADE if not pressed else BG_LIGHT)
    light, dark = (BEVEL_LIGHT, BEVEL_DARK) if not pressed else (BEVEL_DARK, BEVEL_LIGHT)
    b.hline(x, y, w, light); b.vline(x, y, h, light)
    b.hline(x, y + h - 1, w, dark); b.vline(x + w - 1, y, h, dark)
    o = 1 if pressed else 0
    led(b, x + 3 + o, y + (h - 5) // 2 + o, lit)
    draw_text(b, label, x + 10 + o, y + (h - 6) // 2 + o, CREAM if lit else LABEL_DIM)


def make_shufrep():
    """shufrep.bmp 92x92 — repeat, shuffle e os botoes EQ e PL."""
    b = Bitmap(92, 92, BG)
    for row, (lit, pressed) in enumerate(
            ((False, False), (False, True), (True, False), (True, True))):
        y = row * 15
        toggle_button(b, 0, y, 28, 15, "REP", lit, pressed)
        toggle_button(b, 28, y, 47, 15, "SHUFFLE", lit, pressed)
    for row, lit in enumerate((False, True)):
        y = 61 + row * 12
        toggle_button(b, 0, y, 23, 12, "EQ", lit, False)
        toggle_button(b, 23, y, 23, 12, "PL", lit, False)
    return b


def make_monoster():
    """monoster.bmp 56x24 — indicadores mono e estereo, ligado e desligado."""
    b = Bitmap(58, 24, BG)
    fill_face(b, 0, 0, 58, 12, 41)
    fill_face(b, 0, 12, 58, 12, 41)
    for row, ink in ((0, CREAM), (12, LABEL_DIM)):
        # A celula tem 29 px e "stereo" mede exatamente 29 px de tinta
        # (6 glifos de 5 px, o ultimo sem o vao). Comecar em 31 corta as duas
        # ultimas colunas do "O".
        # "stereo" e o elemento mais a direita da janela: a tinta dele tem de
        # terminar na mesma coluna que o poco do titulo e o botao PL. Com o
        # avanco proporcional a palavra encolheu, e alinhada a esquerda na
        # celula ela parava tres colunas antes.
        # O +1 tira o vao que text_width inclui depois do ultimo glifo: sem
        # ele a tinta para uma coluna antes da aresta.
        draw_text(b, "mono", 29 - 3 - text_width("mono"), row + 3, ink)
        draw_text(b, "stereo", 29 + 30 - text_width("stereo"), row + 3, ink)
    return b


def make_playpaus():
    """playpaus.bmp 42x9 — indicador de estado."""
    # O indicador e desenhado DENTRO do mostrador preto, nao sobre o painel:
    # o fundo aqui e preto e o simbolo e claro, ao contrario dos botoes.
    b = Bitmap(42, 9, BG_DARK)
    draw_symbol(b, 'play', 0, 0, 9, 9, GREEN)
    draw_symbol(b, 'pause', 9, 0, 9, 9, GREEN)
    draw_symbol(b, 'stop', 18, 0, 9, 9, GREEN)
    return b


def vertical_bar(b, x, y, w, h, color):
    """Versao vertical de color_bar, tambem embutida.

    Sombra na lateral esquerda e luz na direita: a luz vem de cima e da
    esquerda, entao numa peca afundada a aresta esquerda e a que recebe sombra.
    """
    light = tuple(min(255, round(c * 1.30 + 20)) for c in color)
    shade = tuple(round(c * 0.55) for c in color)
    b.rect(x, y + 1, w, h - 2, color)
    b.hline(x + 1, y, w - 2, shade)
    b.hline(x + 1, y + h - 1, w - 2, light)
    b.vline(x, y + 1, h - 2, shade)
    b.vline(x + w - 1, y + 1, h - 2, light)


def title_stripes(b, x, y, w, h, title, ink, stripe=CREAM):
    """Barra de titulo: listras cremes com um bloco liso atras do nome."""
    b.gradient(x, y, w, h, BG_LIGHT, BG_SHADE)
    # Medido: tres listras entre as linhas 4 e 9, nao quatro entre 3 e 10.
    for j in (4, 6, 8):
        b.hline(x, y + j, w, stripe)
        b.hline(x, y + j + 1, w, BG_SHADE)
    tw = text_width(title)
    b.rect(x + (w - tw) // 2 - 6, y + 1, tw + 12, h - 2, BG)
    draw_text(b, title, x + (w - tw) // 2, y + 4, ink)
    # Raio na ponta esquerda, sobre um trecho liso — na referencia ele ocupa o
    # comeco da barra e interrompe as listras.
    b.rect(x + 1, y + 1, 16, h - 2, BG_SHADE)
    draw_bolt(b, x + 3, y + 3, GOLD if ink is CREAM else LABEL_DIM)


def make_eqmain():
    """eqmain.bmp 275x315 — fundo do equalizador, cursor e fundos de slider."""
    b = Bitmap(275, 315, BG)
    b.gradient(0, 14, 275, 102, BG, BG_SHADE)
    title_stripes(b, 0, 0, 275, 14, "PLAYAMP NG EQUALIZER", CREAM)

    # Mostrador da curva: grade clara SOBRE o painel, sem poco preto. Na
    # referencia nao ha caixa escura ali — sao so as linhas de grade e a curva.
    # Dez verticais distribuidas no mostrador, uma por banda. O mostrador tem
    # 113 px contra os 185 da fileira de sliders, entao a grade acompanha a
    # curva desenhada dentro dele, nao a posicao fisica dos controles — foi o
    # que a versao anterior tentou, e sobravam seis linhas.
    for band in range(10):
        b.vline(81 + round(band * 112 / 9.0), 17, 19, (96, 96, 128))
    b.hline(81, 26, 113, (128, 128, 158))

    # Os tres botoes de rotulo: moldura clara sobre o painel, como o EQ e o PL
    # da janela principal. Antes eram pocos pretos, porque o texto do formato
    # carregava fundo preto; agora o texto e recortado e nao precisa de campo.
    for x, w in ((14, 26), (43, 32), (217, 44)):
        b.gradient(x, 18, w, 12, BG_LIGHT, BG_SHADE)
        b.hline(x, 18, w, BEVEL_LIGHT); b.vline(x, 18, 12, BEVEL_LIGHT)
        b.hline(x, 29, w, BEVEL_DARK); b.vline(x + w - 1, 18, 12, BEVEL_DARK)

    # Escala de dB. O polegar tem 11 px e o curso vai de 38 a 100, entao o
    # centro do polegar vale 43 em +12 dB, 69 em 0 dB e 95 em -12 dB — e nessas
    # tres alturas que ficam os tracos e os rotulos. Rotulo em dourado e traco
    # em branco-azulado, como na referencia.
    for y, label in ((43, "+12db"), (69, "+0db"), (95, "-12db")):
        # Uma regra so para os tracos: um a esquerda de cada slider, mais um
        # de fechamento depois do ultimo. O preamp leva o seu em 16, as bandas
        # em 66+19b, e o de fechamento cai em 256..258 — a mesma coluna em que
        # terminam os demais elementos da janela.
        #
        # O preamp NAO leva traco a direita: ele ocupava 35..37 e o rotulo, de
        # 24 px, so cabia entre ele e o traco da primeira banda encostando nos
        # dois. Sem ele o rotulo fica em 38..61, com 4 px de folga de cada lado.
        draw_text(b, label, 40, y - 2, GOLD)
        b.hline(14, y, 3, LABEL)
        b.hline(33, y, 3, LABEL)
        for band in range(11):
            b.hline(68 + band * 19, y, 3, LABEL)

    # Rotulos das bandas, sob os sliders. Sao arte de fundo — desenhados glifo
    # a glifo, sem o fundo preto do text.bmp.
    for band, label in enumerate(("60", "170", "310", "600", "1K", "3K", "6K",
                                  "12K", "14K", "16K")):
        centre = 72 + band * 19 + 6
        draw_text(b, label, centre - (len(label) * 5 - 1) // 2, 103, LABEL)
    # Alinhado a esquerda na margem da janela, nao centrado no slider: com 31
    # px de largura, centrado no preamp ele comecava em 12 e furava a margem
    # de 16 que o resto da janela respeita.
    draw_text(b, "PREAMP", 14, 103, LABEL)

    b.hline(0, 0, 275, BEVEL_LIGHT); b.vline(0, 0, 116, BEVEL_LIGHT)
    b.hline(0, 115, 275, BEVEL_DARK); b.vline(274, 0, 116, BEVEL_DARK)

    # Cursor dos sliders: mesmo bloco claro do volume, com pega horizontal.
    for index, pressed in enumerate((False, True)):
        key_block(b, 0, 164 + index * 12, 11, 11, "horizontal", pressed)

    # Vinte e oito fundos de slider de 14x63, em duas fileiras de catorze:
    # (13,164) e (13,229). Os 28 nao cabem empilhados na vertical — 164 + 28*63
    # passa de 1900 px e o bitmap tem 315.
    for index in range(28):
        x = 13 + (index % 14) * 14
        y = 164 + (index // 14) * 65
        assert y + 63 <= 315 and x + 14 <= 275, "quadro de slider fora do eqmain"
        # Barra estreita de 5 px centrada no POLEGAR, nao na celula: o polegar
        # tem 11 px e a celula 14, entao centrar na celula deixa meio pixel de
        # desvio e a barra aparece torta atras do cursor. Polegar em x+1..x+11,
        # centro x+6; a barra vai de x+4 a x+8, mesmo centro. Medida na
        # referencia: 5 px, nao 7.
        #
        # Como no volume, a barra inteira muda de cor e o polegar diz o valor.
        vertical_bar(b, x + 4, y, 5, 63, value_color(index / 27.0))
    return b


# viscolor.txt: a cor 0 e o fundo, a 1 e a grade pontilhada, 2..17 o degrade do
# espectro do topo para a base e 18..23 o osciloscopio. Tudo sai da mesma
# paleta medida na referencia — antes a grade era esverdeada e o osciloscopio
# cinza, duas familias de cor que nao existiam em nenhum outro lugar da janela.
VISCOLOR = """\
0,0,0            // 0 fundo
28,28,44         // 1 grade
""" + "\n".join("%d,%d,%d         // %d espectro" % (c[0], c[1], c[2], i + 2)
                for i, c in enumerate(reversed(VALUE_GRADIENT))) + "\n" + "\n".join(
    "%d,%d,%d      // %d %s" % (c[0], c[1], c[2], i + 18, "osciloscopio" if i == 0 else "")
    for i, c in enumerate(_ramp(CREAM, GREEN_DIM, 6))) + "\n"


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else 'assets/skin/default'
    os.makedirs(target, exist_ok=True)

    produced = {
        'main.bmp': make_main(),
        'cbuttons.bmp': make_cbuttons(),
        'titlebar.bmp': make_titlebar(),
        'numbers.bmp': make_numbers(),
        'text.bmp': make_text(),
        'volume.bmp': make_slider_frames(68, 28),
        'balance.bmp': make_slider_frames(38, 28),
        'posbar.bmp': make_posbar(),
        'shufrep.bmp': make_shufrep(),
        'monoster.bmp': make_monoster(),
        'playpaus.bmp': make_playpaus(),
        'eqmain.bmp': make_eqmain(),
    }
    for name, bitmap in produced.items():
        bitmap.write_bmp(os.path.join(target, name))

    with open(os.path.join(target, 'viscolor.txt'), 'w') as handle:
        handle.write(VISCOLOR)

    # Tambem como .wsz, para exercitar o caminho do arquivo compactado.
    archive = target.rstrip('/') + '.wsz'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as zf:
        for name in list(produced) + ['viscolor.txt']:
            zf.write(os.path.join(target, name), name)

    print('%s: %d bitmaps' % (target, len(produced)))
    for name, bitmap in sorted(produced.items()):
        print('  %-14s %dx%d' % (name, bitmap.width, bitmap.height))
    print('%s: %d bytes' % (archive, os.path.getsize(archive)))


if __name__ == '__main__':
    main()
