#!/usr/bin/env python3
"""Cart Chase 2P - genera el lote de etapas (50 por nivel + las 10 primeras)
como recetas SIN COSTURA.

Version 2 (2026-10-09). La version 1 (2026-10-08, copia en
cart2p\\archivo\\2026-10-09_etapas_con_cortes\\generar_etapas_v1.py) unia
tramos solo por la altura de la via y dejaba ~20 cortes visibles por etapa:
el paisaje cambiaba de golpe y parecia otra etapa (reporte de prueba).
Ahora cada etapa es un recorrido por el nivel original que salta solo entre
COLUMNAS GEMELAS (columnas de la capa 1 identicas en dos lugares del nivel,
gen_stage.gemelas): el salto no se ve. Hacia adelante acorta, hacia atras
alarga (repite una parte). Sin piezas inventadas (rectas u hoyos copiados
dejan cortes).

Familias (por nivel); cada etapa elige, entre varios recorridos validos, el
que mejor cumple la intencion de su familia:
  REMIX     (10) largo original, 6-9 saltos, el que menos repite
  SPRINT    (10) 45-65 % del largo, 3-5 saltos
  HOYOS     (10) largo original, el que pasa por mas hoyos del nivel
  TRANQUILA (5)  80-100 %, el de menos enemigos y hoyos
  LARGA     (5)  el maximo que cabe en la memoria del juego
  MIXTA     (10) 70-95 %, 5-10 saltos
Mismos ids y nombres que la version 1. Determinista: misma version = mismas
etapas.

Uso: generar_etapas.py [--reemplazar]
"""
import argparse
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import etapa_lib as L  # noqa: E402
import gen_stage as G  # noqa: E402

HOY = '2026-10-09'

FAMILIAS = [
    # nombre, cantidad, (largo min, max) en fraccion o 'max', (saltos min, max),
    # objetivo, descripcion base
    ('REMIX', 10, (1.0, 1.0), (6, 9), 'variedad',
     'Recorrido sin costuras por el nivel, con el largo del original: salta '
     'entre columnas identicas a otras partes del nivel, en otro orden.'),
    ('SPRINT', 10, (0.45, 0.65), (3, 5), 'variedad',
     'Carrera corta sin costuras: solo una parte del recorrido. Para partidas '
     'rapidas y revanchas.'),
    ('HOYOS', 10, (1.0, 1.0), (6, 9), 'hoyos',
     'Recorrido sin costuras que pasa por la mayor cantidad posible de hoyos '
     'del nivel (repite los tramos con mas saltos).'),
    ('TRANQUILA', 5, (0.8, 1.0), (4, 7), 'tranquila',
     'Recorrido sin costuras con la menor cantidad de enemigos y hoyos que se '
     'encontro. Para aprender o para jugadores con menos experiencia.'),
    ('LARGA', 5, ('max', 'max'), (8, 12), 'variedad',
     'La etapa sin costuras mas larga que cabe en la memoria del juego. '
     'Carrera de resistencia.'),
    ('MIXTA', 10, (0.7, 0.95), (5, 10), 'variedad',
     'Recorrido sin costuras de largo intermedio con muchos saltos.'),
]

EXISTENTES = [
    # id, nivel, nombre, familia, objetivo, saltos, descripcion
    ('remix1_v3', 'carnage', 'REMIX 1', 'REMIX', 'variedad', 6,
     'Primera etapa remix (2026-10-06), rehecha sin costuras el 2026-10-09.'),
    ('remix_madness1_v3', 'madness', 'REMIX MADNESS 1', 'REMIX', 'variedad', 6,
     'Primer remix de Mine Cart Madness, rehecho sin costuras el 2026-10-09.'),
    ('inventada_carnage1', 'carnage', 'INVENTADA 1', 'HOYOS', 'hoyos', 6,
     'Etapa de hoyos de Carnage, rehecha sin costuras el 2026-10-09 (ya no '
     'tiene piezas inventadas: pasa por los hoyos del nivel).'),
    ('inventada_madness1', 'madness', 'INVENTADA MAD 1', 'HOYOS', 'hoyos', 6,
     'Etapa de hoyos de Madness, rehecha sin costuras el 2026-10-09.'),
    ('azar_madness_67311', 'madness', 'AZAR MADNESS 67311', 'AZAR', 'variedad', 6,
     'Etapa del randomizador (2026-10-07), rehecha sin costuras.'),
    ('azar_carnage_80897', 'carnage', 'AZAR CARNAGE 80897', 'AZAR', 'variedad', 6,
     'Etapa del randomizador (2026-10-07), rehecha sin costuras.'),
    ('azar_madness_98101', 'madness', 'AZAR MADNESS 98101', 'AZAR', 'variedad', 6,
     'Etapa del randomizador (2026-10-07), rehecha sin costuras.'),
    ('azar_madness_8378', 'madness', 'AZAR MADNESS 8378', 'AZAR', 'variedad', 6,
     'Etapa del randomizador (2026-10-07), rehecha sin costuras.'),
    ('azar_carnage_75658', 'carnage', 'AZAR CARNAGE 75658', 'AZAR', 'variedad', 6,
     'Etapa del randomizador (2026-10-07), rehecha sin costuras (era la de '
     'los cambios de paisaje que se reporto en pruebas).'),
    ('azar_madness_38819', 'madness', 'AZAR MADNESS 38819', 'AZAR', 'variedad', 6,
     'Etapa del randomizador (2026-10-07), rehecha sin costuras.'),
]


