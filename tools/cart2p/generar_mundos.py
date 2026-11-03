"""Cart Chase 2P - 10 etapas de prueba "OTROS MUNDOS" (2026-10-12).

Cada etapa es la via de una etapa con tema ya hecha (e101-e180, sin
costuras) con la MUSICA y los ENEMIGOS del nivel del que sale el tema:
  - musica: la cancion de ese nivel (receta "musica", el juego la pone con
    su propia rutina de cambio de cancion, ver FORMATO_ETAPAS.txt)
  - enemigos: las plantillas (banco $B5) que usa ese nivel en su lista de
    objetos; se quitan la mitad de los enemigos originales (uno si, uno no)
    y se agregan los del otro mundo repartidos por la etapa, sobre via plana
    (los de suelo) o sobre la via (los voladores).
Solo plantillas comprobadas en el juego (headless con el bot y
DKC1_CART2P_OBJLOG: aparecen en su X y caminan o vuelan sobre la via de
carro; BITACORA 2026-10-12). Descartadas porque nunca aparecen al ponerlas
sueltas en la lista de objetos: Kritter $94ED, Krusha $B5AB y $BB89.

Uso:
  python tools\\cart2p\\generar_mundos.py            genera e181-e190 (recetas,
                                                  .c2p, menu y catalogo)
  python tools\\cart2p\\generar_mundos.py --ver      solo muestra el plan
"""
import argparse
import copy
import datetime
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import etapa_lib as L  # noqa: E402
import compilar_etapas as C  # noqa: E402

