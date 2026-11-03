#!/usr/bin/env python3
"""Cart Chase 2P - randomizador de etapas de carro.

Genera N etapas nuevas al azar SIN COSTURAS (nivel Carnage o Madness y
semilla al azar; recorrido que salta entre columnas identicas del nivel,
gen_stage.generar_sin_costura, igual que ETAPAS AL AZAR del juego). Cada una queda como RECETA editable en
cart2p\\etapas_fuente\\azar_<nivel>_<semilla>.json, compilada en
cart2p\\stages\\ y agregada al final del menu (cart2p\\etapas.txt).
Nunca sobrescribe una etapa existente: si el id ya existe, sortea otro.

Uso: azar_etapas.py --cantidad N [--semilla S]
"""
import argparse
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import etapa_lib as L  # noqa: E402
import gen_stage as G  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cantidad', type=int, default=6)
    ap.add_argument('--semilla', type=int, default=0,
                    help='semilla del sorteo (0 = segun la hora)')
    ap.add_argument('--min-inventados', type=int, default=6,
                    help='mas = mas saltos (saltos minimos = 5 + inventados / 2)')
    ap.add_argument('--max-inventados', type=int, default=12)
    args = ap.parse_args()

    rng = random.Random(args.semilla or time.time_ns())
    hecho = 0
    tries = 0
    while hecho < args.cantidad and tries < args.cantidad * 10:
        tries += 1
        nivel = rng.choice(sorted(L.NIVELES))
        seed = rng.randint(1, 99999)
        inv = rng.randint(args.min_inventados, args.max_inventados)
        ident = 'azar_%s_%d' % (nivel, seed)
        if os.path.exists(os.path.join(L.FUENTE_DIR, ident + '.json')) or \
                os.path.exists(os.path.join(L.STAGES_DIR, ident + '.c2p')):
            continue
        nv = L.nivel(nivel)
        saltos = 5 + inv // 2
        r = G.generar_sin_costura(nv, seed, 0, saltos)
        if not r:
            continue
        if nivel in L.TEMAS_NIVELES:
            k = rng.randint(0, len(L.TEMAS))   # 0 = sin tema
            if k:
                r['tema'] = L.TEMAS[k - 1]
                r['musica'] = L.TEMA_MUSICA[r['tema']]
        r.update({'id': ident, 'nombre': 'AZAR %s %d' % (nivel.upper(), seed),
                  'familia': 'AZAR', 'autor': 'azar_etapas.py', 'semilla': seed,
                  'creada': time.strftime('%Y-%m-%d'),
                  'generacion': {'semilla': seed, 'columnas': 0, 'saltos_min': saltos,
                                 'sin_costura': True},
                  'historial': ['%s generada por el randomizador' % time.strftime('%Y-%m-%d')]})
        errs, out = L.compilar_receta(r)
        if not out:
            print('descartada %s: %s' % (ident, errs[0]))
            continue
        st = r['estadisticas']
        r['descripcion'] = ('Etapa del randomizador, sin costuras: un recorrido por %s '
                            'que salta %d veces entre columnas identicas (el corte no se '
                            've). %d columnas, unos %s s, %d enemigos.'
                            % (nv.info['titulo'], len(r['piezas']) - 1, st['columnas'],
                               st['segundos_aprox'], st['enemigos']))
        L.guardar_receta(r)
        L.asegurar_en_lista(r)
        print('%s: %d columnas, %d cortes visibles' % (ident, st['columnas'],
                                                       st['cortes_visibles']))
        hecho += 1
    print('%d etapas nuevas agregadas al menu (%s)' % (hecho, L.LISTA))
    return 0 if hecho == args.cantidad else 1


if __name__ == '__main__':
    sys.exit(main())
