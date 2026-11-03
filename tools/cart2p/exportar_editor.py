#!/usr/bin/env python3
"""Cart Chase 2P - datos del editor de etapas.

Para cada nivel de carro escribe en cart2p\\editor\\datos\\:
  <nivel>.png   el nivel original completo dibujado con sus graficos reales
                (capa 2 de fondo, oscurecida, y capa 1 encima), 32 px por
                columna x 512 px de alto
  <nivel>.json  vias por columna, tramos, bloques de hoyo, rectas posibles,
                objetos, platanos, plantillas de objetos y limites

Los graficos salen de un volcado de la VRAM y las paletas del juego en el
nivel (gfx_<nivel>.bin, lo escribe el juego con DKC1_CART2P_EXPORT; si falta,
este script corre dkc1_snesrecomp_headless.exe para obtenerlo).

Dibujo (CODE_818705): cada celda del mapa (2 bytes) es un metatile de 32x32
px = 4x4 tiles de 8x8: palabra del tile (fila r, columna t) en
metatile*32 + r*8 + t*2, banco $D5, desde $1B11. Bit $4000 de la celda =
espejo horizontal, bit $8000 = espejo vertical. Celda 0 = arriba.
Palabra de tile SNES: bits 0-9 tile, 10-12 paleta, 14 espejo H, 15 espejo V;
4 bits por pixel desde la base de caracteres de la capa.
"""
import json
import os
import struct
import subprocess
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import etapa_lib as L  # noqa: E402
import gen_stage as G  # noqa: E402
import hashlib  # noqa: E402

DATOS = os.path.join(L.CART2P_DIR, 'editor', 'datos')
EXE = os.path.join(L.ROOT, 'build-windows', 'release', 'dkc1_snesrecomp_headless.exe')