# Enemigos de cada mundo: (nombre, plantilla $B5, 'suelo' o 'aire').
# Salen de las listas de objetos de esos niveles (DATA_BD8000[entrada]).
MUNDOS = [
    {'id': 'e181_carnage_mundo_jungla', 'nombre': 'MUNDO JUNGLA', 'nivel': 'carnage',
     'base': 'e101_carnage_jungla01', 'tema': 'jungla', 'musica': 'isla',
     'origen': 'Jungle Hijinxs (DK Island Swing)',
     'enemigos': [('KRITTER', 0x9443, 'suelo'), ('GNAWTY', 0xB413, 'suelo'),
                  ('NECKY NUECES', 0xC703, 'aire')]},
    {'id': 'e182_carnage_mundo_nieve', 'nombre': 'MUNDO NIEVE', 'nivel': 'carnage',
     'base': 'e116_carnage_nieve01', 'tema': 'nieve', 'musica': 'nieve',
     'origen': 'Snow Barrel Blast (Northern Hemispheres)',
     'enemigos': [('KRUSHA', 0xB587, 'suelo'), ('KLAPTRAP', 0x9703, 'suelo'),
                  ('NECKY', 0xBC87, 'aire'), ('ZINGER', 0x9D3D, 'aire'),
                  ('GNAWTY', 0xB413, 'suelo')]},
    {'id': 'e183_carnage_mundo_arboles', 'nombre': 'MUNDO ARBOLES', 'nivel': 'carnage',
     'base': 'e121_carnage_arboles01', 'tema': 'arboles', 'musica': 'arboles',
     'origen': 'Tree Top Town (Treetop Rock)',
     'enemigos': [('KRITTER', 0x9C7F, 'suelo'), ('NECKY', 0xBCAB, 'aire'),
                  ('ZINGER', 0x9F87, 'aire'), ('GNAWTY', 0xB413, 'suelo')]},
    {'id': 'e184_carnage_mundo_templo', 'nombre': 'MUNDO TEMPLO', 'nivel': 'carnage',
     'base': 'e131_carnage_templo01', 'tema': 'templo', 'musica': 'templo',
     'origen': 'Temple Tempest y Millstone Mayhem (Voices of the Temple)',
     'enemigos': [('KRITTER', 0x9C4F, 'suelo'), ('SLIPPA', 0x9CCF, 'suelo'),
                  ('KRUSHA', 0xB5CB, 'suelo'), ('ZINGER', 0x9D21, 'aire'),
                  ('GNAWTY', 0xB413, 'suelo')]},
    {'id': 'e185_carnage_mundo_mina', 'nombre': 'MUNDO MINA OSCURA', 'nivel': 'carnage',
     'base': 'e136_carnage_mina01', 'tema': 'mina', 'musica': 'niebla',
     'origen': 'Misty Mine (Misty Menace)',
     'enemigos': [('KLAPTRAP', 0x96F5, 'suelo'), ('SLIPPA', 0x9CCF, 'suelo'),
                  ('ARMY', 0xA467, 'suelo'), ('GNAWTY', 0xB413, 'suelo')]},
    {'id': 'e186_madness_mundo_cueva', 'nombre': 'MUNDO CUEVA', 'nivel': 'madness',
     'base': 'e146_madness_cueva01', 'tema': 'cueva', 'musica': 'cueva',
     'origen': 'Bouncy Bonanza, Manic Mincers y Torchlight Trouble (Cave Dweller Concert)',
     'enemigos': [('KRITTER', 0x958D, 'suelo'), ('KRUSHA', 0xB559, 'suelo'),
                  ('KLUMP', 0x98B3, 'suelo'), ('ZINGER', 0x9D21, 'aire'),
                  ('KRITTER', 0x9B83, 'suelo')]},
    {'id': 'e187_madness_mundo_fabrica', 'nombre': 'MUNDO FABRICA', 'nivel': 'madness',
     'base': 'e151_madness_fabrica01', 'tema': 'fabrica', 'musica': 'fabrica',
     'origen': 'Oil Drum Alley y Blackout Basement (Fear Factory)',
     'enemigos': [('KRITTER', 0x9C15, 'suelo'), ('KLUMP', 0x98B3, 'suelo'),
                  ('KLAPTRAP', 0x9703, 'suelo'), ('GNAWTY', 0xB3F7, 'suelo'),
                  ('KRITTER', 0x9C4F, 'suelo')]},
    {'id': 'e188_madness_mundo_bosque', 'nombre': 'MUNDO BOSQUE', 'nivel': 'madness',
     'base': 'e166_madness_bosque01', 'tema': 'bosque', 'musica': 'bosque',
     'origen': 'Forest Frenzy y Vulture Culture (Forest Frenzy)',
     'enemigos': [('NECKY', 0xBCBD, 'aire'), ('ZINGER', 0x9E1D, 'aire'),
                  ('KRITTER', 0x953B, 'suelo'), ('MINI NECKY', 0xB86D, 'aire'),
                  ('KRITTER', 0x958D, 'suelo')]},
    {'id': 'e189_madness_mundo_plataformas', 'nombre': 'MUNDO PLATAFORMAS', 'nivel': 'madness',
     'base': 'e176_madness_mina01', 'tema': 'mina', 'musica': 'minas',
     'origen': 'Platform Perils y Trick Track Trek (Life in the Mines)',
     'enemigos': [('KRUSHA', 0xB5E3, 'suelo'), ('ARMY', 0xA47B, 'suelo'),
                  ('KLUMP', 0x999B, 'suelo'), ('MINI NECKY', 0xB863, 'aire'),
                  ('KRITTER', 0x9C27, 'suelo')]},
    {'id': 'e190_madness_mundo_luces', 'nombre': 'MUNDO LUCES', 'nivel': 'madness',
     'base': 'e171_madness_templo01', 'tema': 'templo', 'musica': 'niebla',
     'origen': 'Stop & Go Station y Loopy Lights (Misty Menace)',
     'enemigos': [('KLAPTRAP', 0x96BF, 'suelo'), ('KRITTER', 0x9B9F, 'suelo'),
                  ('NECKY NUECES', 0xC73F, 'aire'), ('ZINGER', 0x9FC9, 'aire'),
                  ('KLAPTRAP', 0x97D1, 'suelo')]},
]

AIRE_DY = 0x30          # voladores: esta altura sobre la via
INICIO_X = {'carnage': 0x700, 'madness': 0x800}
# nada antes: la partida ya reviso esa zona de objetos al grabar el estado
# inicial (un objeto agregado ahi no aparece nunca; medido en Madness $600)
MARGEN_META = 0x300     # nada en los ultimos px antes de la meta
SEPARACION = 0x100      # entre enemigos (agregados y originales)
MIN_AGREGAR = 8
MAX_AGREGAR = 14


def plano(nv, src, c):
    """Altura de la via en el centro de la columna c de la etapa, si hay una
    via es lo mas alto ahi (nada encima) y tambien hay via en las columnas
    vecinas (lejos de hoyos). Los enemigos de suelo siguen las pendientes."""
    if c < 1 or c + 1 >= len(src):
        return None
    for k in (c - 1, c + 1):
        if nv.edge_y(src[k], 0) is None or nv.edge_y(src[k], 1) is None:
            return None
    x = src[c] * L.COL_PX + L.COL_PX // 2
    r = L.rail_at(nv.surf, x)
    if not r or max(nv.surf[x & ~3])[0] != r[0]:
        return None             # sin via, o algo encima de la via
    return r[0]


