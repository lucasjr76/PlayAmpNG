#!/usr/bin/env python3
"""Gera o atlas de sprites do PlayAmpNG.

Os bitmaps do skin base do Winamp sao material protegido e nao sao
redistribuidos. Este script desenha um atlas proprio seguindo a linguagem
visual descrita na secao 2 da especificacao: fundo cinza escuro, bordas
chanfradas com luz no topo-esquerda, mostradores e texto em verde, tipografia
bitmap compacta, botoes pequenos com cinco estados.

A arte fica em codigo, e nao em binario opaco: qualquer ajuste de paleta ou de
layout e um diff legivel, e o PNG e reproduzivel a partir da fonte.

Sem dependencias: o escritor de PNG usa apenas zlib da biblioteca padrao.

    python3 tools/make_skin.py assets/skin
"""

import json
import os
import struct
import sys
import zlib

# --------------------------------------------------------------------- paleta

BG          = (42, 42, 42)     # fundo geral
BG_DARK     = (24, 24, 24)     # poco de mostrador e de visualizacao
BEVEL_LIGHT = (96, 96, 96)     # aresta iluminada (topo-esquerda)
BEVEL_DARK  = (16, 16, 16)     # aresta sombreada (baixo-direita)
FACE        = (58, 58, 58)     # face de botao em repouso
FACE_HOT    = (72, 72, 72)     # botao ativo
FACE_DOWN   = (34, 34, 34)     # botao pressionado
DISABLED    = (48, 48, 48)
GREEN       = (0, 255, 127)    # mostradores e texto
GREEN_DIM   = (0, 120, 60)     # texto desabilitado
FOCUS       = (0, 190, 100)    # contorno de foco
GRAY_TEXT   = (150, 150, 150)
TRANSPARENT = (255, 0, 255)    # cor-chave, nunca desenhada


