#!/usr/bin/env python3
"""Cart Chase 2P - generador automatico de etapas de carro.

Corta el nivel original (Mine Cart Carnage o Mine Cart Madness) en tramos por
las columnas donde la via es plana y los vuelve a unir en otro orden,
exigiendo que la altura de la via coincida en cada union. Con --inventar N
inserta ademas N tramos inventados en uniones planas: rectas nuevas y hoyos
hechos con bordes de hoyo originales (nunca junto a enemigos).

Desde el 2026-10-08 el resultado es una RECETA (JSON editable, formato en
cart2p\\docs\\FORMATO_ETAPAS.txt) que se compila al .c2p con etapa_lib.py.
La version anterior (que escribia el .c2p directo) esta en
cart2p\\archivo\\2026-10-08_gen_stage_v3\\; para las mismas opciones produce
el mismo .c2p byte a byte.

Uso:
  gen_stage.py --nivel carnage --seed 4 --inventar 12 --id inventada_carnage1
               [--nombre "INVENTADA 1"] [--cols N] [--solo-receta]
  (compatibilidad: --survey cart2p\\stages\\vias_<nivel>.txt --out <ruta>.c2p)
"""
import argparse
import os
import random
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import etapa_lib as L  # noqa: E402

COL_BYTES = L.COL_BYTES


def build_order(segments, last, target_cols, rng, tries=20000):
    """Lista de tramos (indices) cuya altura encaja en cada union y que termina
    en `last`. Prefiere no repetir tramos y acercarse a target_cols."""
    first = 0
    by_start = defaultdict(list)
    for i, s in enumerate(segments):
        if i not in (first, last):
            by_start[s['y0']].append(i)
    best = None
    for _ in range(tries):
        order = [first]
        used = {first}
        y = segments[first]['y1']
        total = segments[first]['cols']
        while True:
            remaining = target_cols - total - segments[last]['cols']
            if y == segments[last]['y0'] and remaining <= 12:
                order.append(last)
                total += segments[last]['cols']
                break
            cands = [i for i in by_start[y] if i not in used]
            if not cands:
                cands = list(by_start[y])
            cands = [i for i in cands if segments[i]['cols'] <= remaining + 12]
            if not cands:
                break
            i = rng.choice(cands)
            order.append(i)
            used.add(i)
            y = segments[i]['y1']
            total += segments[i]['cols']
        if order[-1] != last:
            continue
        score = abs(total - target_cols) + 40 * (len(order) - len(set(order)))
        if best is None or score < best[0]:
            best = (score, order, total)
            if score == 0:
                break
    return best


def invent_module(rng, y, near, flat, blocks, pits_ok=True):
    """Pieza inventada a la altura y (dict de receta) o None. near = columna
    original junto a la union (para copiar fondo parecido)."""
    if not flat.get(y):
        return None
    f = L.nearest(flat[y], near)
    kinds = ['recta']
    if pits_ok and blocks.get(y):
        kinds = ['recta', 'hoyo', 'hoyo', 'doble']
    kind = rng.choice(kinds)
    if kind == 'recta':
        return {'tipo': 'recta', 'columna': f, 'largo': rng.randint(3, 8)}
    cols = [f] * rng.randint(2, 4)
    widths = []
    for p in range(1 if kind == 'hoyo' else 2):
        if p:
            cols += [f] * (4 + rng.randint(0, 2))
        blk = rng.choice(blocks[y])
        cols += blk['cols']
        widths.append(blk['w'])
    cols += [f] * rng.randint(2, 4)
    return {'tipo': kind, 'hoyos_px': widths, 'columnas': cols}


