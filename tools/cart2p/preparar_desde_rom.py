#!/usr/bin/env python3
"""Cart Chase 2P - prepara desde la ROM del usuario todo lo que sale del juego.

El proyecto publicable no trae nada copiado de la ROM: ni estados, ni mapas,
ni graficos. Este script los crea en el equipo del usuario a partir de SU
ROM (Donkey Kong Country USA v1.0) y del juego ya compilado:

  1 states\\*.state            arranque limpio + guiones .dks (to_map, enter,
                               ride, ride_madness) y parche de $003E
  2 stages\\vias_<nivel>.txt   perfil de vias (DKC1_CART2P_SURVEY)
  3 stages\\tema_*.c2t         pieles de otros mundos (temas.py generar)
  4 stages\\<id>.c2p           etapas compiladas desde etapas_fuente\\*.json
  5 editor\\datos\\*           graficos y datos del editor HTML (exportar_editor.py)

Cada archivo se crea solo si falta (--forzar los rehace). Nunca toca las
recetas ni los records.

Uso (desde la raiz del workspace):
  python tools\\cart2p\\preparar_desde_rom.py
  python tools\\cart2p\\preparar_desde_rom.py --verificar
  python tools\\cart2p\\preparar_desde_rom.py --destino C:\\tmp\\prueba   (copia de cart2p)
  python tools\\cart2p\\preparar_desde_rom.py --huellas                  (reescribe huellas)

--verificar compara cada archivo creado con tools\\cart2p\\huellas_preparado.txt
(SHA-256; solo huellas, sin datos del juego).
"""
import argparse
import copy
import glob
import hashlib
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
ROM_SHA256 = 'fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15'
ROM_SIZE = 0x400000
HUELLAS = os.path.join(HERE, 'huellas_preparado.txt')

# estado -> (estado de partida, guion, cuadros, parche $003E, guardar en el cuadro)
ESTADOS = [
    ('map.state', None, 'to_map.dks', 12000, None, None),
    ('map_carnage.state', 'map.state', None, 0, 0x002E, None),
    ('map_madness.state', 'map.state', None, 0, 0x0027, None),
    ('carnage_inicio.state', None, 'enter.dks', 8000, None, None),
    ('madness_inicio.state', None, 'enter_madness.dks', 8000, None, None),
    ('carnage_montado.state', None, 'ride.dks', 3000, None, None),
    ('madness_montado.state', None, 'ride_madness.dks', 900, None, 900),
]
NIVELES = [('carnage', 'carnage_montado.state'), ('madness', 'madness_montado.state')]


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def paso(msg):
    print('\n== ' + msg, flush=True)