def armar(m):
    base = L.cargar_receta(os.path.join(L.FUENTE_DIR, m['base'] + '.json'))
    nv = L.nivel(m['nivel'])
    r = copy.deepcopy(base)
    lay, src = L.layout(r, nv)
    res = L.resolver_objetos(r, nv, lay)
    originales = [o for o in res['objetos'] if o['nombre'] in L.ENEMIES]
    quitar = originales[1::2]
    r['objetos_quitar'] = [{'pieza': o['pieza'], 'indice': o['indice']} for o in quitar]
    quedan = [o for o in originales if o not in quitar]
    libres = res['limite'] - (res['total'] - len(quitar))
    n = max(0, min(MAX_AGREGAR, max(MIN_AGREGAR, len(quitar)), libres))
    exit_x = res['salida'][0]['x'] if res['salida'] else len(src) * L.COL_PX
    x0, x1 = INICIO_X[m['nivel']], exit_x - MARGEN_META
    ocupados = [o['x'] for o in quedan]
    agregar = []
    for k in range(n):
        nombre, ptr, clase = m['enemigos'][k % len(m['enemigos'])]
        objetivo = x0 + (x1 - x0) * (k + 0.5) / n
        mejor = None
        for c in range(x0 // L.COL_PX, x1 // L.COL_PX):
            y = plano(nv, src, c)
            if y is None:
                continue
            x = c * L.COL_PX + L.COL_PX // 2
            if any(abs(x - o) < SEPARACION for o in ocupados):
                continue
            if mejor is None or abs(x - objetivo) < abs(mejor[0] - objetivo):
                mejor = (x, y)
        if mejor is None:
            continue
        x, y = mejor
        ocupados.append(x)
        c = x // L.COL_PX
        pieza = max(i for i, Ly in enumerate(lay) if Ly['inicio'] <= c)
        agregar.append({'pieza': pieza, 'dx': x - lay[pieza]['inicio'] * L.COL_PX,
                        'y': y + (AIRE_DY if clase == 'aire' else 0), 'nombre': nombre,
                        'puntero': '$%04X' % ptr, 'enemigo': True})
    agregar.sort(key=lambda a: (a['pieza'], a['dx']))
    r['objetos_agregar'] = agregar
    hoy = datetime.date.today().isoformat()
    r.update({
        'id': m['id'], 'nombre': m['nombre'], 'tema': m['tema'], 'musica': m['musica'],
        'familia': 'OTROS MUNDOS',
        'descripcion': 'Etapa de prueba con la musica y los enemigos de %s, sobre la via de %s '
                       '(%s) con su tema. Se quitaron %d enemigos originales y se agregaron %d '
                       'del otro mundo (%s).' % (
                           m['origen'], base['nombre'], m['base'], len(quitar), len(agregar),
                           ', '.join(sorted({e[0] for e in m['enemigos']}))),
        'autor': 'Claude (generar_mundos.py)', 'creada': hoy, 'modificada': hoy,
        'notas': 'Prueba de enemigos y musica de otros niveles. Jugarla y ajustar en el editor '
                 '(los enemigos agregados se ven con borde blanco).',
        'historial': ['%s generada con generar_mundos.py desde %s' % (hoy, m['base'])],
    })
    r.pop('semilla', None)
    r.pop('generacion', None)
    return r, nv


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ver', action='store_true', help='solo mostrar el plan')
    args = ap.parse_args()
    ids = []
    for m in MUNDOS:
        r, nv = armar(m)
        errs = L.validar(r, nv)
        print('%-32s %-8s musica %-8s quita %2d agrega %2d %s' % (
            r['id'], r['tema'], r['musica'], len(r['objetos_quitar']),
            len(r['objetos_agregar']), ('ERRORES: %s' % errs) if errs else 'ok'))
        for a in r['objetos_agregar']:
            print('    pieza %2d dx %5d y $%04X %-12s %s' % (a['pieza'], a['dx'], a['y'],
                                                        a['nombre'], a['puntero']))
        if args.ver or errs:
            continue
        L.guardar_receta(r)
        ids.append(r['id'])
    if ids:
        # compila, documenta, agrega al menu y rehace el catalogo
        sys.argv = [sys.argv[0]] + ids
        return C.main()
    return 0


if __name__ == '__main__':
    sys.exit(main())