def generar(nv, seed, cols=0, inventar=0, min_segment=6, verbose=False, hoyos=True):
    """Receta generada (sin id ni nombre). Misma secuencia aleatoria que la
    version v3 del generador. hoyos=False: los inventados son solo rectas."""
    rng = random.Random(seed)
    segments = nv.segments(min_segment)
    last = len(segments) - 1
    if verbose:
        print('cortes validos:', len(nv.cuts), 'tramos:', len(segments))
        for i, s in enumerate(segments):
            print('  %2d: cols %3d-%3d (%3d) y0 %s y1 %s' % (
                i, s['a'], s['b'], s['cols'],
                '%04X' % s['y0'] if s['y0'] is not None else '----',
                '%04X' % s['y1'] if s['y1'] is not None else '----'))
    target = cols or nv.cols
    max_cols = nv.max_cols
    flat, blocks = nv.flat, nv.blocks
    budget = 10 * inventar
    base_target = max(40, min(target, max_cols) - budget)
    best = None
    while not best and base_target <= max_cols:
        best = build_order(segments, last, base_target, rng)
        base_target += 10
    if not best:
        return None
    _, order, base_total = best
    if verbose:
        print('orden de tramos:', order, 'columnas:', base_total)

    inserts = {}
    if inventar:
        joint_x = []
        base_place = []
        pos = 0
        for i in order:
            s = segments[i]
            base_place.append((s['a'], s['b'], pos))
            pos += s['cols']
            joint_x.append(pos * COL_BYTES)
        enemies = []
        for o in nv.objects:
            zone = L.ENEMY_ZONE.get(o['nombre'])
            if not zone or o['type'] != 1:
                continue
            for a, b, new_a in base_place:
                if a * COL_BYTES <= o['x'] < b * COL_BYTES:
                    enemies.append((o['x'] - a * COL_BYTES + new_a * COL_BYTES,
                                    zone[0], zone[1]))
                    break

        def pits_ok(k):
            jx = joint_x[k]
            return (hoyos and blocks.get(segments[order[k]]['y1']) and
                    not any(ex - before <= jx <= ex + after
                            for ex, before, after in enemies))
        joints = [k for k in range(len(order) - 1)
                  if flat.get(segments[order[k]]['y1'])]
        rng.shuffle(joints)
        joints.sort(key=lambda k: not pits_ok(k))
        if verbose:
            print('uniones con via plana: %d, aptas para hoyos: %d' % (
                len(joints), sum(1 for k in joints if pits_ok(k))))
        room = max_cols - base_total
        for k in joints:
            if len(inserts) >= inventar:
                break
            s = segments[order[k]]
            mod = invent_module(rng, s['y1'], s['b'] - 1, flat, blocks, pits_ok(k))
            if not mod or len(L.pieza_columnas(mod)) > room:
                continue
            inserts[k] = mod
            room -= len(L.pieza_columnas(mod))
        if len(inserts) < inventar and verbose:
            print('aviso: solo caben %d de %d tramos inventados' % (len(inserts), inventar))

    piezas = []
    for k, i in enumerate(order):
        s = segments[i]
        piezas.append({'tipo': 'tramo', 'desde': s['a'], 'hasta': s['b']})
        if k in inserts:
            piezas.append(inserts[k])
    return {'formato': 1, 'nivel': nv.nombre, 'piezas': piezas,
            'objetos_quitar': [], 'objetos_agregar': [], 'platanos_quitar': []}


def gemelas(nv):
    """Saltos sin costura: para cada columna c (con via, fuera de la partida y
    de la meta) las otras columnas t con la capa 1 identica a c, a mas de 8
    columnas. Terminar un tramo justo antes de c y seguir desde t no deja
    corte visible: la columna que se ve es la misma que en el original."""
    last = (nv.camera_max + 1) // COL_BYTES
    grupos = defaultdict(list)
    for c in range(L.FIRST_CUT_MIN + 8, last - 8):
        y0, y1 = nv.edge_y(c - 1, 1), nv.edge_y(c, 0)
        if y0 is None or y1 is None or abs(y0 - y1) > 2:
            continue  # via en pendiente en el corte: la validacion no la acepta
        grupos[bytes(nv.layer1[c * COL_BYTES:(c + 1) * COL_BYTES])].append(c)
    out = defaultdict(list)
    for cs in grupos.values():
        for c in cs:
            for t in cs:
                if abs(t - c) > 8:
                    out[c].append(t)
    return out


