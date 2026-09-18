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
BG          = (60, 60, 60)
BG_DARK     = (0, 0, 0)
BEVEL_LIGHT = (122, 122, 122)
BEVEL_DARK  = (22, 22, 22)
FACE_TOP    = (92, 92, 92)
FACE_BOTTOM = (58, 58, 58)
FACE_DOWN_TOP    = (44, 44, 44)
FACE_DOWN_BOTTOM = (62, 62, 62)
GREEN       = (0, 255, 12)
GREEN_TEXT  = (0, 224, 24)
GREEN_DIM   = (0, 104, 12)

# Degrade usado nos fundos de volume, balanco e sliders do equalizador: a COR
# diz o valor, do verde (baixo/corte) ao vermelho (alto/reforco).
VALUE_GRADIENT = [
    (16, 206, 16), (24, 200, 16), (41, 194, 16), (57, 190, 16),
    (74, 186, 16), (98, 180, 16), (116, 174, 16), (132, 166, 16),
    (140, 150, 16), (144, 132, 16), (148, 112, 16), (150, 92, 16),
    (160, 74, 16), (186, 62, 16), (214, 54, 16), (239, 49, 16),
]


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
GLYPHS = {
 'A':".###.#...#####.#...##...#.....",'B':"####.#...####..#...#####......",
 'C':".#####....#....#.....####.....",'D':"####.#...##...##...#####......",
 'E':"######....###..#....#####.....",'F':"######....###..#....#.........",
 'G':".#####....#.###...#..####.....",'H':"#...##...#######...##...#.....",
 'I':"#####..#....#....#...#####....",'J':"..###....#....##...#.###......",
 'K':"#...##..#.##...#..#.#...#.....",'L':"#....#....#....#....#####.....",
 'M':"#...####.###.#.##...##...#....",'N':"#...###..##.#.##..###...#.....",
 'O':".###.#...##...##...#.###......",'P':"####.#...#####.#....#.........",
 'Q':".###.#...##.#.##..#..##.#.....",'R':"####.#...#####.#..#.#...#.....",
 'S':".#####.....###.....#####......",'T':"#####..#....#....#....#.......",
 'U':"#...##...##...##...#.###......",'V':"#...##...##...#.#.#...#.......",
 'W':"#...##...##.#.####.##...#.....",'X':"#...#.#.#...#...#.#.#...#.....",
 'Y':"#...#.#.#...#....#....#.......",'Z':"#####...#...#...#...#####.....",
 '"':"#.#..#.#.....................",'@':".###.#...##.####.##....###....",
 ' ':"..............................",
 '0':".###.#..###.####.###..###.....",'1':"..#..###....#....#...###......",
 '2':".###.#...#..##.#....#####.....",'3':"####.....#.###.....#####......",
 '4':"#..#.#..#.#####...#....#......",'5':"#####.....####.....#####......",
 '6':".###.#....####.#...#.###......",'7':"#####....#...#...#...#........",
 '8':".###.#...#.###.#...#.###......",'9':".###.#...#.####....#.###......",
 '.':"...............##....##.......",':':"..##...##....##...##..........",
 '(':"..#..#....#....#....#...#.....",')':"#.....#.....#.....#..#........",
 '-':".......#####..................",'\'':".#....#.......................",
 '!':"..#....#....#....#........#...",'_':"....................#####.....",
 '+':"..#....#..#####..#....#.......",'\\':"#.....#.....#.....#.....#.....",
 '/':"....#....#...#...#...#........",'[':".###..#....#....#....###......",
 ']':"###....#....#....#...###......",'^':"..#...#.#.....................",
 '&':".##..#.#..##.#.#.#..##.#......",'%':"#...#...#...#...#...#...#.....",
 ',':"...............##....#........",'=':".....#####...#####............",
 '$':"..#..####.##..#.#####..#......",'#':".#.#.#####.#.#####.#.#........",
 '?':".###.#...#...#...#........#...",'*':"#.#..###..###.#.#.............",
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


def draw_text(bmp, text, x, y, color=GREEN_TEXT):
    for index, character in enumerate(text):
        draw_glyph(bmp, character, x + index * 5, y, color)


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

def make_main():
    """main.bmp 275x116 — o fundo da janela principal."""
    b = Bitmap(275, 116)

    # Barra de titulo com listras, e um bloco liso atras do nome.
    b.gradient(0, 0, 275, 14, (84, 84, 84), (52, 52, 52))
    for j in range(3, 11, 2):
        b.hline(0, j, 275, BEVEL_LIGHT); b.hline(0, j + 1, 275, BEVEL_DARK)
    title = "PLAYAMPNG"
    tw = len(title) * 5
    b.rect((275 - tw) // 2 - 5, 2, tw + 10, 10, BG)
    draw_text(b, title, (275 - tw) // 2, 4, GREEN)

    # Bloco do mostrador: tempo e visualizacao no MESMO poco, como no formato.
    b.well(24, 24, 76, 36)          # visualizacao comeca em 24,43
    b.well(33, 23, 74, 17)          # area do tempo, dentro da mesma faixa

    # Dois-pontos do relogio: faz parte do FUNDO, nao dos digitos.
    b.rect(72, 30, 2, 2, GREEN)
    b.rect(72, 35, 2, 2, GREEN)

    # Poco do titulo da faixa e rotulos fixos.
    b.well(107, 24, 160, 13)
    draw_text(b, "kbps", 130, 43, GREEN_DIM)
    draw_text(b, "khz", 178, 43, GREEN_DIM)

    # Molduras dos controles.
    b.well(107, 57, 68, 13)
    b.well(177, 57, 38, 13)
    b.well(16, 72, 248, 10)

    # Borda externa.
    b.hline(0, 0, 275, BEVEL_LIGHT); b.vline(0, 0, 116, BEVEL_LIGHT)
    b.hline(0, 115, 275, BEVEL_DARK); b.vline(274, 0, 116, BEVEL_DARK)
    return b


def draw_symbol(b, name, x, y, w, h, color=GREEN):
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


def make_cbuttons():
    """cbuttons.bmp 136x36 — cinco botoes de 23x18 e o eject de 22x16."""
    b = Bitmap(136, 36, BG)
    order = ['previous', 'play', 'pause', 'stop', 'next']
    for index, name in enumerate(order):
        x = index * 23
        for row, (top, bottom, raised) in enumerate(
                ((FACE_TOP, FACE_BOTTOM, True), (FACE_DOWN_TOP, FACE_DOWN_BOTTOM, False))):
            b.bevel(x, row * 18, 23, 18, top, bottom, raised)
            draw_symbol(b, name, x + (1 if row else 0), row * 18 + (1 if row else 0), 23, 18)
    for row, (top, bottom, raised) in enumerate(
            ((FACE_TOP, FACE_BOTTOM, True), (FACE_DOWN_TOP, FACE_DOWN_BOTTOM, False))):
        b.bevel(114, row * 16, 22, 16, top, bottom, raised)
        draw_symbol(b, 'eject', 114 + (1 if row else 0), row * 16 + (1 if row else 0), 22, 16)
    return b


def make_titlebar():
    """titlebar.bmp 344x87 — barras e os tres botoes da janela.

    A largura e 344, e nao 275: a barra de 275 px comeca em x=27, depois dos
    botoes, entao o bitmap precisa de 27+275 no minimo. Fazer 275 recortava a
    barra pela direita — o teste de geometria pegou isso.
    """
    b = Bitmap(344, 87, BG)
    for row, active in ((0, False), (15, True)):
        base = (96, 96, 96) if active else (76, 76, 76)
        b.gradient(27, row, 275, 14, base, tuple(c - 20 for c in base))
        for j in range(row + 3, row + 11, 2):
            b.hline(27, j, 275, BEVEL_LIGHT); b.hline(27, j + 1, 275, BEVEL_DARK)

        # Nome da janela no centro da barra, sobre bloco liso.
        title = "PLAYAMPNG"
        tw = len(title) * 5
        cx = 27 + (275 - tw) // 2
        b.rect(cx - 5, row + 2, tw + 10, 10, BG)
        draw_text(b, title, cx, row + 4, GREEN if active else GREEN_DIM)

    marks = {'shade': 0, 'minimize': 9, 'close': 18}
    for name, x in marks.items():
        for row, (top, bottom, raised) in enumerate(
                ((FACE_TOP, FACE_BOTTOM, True), (FACE_DOWN_TOP, FACE_DOWN_BOTTOM, False))):
            y = row * 9
            b.bevel(x, y, 9, 9, top, bottom, raised)
            offset = 1 if row else 0
            if name == 'minimize':
                b.rect(x + 2 + offset, y + 6 + offset, 5, 1, GREEN)
            elif name == 'shade':
                b.rect(x + 2 + offset, y + 3 + offset, 5, 1, GREEN)
                b.rect(x + 2 + offset, y + 5 + offset, 5, 1, GREEN)
            else:
                for i in range(5):
                    b.set(x + 2 + i + offset, y + 2 + i + offset, GREEN)
                    b.set(x + 6 - i + offset, y + 2 + i + offset, GREEN)
    return b


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


def make_slider_frames(width, frames, thumb_width=14):
    """Tira de fundos onde o quadro escolhido representa o VALOR."""
    b = Bitmap(width, 433, BG)
    for index in range(frames):
        y = index * 15
        b.well(0, y, width, 13)
        color = value_color(index / max(1, frames - 1))
        b.rect(2, y + 2, width - 4, 9, color)
    for index, (top, bottom, raised) in enumerate(
            ((FACE_DOWN_TOP, FACE_DOWN_BOTTOM, False), (FACE_TOP, FACE_BOTTOM, True))):
        b.bevel(index * 15, 422, thumb_width, 11, top, bottom, raised)
        b.vline(index * 15 + thumb_width // 2, 424, 7, BEVEL_LIGHT if raised else BEVEL_DARK)
    return b


def make_posbar():
    """posbar.bmp 307x10 — fundo de 248 e dois cursores de 29."""
    b = Bitmap(307, 10, BG)
    b.well(0, 0, 248, 10)
    b.hline(1, 4, 246, (48, 48, 48)); b.hline(1, 5, 246, (48, 48, 48))
    for index, (top, bottom, raised) in enumerate(
            ((FACE_TOP, FACE_BOTTOM, True), (FACE_DOWN_TOP, FACE_DOWN_BOTTOM, False))):
        x = 248 + index * 30
        b.bevel(x, 0, 29, 10, top, bottom, raised)
        for i in (12, 14, 16):
            b.vline(x + i, 2, 6, BEVEL_LIGHT if raised else BEVEL_DARK)
    return b


def make_shufrep():
    """shufrep.bmp 92x92 — repeat, shuffle e os botoes EQ e PL."""
    b = Bitmap(92, 92, BG)
    for row, (label_on, top, bottom, raised) in enumerate((
            (False, FACE_TOP, FACE_BOTTOM, True),
            (False, FACE_DOWN_TOP, FACE_DOWN_BOTTOM, False),
            (True, FACE_TOP, FACE_BOTTOM, True),
            (True, FACE_DOWN_TOP, FACE_DOWN_BOTTOM, False))):
        y = row * 15
        ink = GREEN if label_on else GREEN_DIM
        b.bevel(0, y, 28, 15, top, bottom, raised)
        draw_text(b, "REP", 6, y + 5, ink)
        b.bevel(28, y, 47, 15, top, bottom, raised)
        draw_text(b, "SHUFFLE", 6 + 28, y + 5, ink)

    for row, (label_on, top, bottom, raised) in enumerate((
            (False, FACE_TOP, FACE_BOTTOM, True), (True, FACE_TOP, FACE_BOTTOM, True))):
        y = 61 + row * 12
        ink = GREEN if label_on else GREEN_DIM
        b.bevel(0, y, 23, 12, top, bottom, raised)
        draw_text(b, "EQ", 6, y + 3, ink)
        b.bevel(23, y, 23, 12, top, bottom, raised)
        draw_text(b, "PL", 29, y + 3, ink)
    return b


def make_monoster():
    """monoster.bmp 56x24 — indicadores mono e estereo, ligado e desligado."""
    b = Bitmap(58, 24, BG)
    for row, ink in ((0, GREEN), (12, GREEN_DIM)):
        draw_text(b, "mono", 2, row + 3, ink)
        draw_text(b, "stereo", 31, row + 3, ink)
    return b


def make_playpaus():
    """playpaus.bmp 42x9 — indicador de estado."""
    b = Bitmap(42, 9, BG)
    draw_symbol(b, 'play', 0, 0, 9, 9)
    draw_symbol(b, 'pause', 9, 0, 9, 9)
    draw_symbol(b, 'stop', 18, 0, 9, 9)
    return b


def make_eqmain():
    """eqmain.bmp 275x315 — fundo do equalizador, cursor e fundos de slider."""
    b = Bitmap(275, 315, BG)

    b.gradient(0, 0, 275, 14, (84, 84, 84), (52, 52, 52))
    for j in range(3, 11, 2):
        b.hline(0, j, 275, BEVEL_LIGHT); b.hline(0, j + 1, 275, BEVEL_DARK)
    title = "PLAYAMPNG EQUALIZER"
    tw = len(title) * 5
    b.rect((275 - tw) // 2 - 5, 2, tw + 10, 10, BG)
    draw_text(b, title, (275 - tw) // 2, 4, GREEN)

    b.well(87, 17, 113, 19)    # mostrador da curva
    b.hline(0, 0, 275, BEVEL_LIGHT); b.vline(0, 0, 116, BEVEL_LIGHT)
    b.hline(0, 115, 275, BEVEL_DARK); b.vline(274, 0, 116, BEVEL_DARK)

    # Cursor dos sliders.
    for index, (top, bottom, raised) in enumerate(
            ((FACE_TOP, FACE_BOTTOM, True), (FACE_DOWN_TOP, FACE_DOWN_BOTTOM, False))):
        y = 164 + index * 12
        b.bevel(0, y, 11, 11, top, bottom, raised)
        b.hline(2, y + 5, 7, BEVEL_LIGHT if raised else BEVEL_DARK)

    # Vinte e oito fundos de slider de 14x63, empilhados a partir de (13,164).
    for index in range(28):
        x, y = 13, 164 + index * 63
        if y + 63 > 315:
            break
        color = value_color(index / 27.0)
        b.rect(x + 3, y, 8, 63, color)
        b.vline(x + 2, y, 63, BEVEL_DARK)
        b.vline(x + 11, y, 63, BEVEL_LIGHT)
    return b


VISCOLOR = """\
0,0,0            // 0 fundo
24,33,41         // 1 grade
""" + "\n".join("%d,%d,%d         // %d espectro" % (c[0], c[1], c[2], i + 2)
                for i, c in enumerate(reversed(VALUE_GRADIENT))) + """
255,255,255      // 18 osciloscopio
214,214,214      // 19
172,172,172      // 20
132,132,132      // 21
90,90,90         // 22
0,0,0            // 23
"""


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
