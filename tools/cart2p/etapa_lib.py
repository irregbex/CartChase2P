#!/usr/bin/env python3
"""Cart Chase 2P - biblioteca comun de etapas.

Una ETAPA se describe con una RECETA (archivo JSON en cart2p\\etapas_fuente\\)
que es la fuente editable; el juego carga el archivo COMPILADO .c2p
(cart2p\\stages\\). Esta biblioteca hace todo lo que comparten el generador,
el compilador, el randomizador y el editor HTML:

  Nivel(nombre)          datos del nivel original (mapas, vias, tramos,
                         bloques de hoyo, objetos, platanos, camara)
  validar(receta, nivel) lista de problemas (vacia = jugable por construccion)
  compilar(receta, nivel) bytes del .c2p (formato v3, ver FORMATO_ETAPAS.txt)
  documentar(receta, nivel) completa notas por pieza y estadisticas
  pieza_columnas(pieza)  columnas del nivel original que usa una pieza

Formato de la receta: cart2p\\docs\\FORMATO_ETAPAS.txt.
Datos del juego (direcciones ROM/WRAM): cabecera de gen_stage.py y
runner\\cart2p\\cart2p_stage.c.
"""
import json
import os
import re
import struct
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
# CART2P_DIR / CART2P_ROM redirigen los datos (tools\cart2p\preparar_desde_rom.py
# los usa para generar en otra carpeta sin tocar la vigente).
CART2P_DIR = os.environ.get('CART2P_DIR') or os.path.join(ROOT, 'cart2p')
STAGES_DIR = os.path.join(CART2P_DIR, 'stages')
FUENTE_DIR = os.path.join(CART2P_DIR, 'etapas_fuente')
LISTA = os.path.join(CART2P_DIR, 'etapas.txt')
ROM_PATH = os.environ.get('CART2P_ROM') or os.path.join(ROOT, 'rom', 'dkc1.sfc')

LAYER2_OFFSET = 0x4B00             # capa 2 = capa 1 + $4B00 (CODE_818B6B)
COL_BYTES = 32                     # una columna = 16 celdas de 32x32 px
COL_PX = 32
OBJECT_TABLE = (0xBD, 0x8000)      # DATA_BD8000: puntero por entrada
BANANA_INDEX = (0xB8, 0xB41D)      # DATA_B8B41D: indice por entrada
BANANA_TABLE = (0xB8, 0xC0E1)      # DATA_B8C0E1: desplazamientos, luego listas
CAMERA_TABLE = (0xBC, 0x8000)      # DATA_BC8000: zonas de camara por entrada
CAMERA_MAX_BYTES = 0x0E00          # espacio en $BC:9DEA (cart2p_stage.c)
BANANA_X_MAX = 0x3FF0              # el campo X del grupo tiene 11 bits (<<3)
FLAT_TYPES = {0x40, 0xC0}
RAIL_TYPES = {0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x60,
              0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5}
FIRST_CUT_MIN = 48                 # el estado inicial ya muestra hasta ~$600
EXTRA_COLS = 12                    # columnas de mapa tras el limite de camara
WRAM_LAYER2_END_BASE = 0x7F3800 + LAYER2_OFFSET
WRAM_FREE_END = 0x7FEAC0
MAX_COLS = LAYER2_OFFSET // COL_BYTES
BLOCK_MAX_COLS = 9
TEMPLATE_BANK = 0xB5
SPEED_PX_FRAME = 3.5               # velocidad media del carro (medida)

# Sprites de objeto conocidos (plantillas banco $B5, ver gen_stage.py).
SPRITES = {
    0x19: 'ZINGER', 0x4F: 'NECKY', 0x4D: 'GNAWTY', 0x51: 'CARRO',
    0x16: 'LETRA KONG', 0x15: 'RACIMO', 0x26: 'BARRIL DK', 0x38: 'CANON',
    0x2C: 'COFRE', 0x45: 'FICHA', 0x4A: 'BARRIL CONTROL', 0x34: 'GLOBO',
    0x73: 'SENAL', 0x12: 'OBJETO 12', 0x1B: 'MEDIO NEUMATICO',
    0x1F: 'NEUMATICO', 0x30: 'CUERDA', 0x6A: 'PUERTA SALIDA',
}
ENEMIES = {'ZINGER', 'NECKY', 'GNAWTY', 'CARRO'}
# zona prohibida para hoyos inventados (px antes, px despues del enemigo)
ENEMY_ZONE = {'ZINGER': (0x40, 0x200), 'NECKY': (0x40, 0x200),
              'GNAWTY': (0x80, 0x80), 'CARRO': (0x80, 0x80)}
