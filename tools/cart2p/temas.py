#!/usr/bin/env python3
"""Cart Chase 2P - graficos de los otros mundos de DKC1 ("temas"), leidos
directo de la ROM (sin correr el juego).

Cada nivel arranca (DATA_F9801E[entrada], banco $F9) con:
  F9:A4DC  modo de video n: registros PPU desde la tabla F9:A50E (aqui
           interesa BG12NBA $210B = base de caracteres de BG1/BG2)
  81:8C66  set de mapa n: mapa (capa 1, capa 2 = +$4B00) en $D3/$D5,
           metatiles de 32x32 en $1B11, colision en $D7 (tablas 81:8B94..)
  F9:A924  lista de graficos n (F9:A994): entradas de 7 bytes banco, origen,
           destino VRAM (bit 15 = comprimido), tamano; las comprimidas pasan
           por GFX_DecompressToWRAM (B8:96FC) a $7E:79FC y luego por DMA
  B9:99F1  paleta: X*8 bytes desde B9:A a la CGRAM desde el color Y
Este modulo reproduce eso: VRAM, CGRAM, mapa y metatiles de cada tema.

Uso:
  temas.py generar                 escribe todas las pieles en cart2p\\stages
                                   (tema_<nombre>.c2t y tema_<nombre>_madness.c2t)
  temas.py piel <tema> <salida> [base]   una piel (base carnage o madness)
  temas.py vista <salida.png> [base]     vista previa de todas
  temas.py ver <salida.png> [tema ...]   dibuja un tramo de cada nivel tema
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import etapa_lib as L  # noqa: E402

ROM = None


def rom():
    global ROM
    if ROM is None:
        ROM = open(L.ROM_PATH, 'rb').read()
    return ROM


def off(a):
    """Desplazamiento en la ROM (HiROM) de una direccion de 24 bits."""
    b = (a >> 16) & 0xFF
    lo = a & 0xFFFF
    if b >= 0xC0:
        return ((b - 0xC0) << 16) | lo
    b &= 0x7F
    return ((b - 0x40) << 16 | lo) if b >= 0x40 else (b << 16 | lo)


def rd(a, n=1):
    o = off(a)
    return int.from_bytes(rom()[o:o + n], 'little')


# Temas: nombre -> (entrada de ejemplo, titulo, modo de video, set de mapa,
# listas de graficos, (paleta B9, colores/4)). Valores del escaneo de los
# arranques (DATA_F9801E); cueva y agua cargan su paleta fuera del arranque.
TEMAS = {
    'jungla':   (0x16, 'JUNGLA', 1, 0, (1, 21), (0xA1DC, 32)),
    'cueva':    (0x07, 'CUEVA', 2, 1, (2, 21), (0xA01C, 32)),
    'mina':     (0x0A, 'MINA OSCURA', 3, 3, (5, 21), (0xA3DC, 32)),
    'fabrica':  (0x40, 'FABRICA', 12, 4, (13, 21), (0xB0A3, 32)),
    'agua':     (0xBF, 'ARRECIFE', 4, 5, (7, 21), (0x9C1C, 32)),
    'templo':   (0x42, 'TEMPLO', 13, 6, (14, 21), (0xB3A3, 32)),
    'nieve':    (0x24, 'NIEVE', 5, 7, (8, 21), (0x9B1C, 64)),
    'hielo':    (0x6D, 'HIELO', 14, 8, (15, 21), (0xC723, 64)),
    'arboles':  (0xA4, 'ALDEA EN ARBOLES', 17, 10, (18, 21), (0xB2A3, 32)),
    'bosque':   (0xD0, 'BOSQUE', 18, 11, (19, 21), (0xC623, 32)),
    'carnage':  (0x2E, 'MINE CART CARNAGE', 7, 9, (10, 21), (0x9A14, 32)),
}
# Descompresiones que el arranque hace antes de sus listas (la lista del
# templo copia desde $7E:79FC lo que deja GFX_DecompressToWRAM(E7:3BDE)).
DESCOMPRIMIR_ANTES = {'templo': (0xE73BDE,)}


def descomprimir(addr):
    """GFX_DecompressToWRAM (B8:96FC / B8:9700): A = banco, Y = origen.
    Diccionario de 64 palabras en el origen; los datos empiezan en +$80.
    Byte de control b (0 = fin), n = b & $3F:
      00 n bytes literales      40 un byte repetido n veces
      80 copiar n bytes desde la posicion (palabra) de la salida
      C0 la palabra n del diccionario."""
    out = bytearray()
    src = off(addr)
    r = rom()
    y = 0
    while True:
        b = r[src + 0x80 + y]
        if b == 0:
            break
        y += 1
        n = b & 0x3F
        kind = b & 0xC0
        if kind == 0x00:
            out += r[src + 0x80 + y:src + 0x80 + y + n]
            y += n
        elif kind == 0x40:
            out += bytes([r[src + 0x80 + y]]) * (n or 0x10000)
            y += 1
        elif kind == 0x80:
            p = r[src + 0x80 + y] | r[src + 0x80 + y + 1] << 8
            y += 2
            for i in range(n or 0x100):
                out.append(out[p + i])
        else:
            out += r[src + 2 * n:src + 2 * n + 2]
    return bytes(out)


def lista_graficos(n):
    """Entradas (banco, origen, destino VRAM, comprimido, bytes) de la lista n."""
    p = rd(0xF9A994 + 2 * n, 2)
    x = 0xF9A994 + p
    out = []
    while rd(x):
        dst = rd(x + 3, 2)
        out.append((rd(x), rd(x + 1, 2), dst & 0x7FFF, bool(dst & 0x8000), rd(x + 5, 2)))
        x += 7
    return out


def registros_video(n):
    """Registros PPU del modo de video n (tabla F9:A50E): {direccion: valor}."""
    y = rd(0xF9A50E + 2 * n, 2)
    regs = {}
    while True:
        a = rd(0xF9A50E + y, 2)
        if a == 0:
            break
        y += 2
        if a & 0x8000:
            a &= 0x7FFF
            regs[a] = rd(0xF9A50E + y)
            regs[a + 1] = rd(0xF9A50E + y + 1)
            y += 2
        else:
            regs[a] = rd(0xF9A50E + y)
            y += 1
    return regs


def set_mapa(n):
    """CODE_818C66: (banco del mapa, mapa, banco de los metatiles, metatiles,
    colision, solidos). $D5 = banco del mapa; $D6 = banco de los metatiles
    (DATA_818BC0; 0 = el mismo, solo el set 5 del agua los tiene aparte)."""
    x = 3 * n
    mapa = rd(0x818B94 + x, 2)
    banco = rd(0x818B96 + x)
    banco_meta = rd(0x818BC0 + x) or banco
    meta = rd(0x818BBE + x, 2)
    col = rd(0x818C04 + x, 2) | (rd(0x818C06 + x) << 16)
    solidos = rd(0x818C2E + 2 * n, 2)
    return banco, mapa, banco_meta, meta, col, solidos


class Tema:
    def __init__(self, nombre):
        e, titulo, video, mapa, listas, (pal, cuartos) = TEMAS[nombre]
        self.nombre, self.titulo, self.entrada = nombre, titulo, e
        regs = registros_video(video)
        nba = regs.get(0x210B, 0x22)
        self.char1 = (nba & 0x0F) << 12
        self.char2 = (nba >> 4) << 12
        (self.banco, self.mapa, self.banco_meta, self.meta, self.colision,
         self.solidos) = set_mapa(mapa)
        self.vram = [0] * 0x8000
        wram = bytearray(0x10000)  # $7E: el ultimo descomprimido en $79FC
        for addr in DESCOMPRIMIR_ANTES.get(nombre, ()):
            data = descomprimir(addr)
            wram[0x79FC:0x79FC + len(data)] = data[:0x10000 - 0x79FC]
        for n in listas:
            for banco, src, dst, comp, size in lista_graficos(n):
                if comp:
                    data = descomprimir(banco << 16 | src)
                    wram[0x79FC:0x79FC + len(data)] = data[:0x10000 - 0x79FC]
                    data = bytes(wram[0x79FC:0x79FC + size])
                elif banco == 0x7E:
                    data = bytes(wram[src:src + size])
                else:
                    o = off(banco << 16 | src)
                    data = rom()[o:o + size]
                for i in range(0, len(data) - 1, 2):
                    self.vram[(dst + i // 2) & 0x7FFF] = data[i] | data[i + 1] << 8
        o = off(0xB90000 | pal)
        cg = [rom()[o + 2 * i] | rom()[o + 2 * i + 1] << 8 for i in range(cuartos * 4)]
        cg += [0] * (256 - len(cg))
        self.cgram = cg
        self.pal = [((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31,
                     ((c >> 10) & 31) * 255 // 31) for c in cg]
        self._cache = {}

    def celda(self, capa, columna, fila):
        o = off(self.banco << 16 | self.mapa) + (0x4B00 if capa == 2 else 0)
        return L.u16(rom(), o + columna * 32 + fila * 2)

    def palabra_metatile(self, idx, r, t):
        return L.u16(rom(), off(self.banco_meta << 16 | self.meta) + idx * 32 + r * 8 + t * 2)

    def tile(self, base, word):
        key = (base, word)
        if key in self._cache:
            return self._cache[key]
        num, pal = word & 0x3FF, (word >> 10) & 7
        hf, vf = word & 0x4000, word & 0x8000
        a = (base + num * 16) & 0x7FFF
        px = [None] * 64
        for row in range(8):
            w01 = self.vram[(a + row) & 0x7FFF]
            w23 = self.vram[(a + 8 + row) & 0x7FFF]
            p0, p1, p2, p3 = w01 & 0xFF, w01 >> 8, w23 & 0xFF, w23 >> 8
            for col in range(8):
                bit = 7 - col
                ci = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | \
                    (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3)
                if ci:
                    px[(7 - row if vf else row) * 8 + (7 - col if hf else col)] = \
                        self.pal[pal * 16 + ci]
        self._cache[key] = px
        return px

    def dibujar(self, c0, ncols, capas=(2, 1)):
        w, h = ncols * 32, 512
        img = bytearray(w * h * 3)
        for capa in capas:
            base = self.char2 if capa == 2 else self.char1
            for c in range(ncols):
                for k in range(16):
                    cell = self.celda(capa, c0 + c, k)
                    idx = cell & 0x7FF
                    hf, vf = cell & 0x4000, cell & 0x8000
                    for r in range(4):
                        for t in range(4):
                            sr = 3 - r if vf else r
                            st = 3 - t if hf else t
                            tw = self.palabra_metatile(idx, sr, st) ^ \
                                (0x4000 if hf else 0) ^ (0x8000 if vf else 0)
                            px = self.tile(base, tw)
                            x0, y0 = c * 32 + t * 8, k * 32 + r * 8
                            for yy in range(8):
                                o = ((y0 + yy) * w + x0) * 3
                                for xx in range(8):
                                    col = px[yy * 8 + xx]
                                    if col:
                                        img[o + xx * 3:o + xx * 3 + 3] = bytes(col)
        return w, h, img


def tile_bytes(vram, base, num):
    a = (base + num * 16) & 0x7FFF
    return b''.join(struct.pack('<H', vram[(a + i) & 0x7FFF]) for i in range(16))


def tile_pixels(data):
    """64 indices de color (0-15) de una tesela 4bpp (32 bytes)."""
    px = []
    for row in range(8):
        p0, p1, p2, p3 = data[row * 2], data[row * 2 + 1], data[16 + row * 2], data[17 + row * 2]
        for col in range(8):
            bit = 7 - col
            px.append(((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) |
                      (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3))
    return px


def tile_from_pixels(px):
    out = bytearray(32)
    for row in range(8):
        for col in range(8):
            v = px[row * 8 + col]
            bit = 7 - col
            out[row * 2] |= (v & 1) << bit
            out[row * 2 + 1] |= ((v >> 1) & 1) << bit
            out[16 + row * 2] |= ((v >> 2) & 1) << bit
            out[17 + row * 2] |= ((v >> 3) & 1) << bit
    return bytes(out)


def rgb15(c):
    return (c & 31, (c >> 5) & 31, (c >> 10) & 31)


def mas_cercano(color, opciones):
    r, g, b = rgb15(color)
    best, bd = 1, None
    for i, c in opciones:
        r2, g2, b2 = rgb15(c)
        d = (r - r2) ** 2 * 3 + (g - g2) ** 2 * 4 + (b - b2) ** 2 * 2
        if bd is None or d < bd:
            best, bd = i, d
    return best


# Niveles base (la via): como reparte la VRAM. Carnage dibuja BG1 y BG2 con
# los mismos caracteres ($2000): via y tema comparten 896 teselas y su fondo
# de reja (BG3) se borra. Madness tiene BG2 con caracteres propios en $6000
# (hasta el mapa de BG2 en $7800: 384 teselas) y BG3 apagado. Metatiles:
# los que caben en la WRAM libre con la etapa mas larga (cart2p_stage.c).
BASES = {
    'carnage': {'via': (0x2000, 896), 'tema': None, 'borrar_bg3': True, 'metatiles': 230},
    # Madness anima la fila de paleta 1 en cada cuadro (las luces): no se usa
    'madness': {'via': (0x2000, 896), 'tema': (0x6000, 384), 'borrar_bg3': False,
                'metatiles': 250, 'filas_animadas': (1,)},
}
TEMAS['madness'] = (0x27, 'MINE CART MADNESS', 8, 2, (11, 21), (0xA2DC, 32))

# Ventanas de escenografia por base y tema (columna inicial del mapa del
# tema, columnas): se repiten detras de la via. Las de Carnage se eligieron
# a mano, periodicas (la columna que sigue a la ultima es igual a la
# primera: no se nota la repeticion) salvo fabrica y nieve, cuyas
# periodicas se ven peor. Sin entrada: elegir_ventana().
VENTANAS = {
    ('carnage', 'jungla'): (573, 20), ('carnage', 'cueva'): (428, 20),
    ('carnage', 'fabrica'): (94, 16), ('carnage', 'nieve'): (40, 16),
    ('carnage', 'arboles'): (147, 9), ('carnage', 'bosque'): (509, 12),
    ('carnage', 'templo'): (446, 20), ('carnage', 'mina'): (514, 20),
}
# Color de fondo (cielo, BGR555) por tema; sin entrada = el del nivel.
CIELO = {'jungla': 0x18C2, 'nieve': 0x72D2, 'bosque': 0x0C81, 'cueva': 0x0000,
         'templo': 0x7AF1, 'mina': 0x0000}
# Brillo de la escenografia (la via y los personajes deben destacar).
BRILLO_FONDO = 0.7
BRILLO_TEMA = {'mina': 1.0}   # ya es oscura
# Teselas que el juego anima en tiempo real (en la ROM traen un dibujo de
# relleno, "123456" en las ventanas del templo): quedan transparentes.
TESELAS_ANIMADAS = {'templo': range(0x116, 0x11C)}


def _vacia(t, num):
    return tile_bytes(t.vram, t.char1, num) == bytes(32)


def elegir_ventana(t, max_teselas, max_metatiles, max_filas=7):
    """Ventana (columna inicial, columnas) del mapa del tema t que cabe en el
    presupuesto: periodica si hay (no se nota la repeticion), si no la mas
    llena; entre 8 y 24 columnas."""
    info = {}

    def datos(i):
        if i not in info:
            s, filas, llenas = set(), set(), 0
            for r in range(4):
                for q in range(4):
                    w = t.palabra_metatile(i, r, q)
                    if not _vacia(t, w & 0x3FF):
                        s.add(w & 0x3FF)
                        filas.add((w >> 10) & 7)
                        llenas += 1
            info[i] = (s, filas, llenas)
        return info[i]
    ncols = 0x4B00 // 32
    col = [tuple(t.celda(1, c, f) for f in range(16)) for c in range(ncols)]
    mejor = None
    for k in range(24, 7, -1):
        for a in range(0, ncols - k):
            if all(x == col[a][0] for x in col[a]):
                continue
            ids = {x & 0x7FF for c in range(a, a + k) for x in col[c]}
            if len(ids) > max_metatiles:
                continue
            tiles, filas = set(), set()
            for i in ids:
                s, f, _ = datos(i)
                tiles |= s
                filas |= f
            if len(tiles) + 1 > max_teselas or len(filas) > max_filas:
                continue
            llenado = sum(datos(x & 0x7FF)[2] for c in range(a, a + k) for x in col[c]) / (k * 256.0)
            periodica = col[a] == col[a + k]
            puntaje = llenado + (0.35 if periodica else 0) + k / 200.0
            if mejor is None or puntaje > mejor[0]:
                mejor = (puntaje, a, k)
    if not mejor:
        raise SystemExit('%s: ninguna ventana cabe' % t.nombre)
    return mejor[1], mejor[2]


class Piel:
    """Piel de una etapa de carro: la via (metatiles con colision) del nivel
    base y, detras, la escenografia de un tema en la capa 2 (BG2)."""

    def __init__(self, base, tema, ventana=None):
        cfg = BASES[base]
        b = Tema(base)
        t = Tema(tema)
        self.base, self.tema, self.titulo = base, tema, t.titulo
        self.entrada = b.entrada
        self.conservar = b.solidos
        self.borrar_bg3 = cfg['borrar_bg3']
        # fila de paleta de la via: la que mas usan los metatiles con colision
        uso = {}
        for m in range(b.solidos):
            for r in range(4):
                for q in range(4):
                    w = b.palabra_metatile(m, r, q)
                    if not _vacia(b, w & 0x3FF):
                        uso[(w >> 10) & 7] = uso.get((w >> 10) & 7, 0) + 1
        self.fila_via = fila_via = max(uso, key=uso.get)
        libres = [f for f in range(8)
                  if f != fila_via and f not in cfg.get('filas_animadas', ())]
        via_base, via_max = cfg['via']
        tema_base, tema_max = cfg['tema'] or (via_base, via_max)
        listas = {}

        def alloc(lista, data):
            tiles, index = listas.setdefault(lista, ([bytes(32)], {bytes(32): 0}))
            if data not in index:
                index[data] = len(tiles)
                tiles.append(data)
            return index[data]
        lista_tema = 'tema' if cfg['tema'] else 'via'

        # via: metatiles 0..solidos-1 del nivel base, todo en fila_via
        via_opc = [(i, b.cgram[fila_via * 16 + i]) for i in range(1, 16)]
        meta = bytearray()
        for m in range(b.solidos):
            for r in range(4):
                for q in range(4):
                    w = b.palabra_metatile(m, r, q)
                    num, row = w & 0x3FF, (w >> 10) & 7
                    data = tile_bytes(b.vram, b.char1, num)
                    if data == bytes(32):
                        nw = w & 0xC000
                    else:
                        if row != fila_via:
                            px = tile_pixels(data)
                            px = [0 if p == 0 else mas_cercano(b.cgram[row * 16 + p], via_opc)
                                  for p in px]
                            data = tile_from_pixels(px)
                        nw = (w & 0xE000) | (fila_via << 10) | alloc('via', data)
                    meta += struct.pack('<H', nw)
        # tema: la ventana y sus metatiles, en las filas libres
        presupuesto = tema_max - (len(listas['via'][0]) if lista_tema == 'via' else 0)
        a, k = ventana or VENTANAS.get((base, tema)) or \
            elegir_ventana(t, presupuesto, cfg['metatiles'] - b.solidos, len(libres))
        self.k, self.ventana_origen = k, (a, k)
        ids = []
        for c in range(a, a + k):
            for f in range(16):
                i = t.celda(1, c, f) & 0x7FF
                if i not in ids:
                    ids.append(i)
        filas = sorted({(w >> 10) & 7 for w in (t.palabra_metatile(i, r, q)
                                                 for i in ids for r in range(4) for q in range(4))
                        if not _vacia(t, w & 0x3FF)})
        if len(filas) > len(libres):
            raise SystemExit('%s: la ventana usa %d filas de paleta (maximo %d)' % (
                tema, len(filas), len(libres)))
        fila_de = {f: libres[n] for n, f in enumerate(filas)}
        nuevo = {}
        for i in ids:
            nuevo[i] = b.solidos + len(nuevo)
            for r in range(4):
                for q in range(4):
                    w = t.palabra_metatile(i, r, q)
                    data = tile_bytes(t.vram, t.char1, w & 0x3FF)
                    if (w & 0x3FF) in TESELAS_ANIMADAS.get(tema, ()):
                        data = bytes(32)
                    if data == bytes(32):
                        meta += struct.pack('<H', w & 0xC000)
                    else:
                        meta += struct.pack('<H', (w & 0xE000) | (fila_de[(w >> 10) & 7] << 10) |
                                            alloc(lista_tema, data))
        self.teselas_via = listas['via'][0]
        self.teselas_tema = listas['tema'][0] if lista_tema == 'tema' else []
        self.via_base, self.tema_base = via_base, tema_base
        if len(self.teselas_via) > via_max or len(self.teselas_tema) > (tema_max if cfg['tema'] else 0):
            raise SystemExit('%s/%s: %d + %d teselas no caben' % (
                base, tema, len(self.teselas_via), len(self.teselas_tema)))
        if len(meta) // 32 > cfg['metatiles']:
            raise SystemExit('%s/%s: %d metatiles no caben' % (base, tema, len(meta) // 32))
        self.metatiles = bytes(meta)
        self.ventana = [[(t.celda(1, c, f) & 0xF800) | nuevo[t.celda(1, c, f) & 0x7FF]
                         for f in range(16)] for c in range(a, a + k)]
        brillo = BRILLO_TEMA.get(tema, BRILLO_FONDO)

        def tenue(c):
            r, g, bl = rgb15(c)
            return int(r * brillo) | int(g * brillo) << 5 | int(bl * brillo) << 10
        cg = list(b.cgram[:128])
        cg[0] = CIELO.get(tema, t.cgram[0])
        for f, n in fila_de.items():
            cg[n * 16 + 1:n * 16 + 16] = [tenue(c) for c in t.cgram[f * 16 + 1:f * 16 + 16]]
        self.cgram = cg

    @property
    def n_teselas(self):
        return len(self.teselas_via) + len(self.teselas_tema)

    def bytes(self):
        """Formato .c2t v2 (cart2p/docs/FORMATO_ETAPAS.txt, seccion 9)."""
        n_meta = len(self.metatiles) // 32
        h = struct.pack('<8s12H24s', b'C2PPIEL2', self.entrada, self.conservar, self.k, n_meta,
                        self.via_base, len(self.teselas_via), self.tema_base,
                        len(self.teselas_tema), 1 if self.borrar_bg3 else 0, 0, 0, 0,
                        self.titulo.encode('ascii')[:23])
        out = bytearray(h)
        out += self.metatiles
        for col in self.ventana:
            out += struct.pack('<16H', *col)
        for d in self.teselas_via:
            out += d
        for d in self.teselas_tema:
            out += d
        out += struct.pack('<128H', *self.cgram)
        return bytes(out)

    def vista(self, nv, c0, n, paralaje=1):
        """Imagen de las columnas c0..c0+n del nivel base con la piel (la
        capa 2 avanza a 1/paralaje de la velocidad, como en el juego)."""
        pal = [((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31)
               for c in self.cgram]
        w, h = n * 32, 512
        img = bytearray(bytes(pal[0]) * (w * h))
        cache = {}

        def tile(word, capa):
            key = (word, capa)
            if key not in cache:
                lista = self.teselas_tema if capa == 2 and self.teselas_tema else self.teselas_via
                px = tile_pixels(lista[word & 0x3FF])
                row = (word >> 10) & 7
                out = [None] * 64
                for y in range(8):
                    for x in range(8):
                        p = px[y * 8 + x]
                        if p:
                            out[(7 - y if word & 0x8000 else y) * 8 +
                                (7 - x if word & 0x4000 else x)] = pal[row * 16 + p]
                cache[key] = out
            return cache[key]

        for capa in (2, 1):
            for c in range(n):
                for k in range(16):
                    if capa == 2:
                        cell = self.ventana[((c0 + c) // paralaje) % self.k][k]
                    else:
                        cell = L.u16(nv.layer1, (c0 + c) * 32 + k * 2)
                        if (cell & 0x7FF) >= self.conservar:
                            continue
                    idx = cell & 0x7FF
                    hf, vf = cell & 0x4000, cell & 0x8000
                    for r in range(4):
                        for q in range(4):
                            sr = 3 - r if vf else r
                            sq = 3 - q if hf else q
                            word = L.u16(self.metatiles, idx * 32 + sr * 8 + sq * 2) ^ \
                                (0x4000 if hf else 0) ^ (0x8000 if vf else 0)
                            px = tile(word, capa)
                            for yy in range(8):
                                o = ((k * 32 + r * 8 + yy) * w + c * 32 + q * 8) * 3
                                for xx in range(8):
                                    col = px[yy * 8 + xx]
                                    if col:
                                        img[o + xx * 3:o + xx * 3 + 3] = bytes(col)
        return w, h, img


def nombre_piel(base, tema):
    """Archivo de la piel en cart2p\\stages (el juego arma el mismo nombre)."""
    return 'tema_%s.c2t' % tema if base == 'carnage' else 'tema_%s_%s.c2t' % (tema, base)


def png(path, w, h, rgb):
    raw = b''.join(b'\x00' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def ch(t, b):
        c = struct.pack('>I', len(b)) + t + b
        return c + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                ch(b'IDAT', zlib.compress(raw, 6)) + ch(b'IEND', b''))


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == 'generar':
        for base in L.TEMAS_NIVELES:
            for nombre in L.TEMAS:
                p = Piel(base, nombre)
                path = os.path.join(L.STAGES_DIR, nombre_piel(base, nombre))
                with open(path, 'wb') as f:
                    f.write(p.bytes())
                print('%-8s %-8s ventana %s, %3d+%3d teselas, %3d metatiles -> %s' % (
                    base, nombre, p.ventana_origen, len(p.teselas_via), len(p.teselas_tema),
                    len(p.metatiles) // 32, os.path.basename(path)))
        return 0
    if len(sys.argv) >= 3 and sys.argv[1] == 'vista':
        base = sys.argv[3] if len(sys.argv) > 3 else 'carnage'
        nv = L.nivel(base)
        c0 = 50 if base == 'carnage' else 100
        imgs = [Piel(base, n).vista(nv, c0, 24, 2 if base == 'madness' else 1) for n in L.TEMAS]
        W, H, cols = 768, 512, 2
        rows = (len(imgs) + cols - 1) // cols
        big = bytearray(W * cols * H * rows * 3)
        for i, (w, h, img) in enumerate(imgs):
            r, c = divmod(i, cols)
            for y in range(H):
                o = ((r * H + y) * W * cols + c * W) * 3
                big[o:o + W * 3] = img[y * W * 3:(y + 1) * W * 3]
        png(sys.argv[2], W * cols, H * rows, big)
        return 0
    if len(sys.argv) >= 4 and sys.argv[1] == 'piel':
        base = sys.argv[4] if len(sys.argv) > 4 else 'carnage'
        p = Piel(base, sys.argv[2])
        with open(sys.argv[3], 'wb') as f:
            f.write(p.bytes())
        print('%s/%s: %d teselas, %d metatiles -> %s' % (base, sys.argv[2], p.n_teselas,
                                                        len(p.metatiles) // 32, sys.argv[3]))
        return 0
    if len(sys.argv) < 3 or sys.argv[1] != 'ver':
        print(__doc__)
        return 1
    nombres = sys.argv[3:] or list(TEMAS)
    c0, n = 6, 10
    tiles = []
    for nombre in nombres:
        t = Tema(nombre)
        print('%-9s chars %04X/%04X mapa %02X:%04X metatiles %04X colision %06X solidos %d' % (
            nombre, t.char1, t.char2, t.banco, t.mapa, t.meta, t.colision, t.solidos))
        tiles.append(t.dibujar(c0, n))
    W, H, cols = n * 32, 512, 4
    rows = (len(tiles) + cols - 1) // cols
    big = bytearray(W * cols * H * rows * 3)
    for i, (w, h, img) in enumerate(tiles):
        r, c = divmod(i, cols)
        for y in range(H):
            o = ((r * H + y) * W * cols + c * W) * 3
            big[o:o + W * 3] = img[y * W * 3:(y + 1) * W * 3]
    png(sys.argv[2], W * cols, H * rows, big)
    return 0


if __name__ == '__main__':
    sys.exit(main())
