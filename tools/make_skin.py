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

# Paleta.
#
# O verde da primeira versao era verde-primavera (0,255,127), que puxa para o
# azul. O mostrador classico e verde puro. A diferenca some numa captura pequena
# e salta aos olhos na tela.
BG          = (60, 60, 60)     # cinza base do painel
BG_DARK     = (0, 0, 0)        # poco de mostrador: preto, como no classico
BEVEL_LIGHT = (122, 122, 122)  # aresta iluminada (topo-esquerda)
BEVEL_DARK  = (22, 22, 22)     # aresta sombreada (baixo-direita)

# Faces de botao com degrade vertical, claro em cima e escuro embaixo.
#
# Uma face chapada com uma linha de contorno le-se como "retangulo com borda",
# nao como volume. O que dava a impressao de relevo nas interfaces daquela epoca
# era justamente o degrade de duas ou tres paradas dentro do proprio botao.
FACE_TOP        = (92, 92, 92)
FACE_BOTTOM     = (58, 58, 58)
FACE_HOT_TOP    = (112, 112, 112)
FACE_HOT_BOTTOM = (74, 74, 74)
FACE_DOWN_TOP   = (44, 44, 44)
FACE_DOWN_BOTTOM= (62, 62, 62)   # invertido: pressionado escurece em cima
DISABLED_TOP    = (72, 72, 72)
DISABLED_BOTTOM = (54, 54, 54)

GREEN       = (0, 255, 12)     # mostrador de tempo e espectro
GREEN_TEXT  = (0, 224, 24)     # texto pequeno
GREEN_DIM   = (0, 104, 12)     # texto desabilitado
FOCUS       = (0, 200, 16)     # contorno de foco
GRAY_TEXT   = (176, 176, 176)
TRANSPARENT = (255, 0, 255)    # cor-chave, nunca desenhada

# Degrade vertical do espectro, da base para o topo.
#
# O classico varia a cor com a ALTURA da barra, nao com a posicao dela: verde na
# base, amarelo no meio, vermelho no pico. A primeira versao variava o matiz por
# barra, o que dava um arco-iris horizontal que o Winamp nunca teve.
#
# Os valores sao nossos; a progressao perceptiva e que segue a referencia.
SPECTRUM = [
    (16, 206, 16), (24, 200, 16), (41, 194, 16), (57, 190, 16),
    (74, 186, 16), (98, 180, 16), (116, 174, 16), (132, 166, 16),
    (140, 150, 16), (144, 132, 16), (148, 112, 16), (150, 92, 16),
    (160, 74, 16), (186, 62, 16), (214, 54, 16), (239, 49, 16),
]
PEAK        = (200, 200, 200)  # marcador de pico


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

    def gradient(self, x, y, w, h, top, bottom):
        """Preenche com degrade vertical entre duas cores."""
        for j in range(h):
            t = j / max(1, h - 1)
            color = tuple(int(top[c] + (bottom[c] - top[c]) * t) for c in range(3))
            self.hline(x, y + j, w, color)

    def bevel(self, x, y, w, h, top, bottom, raised=True):
        """Retangulo chanfrado com face em degrade.

        Duas arestas em vez de uma: a externa faz o contorno e a interna faz o
        realce, que e o que produz a sensacao de volume. Com uma aresta so o
        resultado parece um retangulo desenhado, nao um botao.
        """
        light, dark = (BEVEL_LIGHT, BEVEL_DARK) if raised else (BEVEL_DARK, BEVEL_LIGHT)
        self.gradient(x, y, w, h, top, bottom)

        self.hline(x, y, w, light)
        self.vline(x, y, h, light)
        self.hline(x, y + h - 1, w, dark)
        self.vline(x + w - 1, y, h, dark)

        inner_light = tuple(min(255, c + 26) for c in top)
        inner_dark = tuple(max(0, c - 22) for c in bottom)
        if w > 3 and h > 3:
            self.hline(x + 1, y + 1, w - 2, inner_light if raised else inner_dark)
            self.vline(x + 1, y + 1, h - 2, inner_light if raised else inner_dark)
            self.hline(x + 1, y + h - 2, w - 2, inner_dark if raised else inner_light)
            self.vline(x + w - 2, y + 1, h - 2, inner_dark if raised else inner_light)

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
DIGIT_HEIGHT = 14


