"""Cart Chase 2P - icono (2026-10-12): carro de mina con platanos y "2P".

Dibujo en pixel art de 32x32 (estilo SNES), escalado sin suavizar.
Escribe:
  cart2p\\editor\\favicon.ico + favicon.png   (editor de etapas HTML)
  runner\\cart2p\\cart2p.ico                  (icono del ejecutable DKC1Recomp.exe,
                                             runner\\cart2p\\cart2p.rc)
Uso: python tools\\cart2p\\icono.py [--vista salida.png]
"""
import argparse
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))

# Colores
T = None                    # transparente
N = (16, 10, 6)             # contorno
F = (52, 30, 16)            # fondo madera oscura
F2 = (74, 44, 22)           # vetas
A = (120, 126, 136)         # acero del carro
A2 = (86, 90, 100)          # acero sombra
A3 = (170, 176, 186)        # acero brillo
M = (150, 92, 40)           # borde de madera
R = (196, 200, 208)         # riel
D = (96, 58, 26)            # durmiente
W = (44, 44, 48)            # rueda
C = (210, 214, 220)         # eje
Y = (255, 214, 58)          # platano
Y2 = (214, 150, 20)         # platano sombra
K = (255, 240, 120)         # texto 2P
RJ = (200, 32, 32)          # corbata DK


def dibujar():
    im = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
    px = im.load()

    def put(x, y, c):
        if 0 <= x < 32 and 0 <= y < 32 and c is not None:
            px[x, y] = c + (255,)

    # fondo: cuadrado redondeado con vetas
    for y in range(32):
        for x in range(32):
            esquina = min(x, 31 - x) + min(y, 31 - y)
            if esquina < 3:
                continue
            borde = x in (0, 31) or y in (0, 31) or esquina == 3
            put(x, y, N if borde else (F2 if (y % 6 == 2 and (x + y) % 9 > 2) else F))
    # rieles y durmientes
    for x in range(2, 30):
        put(x, 27, R)
        put(x, 29, R)
        if x % 5 == 1:
            put(x, 28, D)
            put(x, 30, D)
    # platanos asomando (antes del carro, quedan detras del borde)
    racimo = [(9, 9), (10, 8), (11, 7), (12, 7), (13, 8), (14, 9),
              (15, 8), (16, 7), (17, 6), (18, 6), (19, 7), (20, 8),
              (21, 9), (22, 8), (23, 7)]
    for x, y in racimo:
        for dy in range(0, 12 - y + 1):
            put(x, y + dy, Y if dy < 2 else Y2)
    for x, y in [(11, 6), (17, 5), (23, 6)]:
        put(x, y, (90, 60, 20))
    # carro: trapecio y = 11..23
    for y in range(11, 24):
        k = (y - 11) // 4
        x0, x1 = 4 + k, 27 - k
        for x in range(x0, x1 + 1):
            if y == 11 or y == 23 or x in (x0, x1):
                put(x, y, N)
            elif y in (12, 13):
                put(x, y, M)
            elif x == x0 + 1 or y == 22:
                put(x, y, A2)
            elif x == x1 - 1:
                put(x, y, A3)
            else:
                put(x, y, A)
    # remaches
    for x in (8, 23):
        put(x, 15, A3)
        put(x, 20, A3)
    # "2P" (fuente 3x5) en el costado
    dos = ['111', '001', '111', '100', '111']
    pe = ['111', '101', '111', '100', '100']
    for gy in range(5):
        for gx in range(3):
            if dos[gy][gx] == '1':
                put(12 + gx, 15 + gy, K)
            if pe[gy][gx] == '1':
                put(17 + gx, 15 + gy, K)
    # ruedas
    for cx in (10, 21):
        for y in range(23, 28):
            for x in range(cx - 2, cx + 3):
                d = (x - cx) ** 2 + (y - 25) ** 2
                if d <= 5:
                    put(x, y, C if d == 0 else (N if d >= 4 else W))
    # corbata roja de DK colgando del borde
    for x, y in [(25, 13), (25, 14), (26, 14), (25, 15)]:
        put(x, y, RJ)
    return im


def escalar(im, n):
    return im.resize((n, n), Image.NEAREST if n % 32 == 0 else Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--vista', help='solo escribe una vista ampliada (PNG)')
    args = ap.parse_args()
    im = dibujar()
    if args.vista:
        escalar(im, 256).save(args.vista)
        print(args.vista)
        return
    big = escalar(im, 256)
    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    destinos = [os.path.join(ROOT, 'cart2p', 'editor', 'favicon.ico'),
                os.path.join(ROOT, 'runner', 'cart2p', 'cart2p.ico')]
    for d in destinos:
        # cada tamano desde el dibujo de 32 (los multiplos sin suavizar)
        big.save(d, format='ICO', sizes=sizes,
                 append_images=[escalar(im, s[0]) for s in sizes])
        print(d)
    png = os.path.join(ROOT, 'cart2p', 'editor', 'favicon.png')
    escalar(im, 64).save(png)
    print(png)


if __name__ == '__main__':
    main()