# Etapas con tema (graficos de otro mundo detras de la via de Carnage,
# tools/cart2p/temas.py): 5 por tema, e101 en adelante.
TITULOS_TEMA = {'jungla': 'JUNGLA', 'cueva': 'CUEVA', 'fabrica': 'FABRICA',
                'nieve': 'NIEVE', 'arboles': 'ALDEA ARBOREA', 'bosque': 'BOSQUE',
                'templo': 'TEMPLO', 'mina': 'MINA OSCURA'}
DESC_TEMA = {
    'jungla': 'La via de Mine Cart Carnage cruza la jungla de Jungle Hijinxs.',
    'cueva': 'La via cruza las cuevas de Bouncy Bonanza.',
    'fabrica': 'La via pasa frente a la fabrica de Oil Drum Alley.',
    'nieve': 'La via cruza los cerros nevados de Snow Barrel Blast.',
    'arboles': 'La via pasa entre los arboles y casas de Tree Top Town.',
    'bosque': 'La via cruza el bosque de Forest Frenzy.',
    'templo': 'La via pasa frente a las ruinas de Temple Tempest.',
    'mina': 'La via cruza la mina oscura de Misty Mine.',
}


def hoyos_del_nivel(nv):
    """X de inicio de cada hoyo del nivel original (sin superficie)."""
    out = []
    x = 0x200
    xmax = nv.camera_max + 256
    while x < xmax:
        if nv.surf.get(x):
            x += 4
            continue
        s = x
        while x < xmax and not nv.surf.get(x):
            x += 4
        if x - s >= 16:
            out.append(s)
    return out


def metricas(r, nv, hoyos):
    nh = 0
    vistas = set()
    rep = 0
    for p in r['piezas']:
        a, b = p['desde'], p['hasta']
        nh += sum(1 for h in hoyos if a * 32 <= h < b * 32)
        for c in range(a, b):
            rep += c in vistas
            vistas.add(c)
    st = L.estadisticas(r, nv)
    return {'hoyos': nh, 'repetidas': rep, 'enemigos': st['enemigos'], 'st': st}


def elegir(nv, semilla, cols, saltos, objetivo, hoyos, vistos, intentos=10):
    """Mejor recorrido entre varios para el objetivo de la familia."""
    mejor = None
    for k in range(intentos):
        r = G.generar_sin_costura(nv, semilla * 31 + k, cols, saltos)
        if not r or L.validar(r, nv) or L.cortes_visibles(r, nv):
            continue
        data = L.compilar(r, nv)
        h = hashlib.sha1(data).hexdigest()
        if h in vistos:
            continue
        m = metricas(r, nv, hoyos)
        if objetivo == 'hoyos':
            puntaje = -m['hoyos'] * 10 + m['repetidas'] // 8
        elif objetivo == 'tranquila':
            puntaje = m['enemigos'] * 3 + m['hoyos'] * 4
        else:
            puntaje = m['repetidas']
        if mejor is None or puntaje < mejor[0]:
            mejor = (puntaje, r, h, m)
    return mejor


def dificultad(st, nh):
    return st['enemigos'] * 1.0 + nh * 1.5 + st['columnas'] / 40.0