def digit_cell_width(character):
    # O dois-pontos ocupa menos que um digito. Dar a ele a largura cheia de 9 px
    # empurrava "00:26" ate encostar nas bordas do poco.
    return 5 if character == ':' else DIGIT_WIDTH


def draw_digit(canvas, character, color):
    if character == ':':
        canvas.rect(2, 3, 2, 2, color)
        canvas.rect(2, 9, 2, 2, color)
        return
    if character == '-':
        canvas.rect(2, 6, 5, 2, color)
        return
    if character not in SEGMENTS:
        return

    # Segmentos SEPARADOS, com entalhe nos cantos.
    #
    # Na versao anterior as barras verticais comecavam na mesma linha da
    # horizontal e os segmentos fundiam num bloco continuo — o "display feio".
    # Num mostrador de sete segmentos de verdade cada barra e uma peca solta.
    top, upper_left, upper_right, middle, lower_left, lower_right, bottom = SEGMENTS[character]
    if top:         canvas.rect(2, 0, 5, 2, color)
    if upper_left:  canvas.rect(0, 2, 2, 4, color)
    if upper_right: canvas.rect(7, 2, 2, 4, color)
    if middle:      canvas.rect(2, 6, 5, 2, color)
    if lower_left:  canvas.rect(0, 8, 2, 4, color)
    if lower_right: canvas.rect(7, 8, 2, 4, color)
    if bottom:      canvas.rect(2, 12, 5, 2, color)


# -------------------------------------------------------------------- botoes