# Enemigos de otros niveles (2026-10-12): se agregan con objetos_agregar y
# la plantilla (banco $B5) que usa ese nivel; ver ENEMIGOS_OTROS.
ENEMIES |= {'KRITTER', 'KLUMP', 'KLAPTRAP', 'SLIPPA', 'ARMY', 'KRUSHA', 'MINI NECKY'}

NIVELES = {
    'carnage': {'titulo': 'Mine Cart Carnage', 'estado': 'states\\carnage_montado.state',
                'perfil': 'vias_carnage.txt'},
    'madness': {'titulo': 'Mine Cart Madness', 'estado': 'states\\madness_montado.state',
                'perfil': 'vias_madness.txt'},
}


def rom_offset(bank, addr):
    return ((bank - 0xC0) << 16) | addr if bank >= 0xC0 else ((bank - 0x80) << 16) | addr


def u16(b, o):
    return b[o] | b[o + 1] << 8


def hx(v):
    return '$%04X' % v


# ---------------------------------------------------------------- perfil

def load_survey(path):
    """Superficies por X y datos del nivel (cabecera del perfil)."""
    surf = defaultdict(list)
    level = None
    for line in open(path):
        if line.startswith('#'):
            m = re.match(r'# entrada (\w+) D3 (\w+) D5 (\w+) 1B11 (\w+) 1B25 (\w+)', line)
            if m:
                e, d3, d5, mt, cam = (int(v, 16) for v in m.groups())
                if (d5 & 0xFF) in (0x7E, 0x7F):
                    raise SystemExit('el perfil es de una etapa ya instalada (mapa en WRAM); '
                                     'usar el perfil del nivel original')
                level = {'entrance': e, 'map': (d5 & 0xFF, d3), 'metatiles': mt,
                         'camera_max': cam, 'cols': (cam + 1) // COL_BYTES + EXTRA_COLS}
            continue
        if not line.strip():
            continue
        x, y, t = (int(v, 16) for v in line.split())
        surf[x].append((y, t))
    if not level:
        raise SystemExit('el perfil no trae la cabecera del nivel (# entrada ...)')
    return surf, level


def rail_at(surf, x):
    """(y, tipo) de la via en x (la superficie mas alta con tipo de via)."""
    cands = [(y, t) for y, t in surf.get(x & ~3, []) if t in RAIL_TYPES]
    return max(cands) if cands else None


def find_cuts(surf, cols):
    """Columnas c donde la union entre c-1 y c es via plana a una sola altura."""
    cuts = {}
    for c in range(1, cols):
        x0 = c * COL_BYTES
        ys = set()
        ok = True
        for x in range(x0 - 16, x0 + 16, 4):
            r = rail_at(surf, x)
            if not r or r[1] not in FLAT_TYPES or len(surf.get(x, [])) != 1:
                ok = False
                break
            ys.add(r[0])
        if ok and len(ys) == 1:
            cuts[c] = ys.pop()
    return cuts


def column_pieces(surf, level):
    """planas[y] = columnas con riel plano a la altura y y nada debajo;
    bloques[y] = bloques de hoyo originales con la misma altura plana a
    ambos lados (lista de dict cols/x/w)."""
    cols = level['cols']
    flat = defaultdict(list)
    for c in range(cols):
        ys = set()
        for x in range(c * COL_BYTES, c * COL_BYTES + COL_BYTES, 4):
            r = rail_at(surf, x)
            if not r or r[1] not in FLAT_TYPES or len(surf.get(x, [])) != 1:
                break
            ys.add(r[0])
        else:
            if len(ys) == 1:
                flat[ys.pop()].append(c)
    flat_of = {c: y for y, cs in flat.items() for c in cs}
    blocks = defaultdict(list)
    xmax = level['camera_max'] + 256
    x = 0x200
    while x < xmax:
        if surf.get(x):
            x += 4
            continue
        s = x
        while x < xmax and not surf.get(x):
            x += 4
        hc0, hc1 = s // COL_BYTES, (x - 4) // COL_BYTES
        a = hc0 - 1
        while a > 0 and a not in flat_of and hc0 - a <= 3:
            a -= 1
        b = hc1 + 1
        while b < cols and b not in flat_of and b - hc1 <= 3:
            b += 1
        y = flat_of.get(a)
        if y is not None and y == flat_of.get(b) and 0 < b - a - 1 <= BLOCK_MAX_COLS:
            blocks[y].append({'cols': list(range(a + 1, b)), 'x': s, 'w': x - s})
    return flat, blocks


def nearest(cands, c):
    return min(cands, key=lambda k: abs(k - c))


# ---------------------------------------------------------------- ROM

def decode_objects(rom, entrance):
    ptr = u16(rom, rom_offset(*OBJECT_TABLE) + entrance * 2)
    o = rom_offset(0xBD, ptr)
    out = []
    while True:
        e = rom[o:o + 8]
        if u16(e, 0) == 0:
            break
        out.append({'raw': bytes(e), 'type': u16(e, 0), 'x': u16(e, 2), 'y': u16(e, 4),
                    'ptr': u16(e, 6)})
        o += 8
    return out


def sprite_id(rom, ptr):
    for _ in range(8):
        o = rom_offset(TEMPLATE_BANK, ptr)
        op = u16(rom, o)
        if op == 0x8200:
            ptr = u16(rom, o + 2)
        elif op == 0x0D45:
            return u16(rom, o + 2)
        else:
            return None
    return None


def encode_object(raw, x):
    e = bytearray(raw)
    struct.pack_into('<H', e, 2, x)
    return bytes(e)


def decode_bananas(rom, entrance):
    idx = rom[rom_offset(*BANANA_INDEX) + entrance]
    base = rom_offset(*BANANA_TABLE)
    o = base + u16(rom, base + idx * 2)
    out = []
    while u16(rom, o) != 0:
        e = rom[o:o + 4]
        w2 = u16(e, 2)
        out.append({'raw': bytes(e), 'x': (w2 & 0xFFE0) >> 2,
                    'y': (u16(e, 1) & 0x1FFC) << 1})
        o += 4
    return out


def encode_banana(raw, x):
    e = bytearray(raw)
    w2 = u16(e, 2)
    w2 = (w2 & 0x1F) | ((x << 2) & 0xFFE0)
    struct.pack_into('<H', e, 2, w2)
    return bytes(e)


def decode_camera(rom, entrance):
    ptr = u16(rom, rom_offset(*CAMERA_TABLE) + entrance * 2)
    o = rom_offset(0xBC, ptr)
    out = []
    while u16(rom, o) != 0xFFFF and len(out) < 256:
        out.append(tuple(u16(rom, o + k) for k in (0, 2, 4, 6)))
        o += 8
    return out


def zone_at(zones, x):
    z = zones[0]
    for cand in zones:
        if cand[0] <= x:
            z = cand
        else:
            break
    return z


def remap_camera(zones, src_cols):
    runs = []
    for n, c in enumerate(src_cols):
        if runs and runs[-1][1] == c and runs[-1][2] + (runs[-1][1] - runs[-1][0]) == n:
            runs[-1][1] = c + 1
        else:
            runs.append([c, c + 1, n])
    out = []
    for c0, c1, n0 in runs:
        ox0, ox1, nx0 = c0 * COL_BYTES, c1 * COL_BYTES, n0 * COL_BYTES
        z = zone_at(zones, ox0)
        cand = [(nx0, z[1], z[2], z[3])]
        for zz in zones:
            if ox0 < zz[0] < ox1:
                cand.append((nx0 + zz[0] - ox0, zz[1], zz[2], zz[3]))
        for e in cand:
            if out and out[-1][1:] == e[1:]:
                continue
            if out and out[-1][0] == e[0]:
                out[-1] = e
            else:
                out.append(e)
    return out


# ---------------------------------------------------------------- nivel

class Nivel:
    """Todo lo del nivel original que necesitan recetas, editor y compilador."""

    def __init__(self, nombre, rom=None):
        if nombre not in NIVELES:
            raise ValueError('nivel desconocido: %s (carnage o madness)' % nombre)
        self.nombre = nombre
        self.info = NIVELES[nombre]
        self.rom = rom or open(ROM_PATH, 'rb').read()
        self.surf, lv = load_survey(os.path.join(STAGES_DIR, self.info['perfil']))
        self.entrance = lv['entrance']
        self.cols = lv['cols']
        self.camera_max = lv['camera_max']
        self.map_bank, self.map_addr = lv['map']
        self.metatile_addr = lv['metatiles']
        map1 = rom_offset(*lv['map'])
        self.layer1 = self.rom[map1:map1 + self.cols * COL_BYTES]
        self.layer2 = self.rom[map1 + LAYER2_OFFSET:map1 + LAYER2_OFFSET + self.cols * COL_BYTES]
        visible = (self.camera_max + 1) // COL_BYTES + 8
        max_index = 0
        for layer in (self.layer1, self.layer2):
            for i in range(0, min(len(layer), visible * COL_BYTES), 2):
                max_index = max(max_index, u16(layer, i) & 0x07FF)
        self.metatile_bytes = (max_index + 1) * 32
        self.max_cols = min(MAX_COLS, (WRAM_FREE_END - self.metatile_bytes -
                                       WRAM_LAYER2_END_BASE) // COL_BYTES)
        self.cuts = find_cuts(self.surf, self.cols)
        self.flat, self.blocks = column_pieces(self.surf, self)
        self.flat_of = {c: y for y, cs in self.flat.items() for c in cs}
        self.objects = decode_objects(self.rom, self.entrance)
        for i, o in enumerate(self.objects):
            o['indice'] = i
            sid = sprite_id(self.rom, o['ptr'])
            o['sprite'] = sid
            o['nombre'] = 'SALIDA' if o['type'] == 2 else SPRITES.get(sid, 'OBJETO %02X' % (sid or 0))
        self.bananas = decode_bananas(self.rom, self.entrance)
        for i, b in enumerate(self.bananas):
            b['indice'] = i
        self.zones = decode_camera(self.rom, self.entrance)
        self.exit_x = next((o['x'] for o in self.objects if o['type'] == 2), None)

    # usado por column_pieces (interfaz tipo dict)
    def __getitem__(self, k):
        return {'cols': self.cols, 'camera_max': self.camera_max}[k]

    def segments(self, min_segment=6):
        """Tramos entre cortes de via plana (mismos que gen_stage.py)."""
        cut_cols = [c for c in sorted(self.cuts) if c >= FIRST_CUT_MIN]
        bounds = [0]
        for c in cut_cols:
            if c - bounds[-1] >= min_segment:
                bounds.append(c)
        bounds.append(self.cols)
        return [{'a': a, 'b': b, 'cols': b - a, 'y0': self.cuts.get(a),
                 'y1': self.cuts.get(b)} for a, b in zip(bounds, bounds[1:])]

    def edge_y(self, c, side):
        """Altura de la via en el borde izquierdo (side 0) o derecho (1) de
        la columna c del nivel original (None = sin via)."""
        r = rail_at(self.surf, c * COL_PX + (0 if side == 0 else COL_PX - 4))
        return r[0] if r else None

    def objects_in(self, a, b):
        return [o for o in self.objects if a * COL_PX <= o['x'] < b * COL_PX]

    def bananas_in(self, a, b):
        return [g for g in self.bananas if a * COL_PX <= g['x'] < b * COL_PX]


_NIVELES_CACHE = {}


def nivel(nombre):
    if nombre not in _NIVELES_CACHE:
        _NIVELES_CACHE[nombre] = Nivel(nombre)
    return _NIVELES_CACHE[nombre]


# ---------------------------------------------------------------- recetas

def pieza_columnas(p):
    """Columnas del nivel original que forman la pieza, en orden."""
    t = p.get('tipo')
    if t == 'tramo':
        return list(range(int(p['desde']), int(p['hasta'])))
    if t == 'recta':
        return [int(p['columna'])] * int(p['largo'])
    if 'columnas' in p:
        return [int(c) for c in p['columnas']]
    raise ValueError('pieza sin columnas: %r' % (p,))


def layout(receta, nv):
    """Coloca las piezas: lista de dict con inicio (columna nueva), cols,
    y la lista total de columnas origen."""
    out = []
    src = []
    pos = 0
    for i, p in enumerate(receta['piezas']):
        cols = pieza_columnas(p)
        out.append({'i': i, 'pieza': p, 'inicio': pos, 'cols': cols})
        src += cols
        pos += len(cols)
    return out, src


# Temas (graficos de otros mundos detras de la via, tools/cart2p/temas.py):
# el numero (posicion + 1) va en la cabecera del .c2p y el juego instala
# cart2p\stages\tema_<nombre>.c2t (Madness: tema_<nombre>_madness.c2t).
TEMAS = ['jungla', 'cueva', 'fabrica', 'nieve', 'arboles', 'bosque', 'templo', 'mina']
TEMAS_NIVELES = ('carnage', 'madness')


def tema_id(receta):
    tema = receta.get('tema') or ''
    return TEMAS.index(tema) + 1 if tema in TEMAS else 0


# Musica de otro nivel (2026-10-12): cancion del juego (Define_DKC1_MusicID_*);
# va en el byte alto de la palabra del tema (cancion + 1) y el juego la pone
# con su propia rutina de cambio de cancion (CODE_B99036). Misma lista que
# cart2p_receta.c kMusicas.
MUSICAS = {'isla': 0x00, 'cueva': 0x01, 'niebla': 0x02, 'agua': 0x03, 'carro': 0x04,
           'nieve': 0x05, 'templo': 0x06, 'fabrica': 0x07, 'minas': 0x08, 'hielo': 0x0B,
           'bosque': 0x0D, 'bonus': 0x10, 'arboles': 0x13, 'jefe': 0x15, 'galeon': 0x18}


# Musica del nivel de donde sale cada tema (misma tabla que cart2p_receta.c
# kTemaMusica): un tema trae su musica.
TEMA_MUSICA = {'jungla': 'isla', 'cueva': 'cueva', 'fabrica': 'fabrica', 'nieve': 'nieve',
               'arboles': 'arboles', 'bosque': 'bosque', 'templo': 'templo', 'mina': 'niebla'}


def musica_id(receta):
    m = receta.get('musica') or ''
    return MUSICAS[m] + 1 if m in MUSICAS else 0


def validar(receta, nv):
    """Problemas que impiden compilar o que hacen la etapa injugable."""
    errs = []
    piezas = receta.get('piezas') or []
    if receta.get('nivel') != nv.nombre:
        errs.append('la receta es del nivel %r, no %r' % (receta.get('nivel'), nv.nombre))
    if not piezas:
        return errs + ['la receta no tiene piezas']
    try:
        lay, src = layout(receta, nv)
    except (ValueError, KeyError, TypeError) as e:
        return errs + ['pieza mal escrita: %s' % e]
    for c in src:
        if not 0 <= c < nv.cols:
            errs.append('columna %d fuera del nivel (0-%d)' % (c, nv.cols - 1))
            break
    first, last = piezas[0], piezas[-1]
    if first.get('tipo') != 'tramo' or int(first.get('desde', -1)) != 0 or \
            int(first.get('hasta', 0)) < FIRST_CUT_MIN:
        errs.append('la primera pieza debe ser el tramo de partida (desde 0, '
                    'hasta >= %d): ahi esta el estado inicial' % FIRST_CUT_MIN)
    if last.get('tipo') != 'tramo' or int(last.get('hasta', 0)) != nv.cols or \
            nv.exit_x is None or not int(last['desde']) * COL_PX <= nv.exit_x:
        errs.append('la ultima pieza debe ser el tramo final del nivel (hasta %d, '
                    'con la puerta de salida en X %s)' % (nv.cols, hx(nv.exit_x or 0)))
    tema = receta.get('tema') or ''
    if tema and tema not in TEMAS:
        errs.append('tema %r desconocido (%s)' % (tema, ', '.join(TEMAS)))
    elif tema and nv.nombre not in TEMAS_NIVELES:
        errs.append('los temas solo existen para etapas de Carnage y Madness')
    musica = receta.get('musica') or ''
    if musica and musica not in MUSICAS:
        errs.append('musica %r desconocida (%s)' % (musica, ', '.join(MUSICAS)))
    if len(src) > nv.max_cols:
        errs.append('etapa de %d columnas: el maximo es %d (memoria del juego)'
                    % (len(src), nv.max_cols))
    if len(src) < FIRST_CUT_MIN + 20:
        errs.append('etapa demasiado corta (%d columnas)' % len(src))
    for k in range(len(lay) - 1):
        a, b = lay[k], lay[k + 1]
        if not a['cols'] or not b['cols']:
            errs.append('pieza %d vacia' % (a['i'] + 1))
            continue
        ya = nv.edge_y(a['cols'][-1], 1)
        yb = nv.edge_y(b['cols'][0], 0)
        if ya is None or yb is None or abs(ya - yb) > 2:
            errs.append('union pieza %d -> %d: la via no calza (sale a %s, entra a %s)'
                        % (a['i'] + 1, b['i'] + 1,
                           hx(ya) if ya is not None else 'sin via',
                           hx(yb) if yb is not None else 'sin via'))
    res = resolver_objetos(receta, nv, lay)
    if res['sobran']:
        errs.append('demasiados objetos: %d (maximo %d del nivel original); se '
                    'pierden los %d ultimos' % (res['total'], res['limite'], res['sobran']))
    return errs


def resolver_objetos(receta, nv, lay=None):
    """Objetos de la etapa: los de cada tramo (primera copia), menos los
    quitados, mas los agregados. Devuelve lista ordenada y conteos."""
    if lay is None:
        lay, _ = layout(receta, nv)
    quitar = {(int(q['pieza']), int(q['indice'])) for q in receta.get('objetos_quitar', [])}
    vistos = set()
    objs = []
    salida = []
    for L in lay:
        p = L['pieza']
        if p.get('tipo') != 'tramo':
            continue
        a, b = int(p['desde']), int(p['hasta'])
        for o in nv.objects_in(a, b):
            if o['indice'] in vistos:
                continue        # tramo repetido: solo la primera copia
            vistos.add(o['indice'])
            if (L['i'], o['indice']) in quitar:
                continue
            nx = o['x'] - a * COL_PX + L['inicio'] * COL_PX
            (salida if o['type'] == 2 else objs).append(
                {'x': nx, 'y': o['y'], 'raw': o['raw'], 'nombre': o['nombre'],
                 'origen': 'nivel', 'indice': o['indice'], 'pieza': L['i']})
    for n, ag in enumerate(receta.get('objetos_agregar', [])):
        L = lay[int(ag['pieza'])]
        ptr = int(str(ag['puntero']).replace('$', ''), 16) if isinstance(ag['puntero'], str) \
            else int(ag['puntero'])
        raw = struct.pack('<HHHH', 1, 0, int(ag['y']), ptr)
        objs.append({'x': L['inicio'] * COL_PX + int(ag['dx']), 'y': int(ag['y']), 'raw': raw,
                     'nombre': ag.get('nombre', 'AGREGADO'), 'origen': 'agregado',
                     'agregado': n, 'pieza': L['i']})
    objs.sort(key=lambda o: o['x'])
    limite = len(nv.objects) - len(salida)
    sobran = max(0, len(objs) - limite)
    return {'objetos': objs[:limite], 'salida': salida, 'total': len(objs),
            'limite': limite, 'sobran': sobran}


def resolver_platanos(receta, nv, lay=None):
    if lay is None:
        lay, _ = layout(receta, nv)
    quitar = {(int(q['pieza']), int(q['indice'])) for q in receta.get('platanos_quitar', [])}
    vistos = set()
    out = []
    for L in lay:
        p = L['pieza']
        if p.get('tipo') != 'tramo':
            continue
        a, b = int(p['desde']), int(p['hasta'])
        for g in nv.bananas_in(a, b):
            if g['indice'] in vistos:
                continue
            vistos.add(g['indice'])
            nx = g['x'] - a * COL_PX + L['inicio'] * COL_PX
            if (L['i'], g['indice']) in quitar or nx > BANANA_X_MAX:
                continue
            out.append({'x': nx, 'y': g['y'], 'raw': g['raw'], 'indice': g['indice'],
                        'pieza': L['i']})
    out.sort(key=lambda g: g['x'])
    return out[:len(nv.bananas)]


def fondo_columna(nv, n, c):
    """Columna del original para la capa 2 (fondo) en la columna n de la etapa.
    Mine Cart Madness mueve su fondo a la mitad de velocidad (paralaje): se
    usa el fondo continuo del original (columna n), asi no salta en cada
    union. Carnage lo mueve junto con la via: se copia con su tramo (c).
    Las ultimas columnas del mapa original traen datos ajenos: se repiten
    64 columnas antes (2026-10-09)."""
    if nv.nombre != 'madness':
        return c
    seguro = min(nv.cols, (nv.camera_max + 1) // COL_BYTES + 8) - 8
    while n >= seguro:
        n -= 64
    return n


def compilar(receta, nv):
    """Bytes del .c2p v3 de la receta (no valida: llamar validar antes)."""
    lay, src = layout(receta, nv)
    new1 = bytearray()
    new2 = bytearray()
    for n, c in enumerate(src):
        new1 += nv.layer1[c * COL_BYTES:(c + 1) * COL_BYTES]
        c2 = fondo_columna(nv, n, c)
        new2 += nv.layer2[c2 * COL_BYTES:(c2 + 1) * COL_BYTES]
    total = len(src)
    res = resolver_objetos(receta, nv, lay)
    object_entries = len(nv.objects) + 1
    entries = [encode_object(o['raw'], o['x']) for o in res['objetos']]
    entries += [encode_object(o['raw'], o['x']) for o in res['salida']]
    entries += [bytes(8)] * (object_entries - len(entries))
    object_bytes = b''.join(entries)
    object_count = len(res['objetos']) + len(res['salida'])
    bananas = resolver_platanos(receta, nv, lay)
    banana_bytes = b''.join(encode_banana(g['raw'], g['x']) for g in bananas)
    banana_bytes += bytes(4 * (len(nv.bananas) - len(bananas)))
    zones = remap_camera(nv.zones, src)
    camera_bytes = b''.join(struct.pack('<HHHH', *z) for z in zones) + b'\xff\xff'
    if len(camera_bytes) > CAMERA_MAX_BYTES:
        raise ValueError('demasiadas zonas de camara (%d)' % len(zones))
    camera_max = nv.camera_max - (nv.cols - total) * COL_BYTES
    header = struct.pack('<8sHHHHHHHHHH', b'C2PSTG3\0', nv.entrance, total, camera_max,
                         object_count, len(bananas), nv.metatile_bytes,
                         len(object_bytes), len(banana_bytes), len(camera_bytes),
                         tema_id(receta) | musica_id(receta) << 8)
    return header + bytes(new1) + bytes(new2) + object_bytes + banana_bytes + camera_bytes


# ---------------------------------------------------------------- documentacion

def describir_pieza(p, L, nv):
    cols = L['cols']
    x0 = L['inicio'] * COL_PX
    y_in = nv.edge_y(cols[0], 0) if cols else None
    y_out = nv.edge_y(cols[-1], 1) if cols else None
    alt = 'via entra a %s y sale a %s' % (hx(y_in) if y_in is not None else '-',
                                         hx(y_out) if y_out is not None else '-')
    t = p.get('tipo')
    if t == 'tramo':
        a, b = int(p['desde']), int(p['hasta'])
        objs = nv.objects_in(a, b)
        cuenta = defaultdict(int)
        for o in objs:
            cuenta[o['nombre']] += 1
        lista = ', '.join('%d %s' % (n, k) for k, n in sorted(cuenta.items())) or 'sin objetos'
        nb = len(nv.bananas_in(a, b))
        return ('Tramo original de %s, columnas %d-%d (X original %s-%s), %d columnas, '
                'en la etapa desde X %s. %s; %d grupo(s) de platanos; %s.'
                % (nv.info['titulo'], a, b - 1, hx(a * COL_PX), hx(b * COL_PX - 1), b - a,
                   hx(x0), lista, nb, alt))
    if t == 'recta':
        return ('Recta inventada: columna %d del original repetida %d veces (%d px) a la '
                'altura %s, en la etapa desde X %s. Sin objetos.'
                % (int(p['columna']), int(p['largo']), int(p['largo']) * COL_PX,
                   hx(nv.flat_of.get(int(p['columna']), 0)), hx(x0)))
    huecos = []
    x = 0
    run = None
    for k, c in enumerate(cols):
        vac = all(not nv.surf.get(c * COL_PX + d) for d in range(0, COL_PX, 4))
        if vac and run is None:
            run = k
        if not vac and run is not None:
            huecos.append(k - run)
            run = None
    nombre = {'hoyo': 'Hoyo inventado', 'doble': 'Doble hoyo inventado'}.get(t, 'Pieza de columnas')
    return ('%s: %d columnas copiadas del original %s, en la etapa desde X %s; los hoyos '
            'son bordes de hoyo originales (salto real del juego); %s. Sin objetos.'
            % (nombre, len(cols), compactar(cols), hx(x0), alt))


def compactar(cols):
    """[5,5,5,7,8,9] -> '5x3, 7-9'"""
    out = []
    i = 0
    while i < len(cols):
        j = i
        while j + 1 < len(cols) and cols[j + 1] == cols[i]:
            j += 1
        if j > i:
            out.append('%dx%d' % (cols[i], j - i + 1))
            i = j + 1
            continue
        j = i
        while j + 1 < len(cols) and cols[j + 1] == cols[j] + 1:
            j += 1
        out.append('%d-%d' % (cols[i], cols[j]) if j > i else '%d' % cols[i])
        i = j + 1
    return ', '.join(out)


def corte_visible(nv, prev_last, next_first):
    """True si la union deja un corte visible: la columna que seguiria a
    prev_last en el original no es identica (capa 1) a next_first."""
    nat = prev_last + 1
    if nat == next_first:
        return False
    if nat >= nv.cols:
        return True
    return nv.layer1[nat * COL_BYTES:(nat + 1) * COL_BYTES] != \
        nv.layer1[next_first * COL_BYTES:(next_first + 1) * COL_BYTES]


def cortes_visibles(receta, nv):
    """Indices k de las uniones (pieza k -> k+1) con corte visible."""
    lay, _ = layout(receta, nv)
    return [k for k in range(len(lay) - 1)
            if lay[k]['cols'] and lay[k + 1]['cols'] and
            corte_visible(nv, lay[k]['cols'][-1], lay[k + 1]['cols'][0])]


def estadisticas(receta, nv):
    lay, src = layout(receta, nv)
    res = resolver_objetos(receta, nv, lay)
    cuenta = defaultdict(int)
    for o in res['objetos']:
        cuenta[o['nombre']] += 1
    hoyos = 0
    for L in lay:
        if L['pieza'].get('tipo') in ('hoyo', 'doble'):
            hoyos += 1 if L['pieza']['tipo'] == 'hoyo' else 2
    largo_px = len(src) * COL_PX
    return {
        'columnas': len(src),
        'largo_px': largo_px,
        'segundos_aprox': round(max(0, largo_px - 0x3A6) / SPEED_PX_FRAME / 60.0, 1),
        'piezas': len(lay),
        'tramos_originales': sum(1 for L in lay if L['pieza'].get('tipo') == 'tramo'),
        'piezas_inventadas': sum(1 for L in lay if L['pieza'].get('tipo') != 'tramo'),
        'hoyos_inventados': hoyos,
        'enemigos': sum(n for k, n in cuenta.items() if k in ENEMIES),
        'objetos': dict(sorted(cuenta.items())),
        'grupos_platanos': len(resolver_platanos(receta, nv, lay)),
        'largo_vs_original': round(len(src) / float(nv.cols), 2),
        'cortes_visibles': len(cortes_visibles(receta, nv)),
    }


def documentar(receta, nv):
    """Completa la nota de cada pieza y las estadisticas (no cambia el
    contenido jugable)."""
    lay, _ = layout(receta, nv)
    for L in lay:
        L['pieza']['nota'] = describir_pieza(L['pieza'], L, nv)
    receta['estadisticas'] = estadisticas(receta, nv)
    return receta


# ---------------------------------------------------------------- archivos

ORDEN_CLAVES = ['formato', 'id', 'nombre', 'nivel', 'tema', 'musica', 'familia', 'descripcion', 'autor',
                'creada', 'modificada', 'semilla', 'notas', 'estadisticas', 'piezas',
                'objetos_quitar', 'objetos_agregar', 'platanos_quitar', 'historial']


def receta_json(receta):
    ordenada = {k: receta[k] for k in ORDEN_CLAVES if k in receta}
    for k in receta:
        if k not in ordenada:
            ordenada[k] = receta[k]
    texto = json.dumps(ordenada, ensure_ascii=False, indent=2)
    # piezas y listas de columnas en una linea cada una: mas legible
    texto = re.sub(r'"columnas": \[\s*([\d,\s]+?)\s*\]',
                   lambda m: '"columnas": [%s]' % ', '.join(m.group(1).split()).replace(',,', ','),
                   texto)
    return texto + '\n'


def guardar_receta(receta, path=None):
    path = path or os.path.join(FUENTE_DIR, receta['id'] + '.json')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(receta_json(receta))
    return path


def cargar_receta(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def nombre_valido(nombre):
    n = re.sub(r'[^A-Z0-9 :.!\-/(),]', ' ', nombre.upper())
    return re.sub(r'\s+', ' ', n).strip()[:28]


def linea_lista(receta):
    return '%s | %s | stages\\%s.c2p' % (nombre_valido(receta['nombre']),
                                        NIVELES[receta['nivel']]['estado'], receta['id'])


def asegurar_en_lista(receta, lista=LISTA):
    """Agrega la etapa al final de etapas.txt si no esta (por archivo .c2p);
    si esta, actualiza su nombre. Devuelve True si cambio el archivo."""
    with open(lista, encoding='utf-8') as f:
        lines = f.read().splitlines()
    clave = 'stages\\%s.c2p' % receta['id']
    nueva = linea_lista(receta)
    for i, l in enumerate(lines):
        if not l.strip() or l.lstrip().startswith('#'):
            continue
        partes = [p.strip() for p in l.split('|')]
        if len(partes) == 3 and partes[2].lower() == clave.lower():
            if l != nueva:
                lines[i] = nueva
                break
            return False
    else:
        lines.append(nueva)
    with open(lista, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return True


def compilar_receta(receta, escribir=True):
    """Valida, documenta y compila. Devuelve (errores, ruta .c2p o None)."""
    nv = nivel(receta['nivel'])
    errs = validar(receta, nv)
    graves = [e for e in errs if not e.startswith('demasiados objetos')]
    if graves:
        return errs, None
    documentar(receta, nv)
    data = compilar(receta, nv)
    out = os.path.join(STAGES_DIR, receta['id'] + '.c2p')
    if escribir:
        with open(out, 'wb') as f:
            f.write(data)
    return errs, out