def generar_sin_costura(nv, seed, cols=0, saltos_min=6, min_run=12, max_run=110,
                        tries=6000):
    """Etapa sin costuras: un recorrido por el nivel original que salta entre
    columnas gemelas (hacia adelante acorta, hacia atras alarga), con al
    menos `saltos_min` saltos. Sin piezas inventadas. Receta o None."""
    rng = random.Random(seed)
    saltos = gemelas(nv)
    puntos = sorted(saltos)
    target = min(cols or nv.cols, nv.max_cols)
    tol = 16
    best = None
    for _ in range(tries):
        runs = []
        pos, total = 0, 0
        usadas = set()
        ok = False
        for _paso in range(40):
            resto = nv.cols - pos
            diff = total + resto - target
            if len(runs) >= saltos_min and abs(diff) <= tol:
                runs.append((pos, nv.cols))
                total += resto
                ok = True
                break
            lo = pos + (L.FIRST_CUT_MIN + min_run if pos == 0 else min_run)
            cands = [c for c in puntos if lo <= c <= pos + max_run]
            # mucho sobrante: solo adelante; mucho faltante: solo atras
            # (si no hay ninguno en esa direccion a mano, cualquiera)
            sentido = 1 if diff > 60 else -1 if diff < -60 else 0
            if sentido:
                f = [c for c in cands if any((t - c) * sentido > 0 for t in saltos[c])]
                if f:
                    cands = f
                else:
                    sentido = 0
            if not cands:
                break
            c = rng.choice(cands)
            ts = [t for t in saltos[c] if (t - c) * sentido > 0] if sentido else saltos[c]
            t = rng.choice(ts)
            runs.append((pos, c))
            total += c - pos
            usadas.update(range(pos, c))
            pos = t
        if not ok or total > nv.max_cols:
            continue
        repetidas = sum(1 for a, b in runs[1:] for x in range(a, b) if x in usadas)
        score = abs(total - target) + repetidas // 3
        if best is None or score < best[0]:
            best = (score, runs, total)
            if score == 0:
                break
    if not best:
        return None
    piezas = [{'tipo': 'tramo', 'desde': a, 'hasta': b} for a, b in best[1]]
    return {'formato': 1, 'nivel': nv.nombre, 'piezas': piezas,
            'objetos_quitar': [], 'objetos_agregar': [], 'platanos_quitar': []}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nivel', choices=sorted(L.NIVELES))
    ap.add_argument('--survey', help='compatibilidad: deduce el nivel del nombre')
    ap.add_argument('--rom', help='compatibilidad (se usa rom\\dkc1.sfc)')
    ap.add_argument('--out', help='compatibilidad: ruta del .c2p (id = nombre del archivo)')
    ap.add_argument('--id', help='nombre de archivo de la etapa (sin extension)')
    ap.add_argument('--nombre', help='nombre en el menu (por defecto el id)')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--cols', type=int, default=0,
                    help='largo objetivo en columnas de 32 px (por defecto el original)')
    ap.add_argument('--min-segment', type=int, default=6)
    ap.add_argument('--inventar', type=int, default=0)
    ap.add_argument('--descripcion', default='')
    ap.add_argument('--solo-receta', action='store_true',
                    help='escribe la receta sin compilar')
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args()

    nivel = args.nivel
    if not nivel and args.survey:
        nivel = 'madness' if 'madness' in os.path.basename(args.survey) else 'carnage'
    if not nivel:
        ap.error('falta --nivel')
    ident = args.id or (os.path.splitext(os.path.basename(args.out))[0] if args.out else None)
    if not ident:
        ap.error('falta --id')
    nv = L.nivel(nivel)
    print('nivel: %s, entrada $%02X, %d columnas, camara $%04X' % (
        nv.info['titulo'], nv.entrance, nv.cols, nv.camera_max))
    receta = generar(nv, args.seed, args.cols, args.inventar, args.min_segment, args.verbose)
    if not receta:
        print('no se encontro un orden que encaje', file=sys.stderr)
        return 1
    receta.update({'id': ident, 'nombre': args.nombre or ident.upper().replace('_', ' '),
                   'familia': 'generada', 'semilla': args.seed, 'autor': 'gen_stage.py',
                   'descripcion': args.descripcion or (
                       'Etapa generada: tramos de %s reordenados al azar (semilla %d)%s.'
                       % (nv.info['titulo'], args.seed,
                          ', %d tramos inventados pedidos' % args.inventar
                          if args.inventar else ''))})
    if args.out:
        out = args.out
    else:
        out = os.path.join(L.STAGES_DIR, ident + '.c2p')
    lay, _ = L.layout(receta, nv)
    for Lp in lay:
        p = Lp['pieza']
        if p['tipo'] != 'tramo':
            desc = p['tipo'] + (' %s px' % '+'.join(map(str, p['hoyos_px']))
                                if 'hoyos_px' in p else '')
            print('  inventado en X $%04X (y %04X): %s, %d columnas' % (
                Lp['inicio'] * COL_BYTES, nv.edge_y(Lp['cols'][0], 0) or 0, desc,
                len(Lp['cols'])))
            if args.verbose:
                print('    columnas origen: %s' % Lp['cols'])
    errs = L.validar(receta, nv)
    for e in errs:
        print('aviso: %s' % e)
    L.documentar(receta, nv)
    path = L.guardar_receta(receta)
    print('receta: %s' % path)
    if args.solo_receta:
        return 0
    data = L.compilar(receta, nv)
    with open(out, 'wb') as f:
        f.write(data)
    st = receta['estadisticas']
    print('etapa escrita: %s (%d columnas, %d objetos, %d grupos de platanos, '
          '%d tramos inventados)' % (out, st['columnas'], sum(st['objetos'].values()) + 1,
                                     st['grupos_platanos'], st['piezas_inventadas']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