BUTTON_STATES = [
    ('normal',   FACE_TOP,      FACE_BOTTOM,      True,  False),
    ('pressed',  FACE_DOWN_TOP, FACE_DOWN_BOTTOM, False, False),
    ('active',   FACE_HOT_TOP,  FACE_HOT_BOTTOM,  True,  False),
    ('disabled', DISABLED_TOP,  DISABLED_BOTTOM,  True,  False),
    ('focus',    FACE_TOP,      FACE_BOTTOM,      True,  True),
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
        triangle(cx - 2, 1, 4)
    elif name == 'pause':
        canvas.rect(cx - 3, cy - 3, 2, 7, color)
        canvas.rect(cx + 1, cy - 3, 2, 7, color)
    elif name == 'stop':
        canvas.rect(cx - 3, cy - 3, 7, 7, color)
    elif name == 'previous':
        canvas.rect(cx - 4, cy - 3, 2, 7, color)
        triangle(cx + 2, -1, 4)
    elif name == 'next':
        canvas.rect(cx + 3, cy - 3, 2, 7, color)
        triangle(cx - 2, 1, 4)
    elif name == 'eject':
        for i in range(3):
            canvas.hline(cx - i, cy - 3 + i, 1 + i * 2, color)
        canvas.rect(cx - 3, cy + 2, 7, 2, color)


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

    # --- digitos do mostrador
    for character in list('0123456789') + [':', '-']:
        cell = Canvas(digit_cell_width(character), DIGIT_HEIGHT)
        draw_digit(cell, character, GREEN)
        place('digit/%d' % ord(character), cell)

    # --- botoes de transporte, cinco estados cada
    for name in ('previous', 'play', 'pause', 'stop', 'next', 'eject'):
        width, height = (22, 16) if name == 'eject' else (23, 18)
        for state, top, bottom, raised, focus in BUTTON_STATES:
            cell = Canvas(width, height)
            cell.bevel(0, 0, width, height, top, bottom, raised)
            symbol = GREEN_DIM if state == 'disabled' else GREEN
            offset = Canvas(width, height)
            draw_glyph_shape(offset, name, symbol, width, height)
            if state == 'pressed':
                cell.blit(offset, 1, 1)
            else:
                cell.blit(offset, 0, 0)
            if focus:
                for i in range(0, width, 2):
                    cell.set(i, 1, FOCUS)
                    cell.set(i, height - 2, FOCUS)
                for j in range(0, height, 2):
                    cell.set(1, j, FOCUS)
                    cell.set(width - 2, j, FOCUS)
            place('button/%s/%s' % (name, state), cell)

    # --- botoes sem rotulo, nas medidas do layout classico
    #
    # O texto nao e mais assado no sprite: os paineis desenham o rotulo por cima
    # com a fonte de verdade. Um sprite por TAMANHO, e nao por rotulo — antes
    # havia um sprite "toggle/eq" com as letras EQ dentro dele.
    PILLS = (
        ('eq', 23, 12),        # EQ e PL
        ('shuffle', 46, 15),
        ('repeat', 28, 15),
        ('wide', 28, 13),      # rodape da playlist
        ('small', 26, 12),     # ON e RST do equalizador
        ('preset', 44, 12),
    )
    for name, width, height in PILLS:
        for state, top, bottom, raised, focus in BUTTON_STATES:
            cell = Canvas(width, height)
            cell.bevel(0, 0, width, height, top, bottom, raised)
            if focus:
                for i in range(0, width, 2):
                    cell.set(i, 1, FOCUS)
                    cell.set(i, height - 2, FOCUS)
            place('pill/%s/%s' % (name, state), cell)

    # --- pecas de slider
    for state, top, bottom, raised, _ in BUTTON_STATES[:3]:
        cell = Canvas(11, 11)
        cell.bevel(0, 0, 11, 11, top, bottom, raised)
        cell.vline(5, 2, 7, BEVEL_LIGHT if raised else BEVEL_DARK)
        place('slider/thumb/%s' % state, cell)

    for state, top, bottom, raised, _ in BUTTON_STATES[:3]:
        cell = Canvas(29, 10)
        cell.bevel(0, 0, 29, 10, top, bottom, raised)
        for i in (12, 14, 16):
            cell.vline(i, 2, 6, BEVEL_LIGHT if raised else BEVEL_DARK)
        place('slider/position/%s' % state, cell)

    groove = Canvas(8, 8)
    groove.bevel(0, 3, 8, 3, BG_DARK, BG_DARK, False)
    place('slider/groove', groove)

    # --- pecas de moldura: canto e aresta de painel, para montar em qualquer tamanho
    for name, width, height, dark in (('panel', 8, 8, False), ('well', 8, 8, True)):
        cell = Canvas(width, height)
        cell.bevel(0, 0, width, height, BG_DARK if dark else FACE_TOP,
                   BG_DARK if dark else FACE_BOTTOM, not dark)
        place('frame/%s' % name, cell)

    # --- botoes da barra de titulo (minimizar, compactar, fechar)
    #
    # A janela nao tem moldura do sistema, entao sem estes botoes nao ha como
    # minimizar nem fechar pela interface — so pelo gerenciador de janelas.
    for name in ('minimize', 'shade', 'close'):
        for state, top, bottom, raised, _ in BUTTON_STATES[:2]:
            cell = Canvas(9, 9)
            cell.bevel(0, 0, 9, 9, top, bottom, raised)
            mark = GREEN
            offset = 1 if state == 'pressed' else 0
            if name == 'minimize':
                cell.rect(2 + offset, 6 + offset, 5, 1, mark)
            elif name == 'shade':
                cell.rect(2 + offset, 3 + offset, 5, 1, mark)
                cell.rect(2 + offset, 5 + offset, 5, 1, mark)
            else:
                for i in range(5):
                    cell.set(2 + i + offset, 2 + i + offset, mark)
                    cell.set(6 - i + offset, 2 + i + offset, mark)
            place('title/%s/%s' % (name, state), cell)

    # --- barra de titulo com faixas, no espirito do classico
    title = Canvas(8, 14)
    title.gradient(0, 0, 8, 14, (84, 84, 84), (52, 52, 52))
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
        'spectrum': [list(c) for c in SPECTRUM],
        'peak': list(PEAK),
        'palette': {
            'background': list(BG), 'well': list(BG_DARK),
            'bevel_light': list(BEVEL_LIGHT), 'bevel_dark': list(BEVEL_DARK),
            'green': list(GREEN), 'green_text': list(GREEN_TEXT),
            'green_dim': list(GREEN_DIM),
            'gray_text': list(GRAY_TEXT), 'focus': list(FOCUS),
        },
        'sprites': sprites,
    }
    with open(os.path.join(target, 'atlas.json'), 'w') as handle:
        json.dump(metadata, handle, indent=1, sort_keys=True)

    print('%s: %dx%d, %d sprites' % (target, atlas.width, atlas.height, len(sprites)))


if __name__ == '__main__':
    main()