class Preparar:
    def __init__(self, a):
        self.a = a
        self.rom = os.path.abspath(a.rom)
        self.exe = os.path.abspath(a.exe)
        self.dst = os.path.abspath(a.destino)
        self.creados = []
        self.env = dict(os.environ, CART2P_DIR=self.dst, CART2P_ROM=self.rom,
                        DKC1_WIDESCREEN='0')
        for k in ('DKC1_SCRIPT', 'DKC1_SAVESTATE_INPUT', 'DKC1_SAVESTATE_OUTPUT',
                  'DKC1_SAVESTATE_SAVE_AT', 'DKC1_CART2P_STAGE', 'DKC1_CART2P_LISTA',
                  'DKC1_CART2P_INICIO', 'DKC1_INPUT_RECORD', 'DKC1_CART2P_SURVEY',
                  'DKC1_CART2P_EXPORT'):
            self.env.pop(k, None)

    def falta(self, path):
        return self.a.forzar or not os.path.exists(path)

    def creado(self, path):
        self.creados.append(path)
        print('  creado', os.path.relpath(path, self.dst))

    # ---------------------------------------------------------------- 0
    def revisar_rom(self):
        paso('ROM')
        if not os.path.isfile(self.rom):
            sys.exit('no esta la ROM: %s\nCopiar Donkey Kong Country (USA) v1.0 sin cabecera '
                     'a rom\\dkc1.sfc' % self.rom)
        import verificar_rom as V
        ok, msg, _ = V.revisar(self.rom)
        if not ok or os.path.getsize(self.rom) != ROM_SIZE:
            sys.exit('ROM %s: %s\nCorrer python tools\\cart2p\\verificar_rom.py (quita la '
                     'cabecera si hace falta). Solo sirve USA v1.0, SHA-256 %s'
                     % (self.rom, msg, ROM_SHA256))
        print('  USA v1.0 verificada:', self.rom)
        if not os.path.isfile(self.exe):
            sys.exit('falta %s (compilar el juego primero: cart2p\\JUGAR.bat compilar)' % self.exe)

    def headless(self, frames, cwd, **env):
        e = dict(self.env)
        e.update({k: str(v) for k, v in env.items()})
        r = subprocess.run([self.exe, self.rom, str(frames)], cwd=cwd, env=e,
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit('el juego (headless) fallo rc=%d: %s' % (r.returncode, r.stderr[-600:]))

    # ---------------------------------------------------------------- 1
    def estados(self):
        paso('1 estados de partida (states)')
        sd = os.path.join(self.dst, 'states')
        os.makedirs(sd, exist_ok=True)
        for dks in glob.glob(os.path.join(ROOT, 'cart2p', 'states', '*.dks')):
            if os.path.normcase(os.path.dirname(dks)) != os.path.normcase(sd):
                shutil.copy2(dks, sd)
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import poke_test as P  # localiza la WRAM dentro del estado
        from pathlib import Path
        for nombre, base, guion, frames, parche, save_at in ESTADOS:
            out = os.path.join(sd, nombre)
            if not self.falta(out):
                continue
            if parche is not None:
                src = os.path.join(sd, base)
                wram = P.run_frames(Path(self.exe), Path(self.rom), Path(src), 0, Path(sd), False)
                st = bytearray(open(src, 'rb').read())
                at = P.locate_wram(st, wram) + 0x3E
                st[at:at + 2] = parche.to_bytes(2, 'little')
                with open(out, 'wb') as f:
                    f.write(st)
                for tmp in ('poke_out.wram.bin',):
                    if os.path.exists(os.path.join(sd, tmp)):
                        os.remove(os.path.join(sd, tmp))
            else:
                extra = {'DKC1_SCRIPT': guion}
                if save_at:
                    extra.update(DKC1_SAVESTATE_OUTPUT=out, DKC1_SAVESTATE_SAVE_AT=save_at)
                self.headless(frames, sd, **extra)
            if not os.path.exists(out):
                sys.exit('no se creo %s' % out)
            self.creado(out)

    # ---------------------------------------------------------------- 2
    def perfiles(self):
        paso('2 perfiles de vias')
        for nivel, estado in NIVELES:
            out = os.path.join(self.dst, 'stages', 'vias_%s.txt' % nivel)
            if not self.falta(out):
                continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            tmp = os.path.join(self.dst, 'logs')
            os.makedirs(tmp, exist_ok=True)
            self.headless(2, tmp, DKC1_CART2P='2', DKC1_CART2P_TEST_NOCOUNT='1',
                          DKC1_SAVESTATE_INPUT=os.path.join(self.dst, 'states', estado),
                          DKC1_CART2P_SURVEY=out,
                          DKC1_CART2P_RECORDS=os.path.join(tmp, 'records_preparar.txt'),
                          DKC1_CART2P_LOGFILE=os.path.join(tmp, 'preparar_log.txt'))
            for f in ('records_preparar.txt', 'preparar_log.txt'):
                if os.path.exists(os.path.join(tmp, f)):
                    os.remove(os.path.join(tmp, f))
            if not os.path.exists(out):
                sys.exit('no se creo %s' % out)
            self.creado(out)

    # ---------------------------------------------------------------- 3 y 4
    def lib(self):
        os.environ['CART2P_DIR'] = self.dst
        os.environ['CART2P_ROM'] = self.rom
        sys.path.insert(0, HERE)
        import etapa_lib as L
        return L

    def pieles(self):
        paso('3 pieles de otros mundos (.c2t)')
        L = self.lib()
        import temas as T
        for base in L.TEMAS_NIVELES:
            for nombre in L.TEMAS:
                out = os.path.join(L.STAGES_DIR, T.nombre_piel(base, nombre))
                if not self.falta(out):
                    continue
                with open(out, 'wb') as f:
                    f.write(T.Piel(base, nombre).bytes())
                self.creado(out)

    def etapas(self):
        paso('4 etapas (.c2p) desde las recetas')
        L = self.lib()
        n = 0
        for path in sorted(glob.glob(os.path.join(L.FUENTE_DIR, '*.json'))):
            r = L.cargar_receta(path)
            out = os.path.join(L.STAGES_DIR, r['id'] + '.c2p')
            if not self.falta(out):
                continue
            r = copy.deepcopy(r)  # la receta en disco no se toca
            errs, _ = L.compilar_receta(r, escribir=True)
            if not os.path.exists(out):
                print('  ERROR %s: %s' % (r['id'], '; '.join(errs)))
                continue
            self.creados.append(out)
            n += 1
        print('  etapas creadas: %d' % n)

    # ---------------------------------------------------------------- 5
    def editor(self):
        paso('5 datos del editor HTML')
        datos = os.path.join(self.dst, 'editor', 'datos')
        salidas = [os.path.join(datos, '%s.%s' % (n, e)) for n in ('carnage', 'madness')
                   for e in ('png', 'json')]
        if not self.a.forzar and all(os.path.exists(p) for p in salidas):
            return
        if self.a.forzar:
            for n in ('carnage', 'madness'):
                g = os.path.join(datos, 'gfx_%s.bin' % n)
                if os.path.exists(g):
                    os.remove(g)
        r = subprocess.run([sys.executable, os.path.join(HERE, 'exportar_editor.py')],
                           env=self.env, capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit('exportar_editor fallo: %s' % (r.stderr or r.stdout)[-600:])
        for p in salidas + [os.path.join(datos, 'gfx_%s.bin' % n) for n in ('carnage', 'madness')]:
            if os.path.exists(p):
                self.creado(p)

    # ---------------------------------------------------------------- huellas
    def archivos(self):
        pats = ['states/*.state', 'stages/vias_*.txt', 'stages/*.c2t', 'stages/*.c2p',
                'editor/datos/*.png', 'editor/datos/*.json']
        # gfx_*.bin (volcado intermedio de VRAM) queda fuera: cambia con el build;
        # lo que se dibuja con el (png/json) si se compara
        out = []
        for p in pats:
            out += glob.glob(os.path.join(self.dst, *p.split('/')))
        return sorted(set(out))

    def huellas(self):
        with open(HUELLAS, 'w', encoding='ascii', newline='\r\n') as f:
            f.write('# SHA-256 de lo que crea preparar_desde_rom.py desde la ROM USA v1.0.\n'
                    '# Solo huellas: no contiene datos del juego.\n')
            for p in self.archivos():
                f.write('%s  %s\n' % (sha(p), os.path.relpath(p, self.dst).replace(os.sep, '/')))
        print('huellas escritas:', HUELLAS)

    def verificar(self):
        paso('verificacion contra huellas_preparado.txt')
        if not os.path.exists(HUELLAS):
            sys.exit('no hay huellas (correr --huellas sobre un preparado bueno)')
        bien = mal = faltan = 0
        for line in open(HUELLAS, encoding='ascii'):
            if line.startswith('#') or not line.strip():
                continue
            h, rel = line.split(None, 1)
            p = os.path.join(self.dst, *rel.strip().split('/'))
            if not os.path.exists(p):
                faltan += 1; print('  FALTA', rel.strip())
            elif sha(p) != h:
                mal += 1; print('  DISTINTO', rel.strip())
            else:
                bien += 1
        print('  identicos %d, distintos %d, faltan %d' % (bien, mal, faltan))
        return mal == 0 and faltan == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--rom', default=os.path.join(ROOT, 'rom', 'dkc1.sfc'))
    ap.add_argument('--exe', default=os.path.join(ROOT, 'build-windows', 'release',
                                                  'dkc1_snesrecomp_headless.exe'))
    ap.add_argument('--destino', default=os.path.join(ROOT, 'cart2p'),
                    help='carpeta cart2p de destino (por defecto la del workspace)')
    ap.add_argument('--forzar', action='store_true', help='rehace aunque el archivo exista')
    ap.add_argument('--verificar', action='store_true')
    ap.add_argument('--huellas', action='store_true')
    a = ap.parse_args()
    pr = Preparar(a)
    if os.path.normcase(pr.dst) != os.path.normcase(os.path.join(ROOT, 'cart2p')):
        # destino aparte: necesita las recetas y la lista de etapas
        for sub in ('etapas_fuente',):
            if not os.path.exists(os.path.join(pr.dst, sub)):
                shutil.copytree(os.path.join(ROOT, 'cart2p', sub), os.path.join(pr.dst, sub))
        for f in ('etapas.txt',):
            if not os.path.exists(os.path.join(pr.dst, f)):
                shutil.copy2(os.path.join(ROOT, 'cart2p', f), pr.dst)
    if a.huellas:
        pr.huellas()
        return 0
    pr.revisar_rom()
    pr.estados()
    pr.perfiles()
    pr.pieles()
    pr.etapas()
    pr.editor()
    print('\nlisto: %d archivos creados en %s' % (len(pr.creados), pr.dst))
    if a.verificar:
        return 0 if pr.verificar() else 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