class Canvas:
    def __init__(self, width, height, fill=TRANSPARENT):
        self.width = width
        self.height = height
        self.pixels = [list(fill) * width for _ in range(height)]

    def set(self, x, y, color):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.pixels[y][x * 3:x * 3 + 3] = list(color)

    def rect(self, x, y, w, h, color):
        for j in range(y, y + h):
            for i in range(x, x + w):
                self.set(i, j, color)

    def hline(self, x, y, w, color):
        for i in range(x, x + w):
            self.set(i, y, color)

    def vline(self, x, y, h, color):
        for j in range(y, y + h):
            self.set(x, j, color)

    def bevel(self, x, y, w, h, face, raised=True):
        """Retangulo chanfrado: luz no topo-esquerda quando elevado."""
        light, dark = (BEVEL_LIGHT, BEVEL_DARK) if raised else (BEVEL_DARK, BEVEL_LIGHT)
        self.rect(x, y, w, h, face)
        self.hline(x, y, w, light)
        self.vline(x, y, h, light)
        self.hline(x, y + h - 1, w, dark)
        self.vline(x + w - 1, y, h, dark)

    def blit(self, other, x, y):
        for j in range(other.height):
            for i in range(other.width):
                pixel = tuple(other.pixels[j][i * 3:i * 3 + 3])
                if pixel != TRANSPARENT:
                    self.set(x + i, y + j, pixel)

    def write_png(self, path):
        raw = bytearray()
        for row in self.pixels:
            raw.append(0)           # filtro "none"
            raw.extend(row)

        def chunk(tag, payload):
            data = tag + payload
            return (struct.pack('>I', len(payload)) + data +
                    struct.pack('>I', zlib.crc32(data) & 0xffffffff))

        header = struct.pack('>IIBBBBB', self.width, self.height, 8, 2, 0, 0, 0)
        with open(path, 'wb') as handle:
            handle.write(b'\x89PNG\r\n\x1a\n')
            handle.write(chunk(b'IHDR', header))
            handle.write(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
            handle.write(chunk(b'IEND', b''))


# ---------------------------------------------------------------- fonte 5x7
#
# Maiusculas, digitos e a pontuacao que aparece em nome de arquivo e em
# mostrador. Minusculas sao mapeadas para maiusculas na hora de desenhar, como
# no classico. Caractere desconhecido vira o bloco de FALLBACK.

GLYPHS_5X7 = {
    'A': ".###. #...# #...# ##### #...# #...# #...#",
    'B': "####. #...# ####. #...# #...# #...# ####.",
    'C': ".#### #.... #.... #.... #.... #.... .####",
    'D': "####. #...# #...# #...# #...# #...# ####.",
    'E': "##### #.... ####. #.... #.... #.... #####",
    'F': "##### #.... ####. #.... #.... #.... #....",
    'G': ".#### #.... #.... #.### #...# #...# .###.",
    'H': "#...# #...# #...# ##### #...# #...# #...#",
    'I': "##### ..#.. ..#.. ..#.. ..#.. ..#.. #####",
    'J': "..### ....# ....# ....# #...# #...# .###.",
    'K': "#...# #..#. #.#.. ##... #.#.. #..#. #...#",
    'L': "#.... #.... #.... #.... #.... #.... #####",
    'M': "#...# ##.## #.#.# #...# #...# #...# #...#",
    'N': "#...# ##..# #.#.# #..## #...# #...# #...#",
    'O': ".###. #...# #...# #...# #...# #...# .###.",
    'P': "####. #...# #...# ####. #.... #.... #....",
    'Q': ".###. #...# #...# #...# #.#.# #..#. .##.#",
    'R': "####. #...# #...# ####. #.#.. #..#. #...#",
    'S': ".#### #.... #.... .###. ....# ....# ####.",
    'T': "##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..",
    'U': "#...# #...# #...# #...# #...# #...# .###.",
    'V': "#...# #...# #...# #...# #...# .#.#. ..#..",
    'W': "#...# #...# #...# #...# #.#.# ##.## #...#",
    'X': "#...# #...# .#.#. ..#.. .#.#. #...# #...#",
    'Y': "#...# #...# .#.#. ..#.. ..#.. ..#.. ..#..",
    'Z': "##### ....# ...#. ..#.. .#... #.... #####",
    '0': ".###. #...# #..## #.#.# ##..# #...# .###.",
    '1': "..#.. .##.. ..#.. ..#.. ..#.. ..#.. .###.",
    '2': ".###. #...# ....# ...#. ..#.. .#... #####",
    '3': "####. ....# ....# .###. ....# ....# ####.",
    '4': "...#. ..##. .#.#. #..#. ##### ...#. ...#.",
    '5': "##### #.... ####. ....# ....# #...# .###.",
    '6': "..##. .#... #.... ####. #...# #...# .###.",
    '7': "##### ....# ...#. ..#.. .#... .#... .#...",
    '8': ".###. #...# #...# .###. #...# #...# .###.",
    '9': ".###. #...# #...# .#### ....# ...#. .##..",
    ' ': "..... ..... ..... ..... ..... ..... .....",
    '.': "..... ..... ..... ..... ..... .##.. .##..",
    ',': "..... ..... ..... ..... .##.. .##.. .#...",
    ':': "..... .##.. .##.. ..... .##.. .##.. .....",
    '-': "..... ..... ..... ##### ..... ..... .....",
    '_': "..... ..... ..... ..... ..... ..... #####",
    '/': "....# ....# ...#. ..#.. .#... #.... #....",
    '(': "...#. ..#.. .#... .#... .#... ..#.. ...#.",
    ')': ".#... ..#.. ...#. ...#. ...#. ..#.. .#...",
    '[': "..### ..#.. ..#.. ..#.. ..#.. ..#.. ..###",
    ']': "###.. ..#.. ..#.. ..#.. ..#.. ..#.. ###..",
    "'": "..#.. ..#.. ..... ..... ..... ..... .....",
    '+': "..... ..#.. ..#.. ##### ..#.. ..#.. .....",
    '&': ".##.. #..#. #.#.. .#... #.#.# #..#. .##.#",
    '%': "##..# ##.#. ..#.. .#... #..## .#.## .....",
    '!': "..#.. ..#.. ..#.. ..#.. ..#.. ..... ..#..",
    '?': ".###. #...# ....# ..##. ..#.. ..... ..#..",
    '*': "..... #.#.# .###. ##### .###. #.#.# .....",
    '=': "..... ..... ##### ..... ##### ..... .....",
    '#': ".#.#. ##### .#.#. ##### .#.#. ..... .....",
    '"': ".#.#. .#.#. ..... ..... ..... ..... .....",
    '<': "...#. ..#.. .#... #.... .#... ..#.. ...#.",
    '>': ".#... ..#.. ...#. ....# ...#. ..#.. .#...",
    '@': ".###. #...# #.### #.#.# #.### #.... .###.",
}

FALLBACK = "##### #...# #...# #...# #...# #...# #####"

GLYPH_WIDTH = 5
GLYPH_HEIGHT = 7


def normalize(pattern):
    """Converte "linha linha ..." em bitmap continuo, validando a forma.

    Um glifo com numero errado de colunas desloca todas as linhas seguintes e
    sai desenhado como outra letra — foi assim que "PLAYAMPNG" virou
    "PLPYPFFNG" na primeira versao. Falhar alto e melhor que desenhar errado em
    silencio.
    """
    rows = pattern.split()
    if len(rows) != GLYPH_HEIGHT:
        raise ValueError('glifo com %d linhas, esperadas %d: %r'
                         % (len(rows), GLYPH_HEIGHT, pattern))
    for row in rows:
        if len(row) != GLYPH_WIDTH:
            raise ValueError('linha com %d colunas, esperadas %d: %r'
                             % (len(row), GLYPH_WIDTH, row))
    return ''.join(rows)


# ------------------------------------------------------------- digitos 9x13
#
# Mostrador de tempo, em sete segmentos desenhados a mao. Maiores que a fonte
# comum porque sao o elemento mais lido da janela.

SEGMENTS = {
    #        topo   sup-esq sup-dir meio   inf-esq inf-dir base
    '0': (True,  True,  True,  False, True,  True,  True),
    '1': (False, False, True,  False, False, True,  False),
    '2': (True,  False, True,  True,  True,  False, True),
    '3': (True,  False, True,  True,  False, True,  True),
    '4': (False, True,  True,  True,  False, True,  False),
    '5': (True,  True,  False, True,  False, True,  True),
    '6': (True,  True,  False, True,  True,  True,  True),
    '7': (True,  False, True,  False, False, True,  False),
    '8': (True,  True,  True,  True,  True,  True,  True),
    '9': (True,  True,  True,  True,  False, True,  True),
}

DIGIT_WIDTH = 9
DIGIT_HEIGHT = 13


def draw_digit(canvas, character, color):
    if character == ':':
        canvas.rect(3, 3, 2, 2, color)
        canvas.rect(3, 8, 2, 2, color)
        return
    if character == '-':
        canvas.rect(2, 6, 5, 2, color)
        return
    if character not in SEGMENTS:
        return

    top, upper_left, upper_right, middle, lower_left, lower_right, bottom = SEGMENTS[character]
    if top:         canvas.rect(2, 0, 5, 2, color)
    if upper_left:  canvas.rect(0, 1, 2, 6, color)
    if upper_right: canvas.rect(7, 1, 2, 6, color)
    if middle:      canvas.rect(2, 6, 5, 2, color)
    if lower_left:  canvas.rect(0, 7, 2, 6, color)
    if lower_right: canvas.rect(7, 7, 2, 6, color)
    if bottom:      canvas.rect(2, 11, 5, 2, color)


# -------------------------------------------------------------------- botoes

BUTTON_STATES = [
    ('normal',   FACE,      True,  False),
    ('pressed',  FACE_DOWN, False, False),
    ('active',   FACE_HOT,  True,  False),
    ('disabled', DISABLED,  True,  False),
    ('focus',    FACE,      True,  True),
]


def draw_glyph_shape(canvas, name, color, width, height):
    """Simbolo do transporte, desenhado em pixels e nao em fonte."""
    cx, cy = width // 2, height // 2

    def triangle(x0, direction, size):
        for i in range(size):
            span = size - i
            for j in range(-span + 1, span):
                canvas.set(x0 + direction * i, cy + j, color)

    if name == 'play':
        triangle(cx - 3, 1, 5)
    elif name == 'pause':
        canvas.rect(cx - 3, cy - 4, 2, 9, color)
        canvas.rect(cx + 1, cy - 4, 2, 9, color)
    elif name == 'stop':
        canvas.rect(cx - 4, cy - 4, 8, 9, color)
    elif name == 'previous':
        canvas.rect(cx - 5, cy - 4, 2, 9, color)
        triangle(cx + 3, -1, 5)
    elif name == 'next':
        canvas.rect(cx + 4, cy - 4, 2, 9, color)
        triangle(cx - 3, 1, 5)
    elif name == 'eject':
        for i in range(4):
            canvas.hline(cx - i, cy - 3 + i, 1 + i * 2, color)
        canvas.rect(cx - 4, cy + 3, 9, 2, color)


def build_atlas():
    sprites = {}
    # Altura generosa: sobra em atlas nao custa nada, falta custa um retrabalho.
    atlas = Canvas(512, 320, BG)
    atlas.rect(0, 0, atlas.width, atlas.height, TRANSPARENT)

    cursor_x, cursor_y, row_height = 0, 0, 0

    def place(name, canvas):
        nonlocal cursor_x, cursor_y, row_height
        if cursor_x + canvas.width > atlas.width:
            cursor_x = 0
            cursor_y += row_height + 1
            row_height = 0
        atlas.blit(canvas, cursor_x, cursor_y)
        sprites[name] = [cursor_x, cursor_y, canvas.width, canvas.height]
        cursor_x += canvas.width + 1
        row_height = max(row_height, canvas.height)

    # --- fonte
    for character, pattern in GLYPHS_5X7.items():
        cell = Canvas(GLYPH_WIDTH, GLYPH_HEIGHT)
        bits = normalize(pattern)
        for j in range(GLYPH_HEIGHT):
            for i in range(GLYPH_WIDTH):
                if bits[j * GLYPH_WIDTH + i] == '#':
                    cell.set(i, j, GREEN)
        place('font/%d' % ord(character), cell)

    fallback = Canvas(GLYPH_WIDTH, GLYPH_HEIGHT)
    bits = normalize(FALLBACK)
    for j in range(GLYPH_HEIGHT):
        for i in range(GLYPH_WIDTH):
            if bits[j * GLYPH_WIDTH + i] == '#':
                fallback.set(i, j, GREEN)
    place('font/fallback', fallback)

    # --- digitos do mostrador
    for character in list('0123456789') + [':', '-']:
        cell = Canvas(DIGIT_WIDTH, DIGIT_HEIGHT)
        draw_digit(cell, character, GREEN)
        place('digit/%d' % ord(character), cell)

    # --- botoes de transporte, cinco estados cada
    for name in ('previous', 'play', 'pause', 'stop', 'next', 'eject'):
        for state, face, raised, focus in BUTTON_STATES:
            cell = Canvas(23, 18)
            cell.bevel(0, 0, 23, 18, face, raised)
            symbol = GREEN_DIM if state == 'disabled' else GREEN
            offset = Canvas(23, 18)
            draw_glyph_shape(offset, name, symbol, 23, 18)
            if state == 'pressed':
                cell.blit(offset, 1, 1)
            else:
                cell.blit(offset, 0, 0)
            if focus:
                for i in range(0, 23, 2):
                    cell.set(i, 1, FOCUS)
                    cell.set(i, 16, FOCUS)
                for j in range(0, 18, 2):
                    cell.set(1, j, FOCUS)
                    cell.set(21, j, FOCUS)
            place('button/%s/%s' % (name, state), cell)

    # --- botoes de alternancia (shuffle, repeat, eq, pl)
    for name in ('shuffle', 'repeat', 'eq', 'playlist'):
        for state, face, raised, focus in BUTTON_STATES:
            cell = Canvas(28, 13)
            cell.bevel(0, 0, 28, 13, face, raised)
            colour = GREEN_DIM if state == 'disabled' else GREEN
            label = {'shuffle': 'SHUF', 'repeat': 'REP', 'eq': 'EQ', 'playlist': 'PL'}[name]
            x = (28 - (len(label) * (GLYPH_WIDTH + 1) - 1)) // 2
            for index, character in enumerate(label):
                bits = normalize(GLYPHS_5X7.get(character, FALLBACK))
                for j in range(GLYPH_HEIGHT):
                    for i in range(GLYPH_WIDTH):
                        if bits[j * GLYPH_WIDTH + i] == '#':
                            cell.set(x + index * (GLYPH_WIDTH + 1) + i, 3 + j, colour)
            place('toggle/%s/%s' % (name, state), cell)

    # --- botao sem rotulo, para quem desenha o proprio texto por cima
    #
    # Os alternadores tem o rotulo assado no sprite. Reaproveita-los para outros
    # botoes e escrever por cima sobrepoe os dois textos — foi o que aconteceu
    # no rodape da playlist e nos botoes do equalizador.
    for state, face, raised, focus in BUTTON_STATES:
        cell = Canvas(28, 13)
        cell.bevel(0, 0, 28, 13, face, raised)
        if focus:
            for i in range(0, 28, 2):
                cell.set(i, 1, FOCUS)
                cell.set(i, 11, FOCUS)
        place('toggle/blank/%s' % state, cell)

    # --- pecas de slider
    for state, face, raised, _ in BUTTON_STATES[:3]:
        cell = Canvas(11, 11)
        cell.bevel(0, 0, 11, 11, face, raised)
        cell.vline(5, 2, 7, BEVEL_LIGHT if raised else BEVEL_DARK)
        place('slider/thumb/%s' % state, cell)

    groove = Canvas(8, 8)
    groove.bevel(0, 3, 8, 3, BG_DARK, False)
    place('slider/groove', groove)

    # --- pecas de moldura: canto e aresta de painel, para montar em qualquer tamanho
    for name, width, height, dark in (('panel', 8, 8, False), ('well', 8, 8, True)):
        cell = Canvas(width, height)
        cell.bevel(0, 0, width, height, BG_DARK if dark else BG, not dark)
        place('frame/%s' % name, cell)

    # --- barra de titulo com faixas, no espirito do classico
    title = Canvas(8, 14)
    title.rect(0, 0, 8, 14, BG)
    title.hline(0, 0, 8, BEVEL_LIGHT)
    title.hline(0, 13, 8, BEVEL_DARK)
    for j in range(3, 11, 2):
        title.hline(0, j, 8, BEVEL_LIGHT)
        title.hline(0, j + 1, 8, BEVEL_DARK)
    place('frame/titlebar', title)

    return atlas, sprites


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else 'assets/skin'
    os.makedirs(target, exist_ok=True)

    atlas, sprites = build_atlas()
    atlas.write_png(os.path.join(target, 'atlas.png'))

    metadata = {
        'version': 1,
        'note': 'Gerado por tools/make_skin.py. Arte propria; nenhum bitmap de '
                'terceiros e redistribuido.',
        'transparent': list(TRANSPARENT),
        'glyph': {'width': GLYPH_WIDTH, 'height': GLYPH_HEIGHT},
        'digit': {'width': DIGIT_WIDTH, 'height': DIGIT_HEIGHT},
        'palette': {
            'background': list(BG), 'well': list(BG_DARK),
            'bevel_light': list(BEVEL_LIGHT), 'bevel_dark': list(BEVEL_DARK),
            'green': list(GREEN), 'green_dim': list(GREEN_DIM),
            'gray_text': list(GRAY_TEXT), 'focus': list(FOCUS),
        },
        'sprites': sprites,
    }
    with open(os.path.join(target, 'atlas.json'), 'w') as handle:
        json.dump(metadata, handle, indent=1, sort_keys=True)

    print('%s: %dx%d, %d sprites' % (target, atlas.width, atlas.height, len(sprites)))


if __name__ == '__main__':
    main()
