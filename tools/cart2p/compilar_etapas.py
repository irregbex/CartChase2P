#!/usr/bin/env python3
"""Cart Chase 2P - compila las recetas de etapas.

Lee cart2p\\etapas_fuente\\*.json (o solo las indicadas), valida cada una,
completa su documentacion (nota por pieza, estadisticas), escribe el .c2p
en cart2p\\stages\\, la agrega al menu (cart2p\\etapas.txt) si falta, y
regenera el catalogo cart2p\\etapas_fuente\\CATALOGO_ETAPAS.txt.

Uso: compilar_etapas.py [id ...] [--sin-catalogo] [--revisar]
  --revisar  solo valida y muestra problemas (no escribe nada)
"""
import argparse
import glob
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import etapa_lib as L  # noqa: E402


def todas():
    return sorted(glob.glob(os.path.join(L.FUENTE_DIR, '*.json')))


def catalogo(recetas):
    """Texto del catalogo de etapas (para leer y elegir que editar)."""
    por_nivel = defaultdict(list)
    for r in recetas:
        por_nivel[r['nivel']].append(r)
    out = []
    out.append('CART CHASE 2P - CATALOGO DE ETAPAS')
    out.append('=' * 34)
    out.append('Generado por tools\\cart2p\\compilar_etapas.py. No editar a mano: se')
    out.append('rehace al compilar. La fuente de cada etapa es su receta')
    out.append('cart2p\\etapas_fuente\\<id>.json (formato en')
    out.append('cart2p\\docs\\FORMATO_ETAPAS.txt); se edita con el editor del juego')
    out.append('(menu principal, EDITOR), con el editor HTML de respaldo')
    out.append('(cart2p\\JUGAR.bat opcion 2) o a mano.')
    out.append('')
    out.append('Columnas: 1 columna = 32 px. Segundos: tiempo aproximado de un')
    out.append('recorrido sin morir (3,5 px por frame). Hoyos: hoyos inventados')
    out.append('(los del nivel original no se cuentan). Enemigos: Zinger, Necky,')
    out.append('Gnawty y carros enemigos. Corte: uniones donde el paisaje cambia de')
    out.append('golpe (0 = sin costuras).')
    out.append('')
    for nivel in sorted(por_nivel):
        lst = por_nivel[nivel]
        out.append('-' * 72)
        out.append('%s (%d etapas)' % (L.NIVELES[nivel]['titulo'].upper(), len(lst)))
        out.append('-' * 72)
        out.append('%-28s %-24s %5s %5s %4s %4s %3s %5s' % (
            'ID', 'NOMBRE EN EL MENU', 'COLS', 'SEG', 'PIEZ', 'HOYO', 'ENE', 'CORTE'))
        for r in lst:
            st = r.get('estadisticas', {})
            out.append('%-28s %-24s %5s %5s %4s %4s %3s %5s' % (
                r['id'][:28], L.nombre_valido(r['nombre'])[:24], st.get('columnas', '?'),
                st.get('segundos_aprox', '?'), st.get('piezas', '?'),
                st.get('hoyos_inventados', '?'), st.get('enemigos', '?'),
                st.get('cortes_visibles', '?')))
        out.append('')
        for r in lst:
            st = r.get('estadisticas', {})
            out.append('* %s  "%s"  (familia %s, dificultad %s%s)' % (
                r['id'], L.nombre_valido(r['nombre']), r.get('familia', '-'),
                r.get('dificultad', '-'),
                ', tema %s' % r['tema'] if r.get('tema') else ''))
            out.append('  %s' % r.get('descripcion', ''))
            gen = r.get('generacion')
            if gen:
                out.append('  Generacion: %s' % ', '.join('%s=%s' % kv for kv in gen.items()))
            out.append('  Piezas (%d): %s' % (len(r['piezas']), resumen_piezas(r)))
            objs = st.get('objetos', {})
            if objs:
                out.append('  Objetos: %s' % ', '.join('%d %s' % (n, k)
                                                      for k, n in objs.items()))
            out.append('')
    return '\n'.join(out) + '\n'


def resumen_piezas(r):
    partes = []
    for p in r['piezas']:
        t = p['tipo']
        if t == 'tramo':
            partes.append('T%d-%d' % (p['desde'], p['hasta'] - 1))
        elif t == 'recta':
            partes.append('R%d' % p['largo'])
        elif t == 'hoyo':
            partes.append('H')
        elif t == 'doble':
            partes.append('HH')
        else:
            partes.append('C%d' % len(p.get('columnas', [])))
    return ' '.join(partes)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ids', nargs='*')
    ap.add_argument('--sin-catalogo', action='store_true')
    ap.add_argument('--revisar', action='store_true')
    ap.add_argument('--sin-lista', action='store_true',
                    help='no agrega las etapas al menu (etapas.txt)')
    args = ap.parse_args()
    paths = [os.path.join(L.FUENTE_DIR, i if i.endswith('.json') else i + '.json')
             for i in args.ids] if args.ids else todas()
    ok = fallas = 0
    for path in paths:
        r = L.cargar_receta(path)
        nv = L.nivel(r['nivel'])
        errs = L.validar(r, nv)
        graves = [e for e in errs if not e.startswith('demasiados objetos')]
        for e in errs:
            print('%s: %s%s' % (r['id'], 'ERROR ' if e in graves else 'aviso ', e))
        if graves:
            fallas += 1
            continue
        if args.revisar:
            ok += 1
            continue
        errs, out = L.compilar_receta(r)
        L.guardar_receta(r, path)
        if not args.sin_lista:
            L.asegurar_en_lista(r)
        ok += 1
    print('recetas compiladas: %d, con errores: %d' % (ok, fallas))
    if not args.sin_catalogo and not args.revisar:
        recetas = [L.cargar_receta(p) for p in todas()]
        with open(os.path.join(L.FUENTE_DIR, 'CATALOGO_ETAPAS.txt'), 'w',
                  encoding='utf-8', newline='\r\n') as f:
            f.write(catalogo(recetas))
        print('catalogo: %s' % os.path.join(L.FUENTE_DIR, 'CATALOGO_ETAPAS.txt'))
    return 1 if fallas else 0


if __name__ == '__main__':
    sys.exit(main())