def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, body):
        c = struct.pack('>I', len(body)) + tag + body
        return c + struct.pack('>I', zlib.crc32(tag + body) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def obtener_gfx(nombre):
    path = os.path.join(DATOS, 'gfx_%s.bin' % nombre)
    if os.path.exists(path):
        return path
    env = dict(os.environ, DKC1_WIDESCREEN='0', DKC1_CART2P='2', DKC1_CART2P_TEST_NOCOUNT='1',
               DKC1_SAVESTATE_INPUT=os.path.join(L.CART2P_DIR, L.NIVELES[nombre]['estado']),
               DKC1_CART2P_EXPORT=path,
               DKC1_CART2P_RECORDS=os.path.join(DATOS, 'records_tmp.txt'),
               DKC1_CART2P_LOGFILE=os.path.join(DATOS, 'export_log.txt'))
    for k in ('DKC1_CART2P_LISTA', 'DKC1_CART2P_STAGE', 'DKC1_SCRIPT', 'DKC1_CART2P_INICIO',
              'DKC1_INPUT_RECORD'):
        env.pop(k, None)
    subprocess.run([EXE, L.ROM_PATH, '2'], env=env, capture_output=True)
    for tmp in ('records_tmp.txt', 'export_log.txt'):
        try:
            os.remove(os.path.join(DATOS, tmp))
        except OSError:
            pass
    if not os.path.exists(path):
        raise SystemExit('no se pudo obtener %s (compilar el juego primero)' % path)
    return path


class Gfx:
    def __init__(self, path):
        d = open(path, 'rb').read()
        if d[:7] != b'C2PGFX1':
            raise SystemExit('formato desconocido: %s' % path)
        h = struct.unpack_from('<8H', d, 8)
        self.entrance, self.bgmode, self.char1, self.char2 = h[0], h[1], h[2], h[3]
        self.vram = struct.unpack_from('<32768H', d, 24)
        cg = struct.unpack_from('<256H', d, 24 + 65536)
        self.pal = [((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31,
                     ((c >> 10) & 31) * 255 // 31) for c in cg]
        self.cache = {}

    def tile(self, base, word):
        """64 colores (o None = transparente) del tile, ya espejado."""
        key = (base, word)
        t = self.cache.get(key)
        if t is not None:
            return t
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
                    y = 7 - row if vf else row
                    x = 7 - col if hf else col
                    px[y * 8 + x] = self.pal[pal * 16 + ci]
        self.cache[key] = px
        return px


def dibujar(nv, gfx):
    w, h = nv.cols * 32, 512
    img = bytearray(w * h * 3)
    meta_base = L.rom_offset(nv.map_bank, nv.metatile_addr)
    rom = nv.rom
    fondo = (16, 12, 10)
    for i in range(0, len(img), 3):
        img[i:i + 3] = bytes(fondo)
    capas = [(nv.layer1, gfx.char1, False)]
    if nv.nombre == 'carnage':
        # Madness dibuja su fondo (capa 2) con otros graficos: sale basura
        capas.insert(0, (nv.layer2, gfx.char2, True))
    for layer, base, dim in capas:
        for c in range(nv.cols):
            for k in range(16):
                cell = L.u16(layer, c * 32 + k * 2)
                idx = cell & 0x7FF
                if idx * 32 + 32 > 0x10000 - (nv.metatile_addr & 0xFFFF) + 0x10000:
                    continue
                hf, vf = cell & 0x4000, cell & 0x8000
                m = meta_base + idx * 32
                for r in range(4):
                    for t in range(4):
                        sr = 3 - r if vf else r
                        st = 3 - t if hf else t
                        tw = L.u16(rom, m + sr * 8 + st * 2) ^ (0x4000 if hf else 0) ^ \
                            (0x8000 if vf else 0)
                        px = gfx.tile(base, tw)
                        x0, y0 = c * 32 + t * 8, k * 32 + r * 8
                        for yy in range(8):
                            o = ((y0 + yy) * w + x0) * 3
                            for xx in range(8):
                                col = px[yy * 8 + xx]
                                if col is None:
                                    continue
                                if dim:
                                    col = (col[0] * 2 // 5, col[1] * 2 // 5, col[2] * 2 // 5)
                                p = o + xx * 3
                                img[p] = col[0]
                                img[p + 1] = col[1]
                                img[p + 2] = col[2]
    return w, h, img


def datos(nv):
    cols = nv.cols
    edges = []
    vacia = []
    for c in range(cols):
        edges.append([nv.edge_y(c, 0), nv.edge_y(c, 1)])
        vacia.append(all(not nv.surf.get(c * 32 + d) for d in range(0, 32, 4)))
    perfil = []  # via cada 8 px (para dibujar la linea de via)
    for x in range(0, cols * 32, 8):
        r = L.rail_at(nv.surf, x)
        perfil.append(r[0] if r else None)
    plantillas = {}
    for o in nv.objects:
        if o['type'] != 1:
            continue
        k = (o['nombre'], o['ptr'])
        if k not in plantillas:
            plantillas[k] = {'nombre': o['nombre'], 'puntero': '$%04X' % o['ptr'],
                             'y': o['y'], 'enemigo': o['nombre'] in L.ENEMIES, 'usos': 0}
        plantillas[k]['usos'] += 1
    return {
        'nivel': nv.nombre, 'titulo': nv.info['titulo'], 'entrada': nv.entrance,
        'columnas': cols, 'max_columnas': nv.max_cols, 'camara_max': nv.camera_max,
        'salida_x': nv.exit_x, 'primer_corte_min': L.FIRST_CUT_MIN,
        'limite_objetos': len(nv.objects) - 1, 'limite_platanos': len(nv.bananas),
        'velocidad_px_frame': L.SPEED_PX_FRAME,
        'bordes': edges, 'vacia': vacia, 'perfil_cada_8px': perfil,
        'planas': {('%d' % y): cs for y, cs in sorted(nv.flat.items())},
        'tramos': nv.segments(),
        'bloques_hoyo': [dict(b, y=y) for y, bl in sorted(nv.blocks.items()) for b in bl],
        'objetos': [{'indice': o['indice'], 'x': o['x'], 'y': o['y'], 'tipo': o['type'],
                     'nombre': o['nombre'], 'puntero': '$%04X' % o['ptr'],
                     'enemigo': o['nombre'] in L.ENEMIES,
                     'zona_hoyos': L.ENEMY_ZONE.get(o['nombre'])} for o in nv.objects],
        'platanos': [{'indice': g['indice'], 'x': g['x'], 'y': g['y']} for g in nv.bananas],
        'plantillas': sorted(plantillas.values(), key=lambda p: (not p['enemigo'], p['nombre'])),
        'imagen': '%s.png' % nv.nombre,
        # firma de cada columna (capa 1): columnas con la misma firma son
        # identicas; unir dos piezas donde la columna que seguiria en el
        # original tiene la misma firma que la siguiente no deja corte
        'firma': [hashlib.md5(bytes(nv.layer1[c * 32:(c + 1) * 32])).hexdigest()[:10]
                  for c in range(cols)],
        # saltos sin costura: columna -> columnas gemelas (gen_stage.gemelas)
        'saltos': {str(c): ts for c, ts in G.gemelas(nv).items()},
    }


def main():
    os.makedirs(DATOS, exist_ok=True)
    for nombre in ('carnage', 'madness'):
        nv = L.nivel(nombre)
        gfx = Gfx(obtener_gfx(nombre))
        if gfx.entrance != nv.entrance:
            raise SystemExit('gfx_%s.bin es de otra entrada' % nombre)
        png = os.path.join(DATOS, nombre + '.png')
        if not os.path.exists(png) or '--forzar' in sys.argv:
            w, h, img = dibujar(nv, gfx)
            write_png(png, w, h, img)
            print('%s: %dx%d' % (png, w, h))
        with open(os.path.join(DATOS, nombre + '.json'), 'w', encoding='utf-8') as f:
            json.dump(datos(nv), f, ensure_ascii=False, separators=(',', ':'))
        print('%s.json' % nombre)
    return 0


if __name__ == '__main__':
    sys.exit(main())