def describir(base, m, saltos):
    st = m['st']
    return ('%s %d columnas (%d %% del original), unos %s s, %d saltos sin costura, '
            'pasa por %d hoyos del nivel, %d enemigos.'
            % (base, st['columnas'], round(100 * st['largo_vs_original']),
               st['segundos_aprox'], saltos, m['hoyos'], st['enemigos']))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--reemplazar', action='store_true')
    args = ap.parse_args()
    os.makedirs(L.FUENTE_DIR, exist_ok=True)
    vistos = set()
    lote = []
    trabajos = []
    numero = 0
    for nivel in ('carnage', 'madness'):
        for fam, cant, (lmin, lmax), (smin, smax), obj, base in FAMILIAS:
            for k in range(cant):
                numero += 1
                trabajos.append(('e%03d_%s_%s%02d' % (numero, nivel, fam.lower(), k + 1),
                                 nivel, '%s %s %02d' % (nivel.upper(), fam, k + 1), fam, obj,
                                 base, lmin, lmax, smin + k % (smax - smin + 1), numero, k))
    for i, (ident, nivel, nombre, fam, obj, sal, desc) in enumerate(EXISTENTES):
        trabajos.append((ident, nivel, nombre, fam, obj, desc, 1.0, 1.0, sal, 900 + i, i))
    for ti, tema in enumerate(L.TEMAS):
        for k in range(5):
            trabajos.append(('e%03d_carnage_%s%02d' % (101 + ti * 5 + k, tema, k + 1),
                             'carnage', '%s %02d' % (TITULOS_TEMA[tema], k + 1), 'TEMA',
                             'variedad', DESC_TEMA[tema] + ' Recorrido sin costuras.',
                             (0.6, 1.0, 0.8, 1.0, 0.7)[k], None, 5 + k % 4, 2000 + ti * 10 + k,
                             tema))
    # los mismos temas detras de la via de Mine Cart Madness: e141 en adelante
    for ti, tema in enumerate(L.TEMAS):
        for k in range(5):
            trabajos.append(('e%03d_madness_%s%02d' % (141 + ti * 5 + k, tema, k + 1),
                             'madness', '%s MAD %02d' % (TITULOS_TEMA[tema], k + 1), 'TEMA',
                             'variedad', 'Via de Mine Cart Madness. ' + DESC_TEMA[tema] +
                             ' Recorrido sin costuras.',
                             (0.6, 1.0, 0.8, 1.0, 0.7)[k], None, 5 + k % 4, 3000 + ti * 10 + k,
                             tema))
    for trabajo in trabajos:
        ident, nivel, nombre, fam, obj, base, lmin, lmax, saltos, semilla, k = trabajo[:11]
        tema = None
        if len(trabajo) > 10 and isinstance(trabajo[10], str):
            tema, k = trabajo[10], 0
            lmax = lmin
        path = os.path.join(L.FUENTE_DIR, ident + '.json')
        anterior = L.cargar_receta(path) if os.path.exists(path) else None
        if anterior and not args.reemplazar:
            continue
        nv = L.nivel(nivel)
        hoyos = hoyos_del_nivel(nv)
        if lmin == 'max':
            cols = nv.max_cols
        else:
            frac = lmin + (lmax - lmin) * (k % 10) / 9.0
            cols = 0 if frac >= 0.999 else int(nv.cols * frac)
        res = elegir(nv, semilla, cols, saltos, obj, hoyos, vistos)
        if not res:
            print('no se pudo generar %s' % ident)
            continue
        _, r, h, m = res
        vistos.add(h)
        hist = list(anterior.get('historial', [])) if anterior else []
        linea = ('%s rehecha sin costuras (version con cortes en '
                 'cart2p\\archivo\\2026-10-09_etapas_con_cortes\\)' % HOY
                 if anterior else '%s generada sin costuras' % HOY)
        if not any(x.startswith('%s rehecha sin costuras' % HOY) for x in hist):
            hist.append(linea)
        if tema:
            r['tema'] = tema
        r.update({
            'id': ident, 'nombre': nombre, 'familia': fam,
            'autor': 'Claude (generar_etapas.py v2, sin costuras)',
            'creada': anterior.get('creada', HOY) if anterior else HOY,
            'semilla': semilla,
            'generacion': {'metodo': 'sin_costura', 'semilla': semilla, 'columnas': cols,
                           'saltos_min': saltos, 'objetivo': obj},
            'notas': 'Etapa sin costuras: cada union salta a una columna identica de otra '
                     'parte del nivel. Probarla jugando y ajustarla en el editor; si se '
                     'agregan piezas, preferir las de "SIN CORTE".',
            'historial': hist,
        })
        L.documentar(r, nv)
        r['descripcion'] = describir(base, m, len(r['piezas']) - 1)
        r['_dif'] = dificultad(r['estadisticas'], m['hoyos'])
        lote.append(r)
    for nivel in ('carnage', 'madness'):
        grupo = sorted([r for r in lote if r['nivel'] == nivel], key=lambda r: r['_dif'])
        for n, r in enumerate(grupo):
            r['dificultad'] = ('FACIL', 'MEDIA', 'DIFICIL')[min(2, n * 3 // max(1, len(grupo)))]
    for r in lote:
        del r['_dif']
        L.guardar_receta(r)
    print('recetas escritas: %d' % len(lote))
    return 0


if __name__ == '__main__':
    sys.exit(main())
