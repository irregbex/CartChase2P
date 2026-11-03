#!/usr/bin/env python3
"""Cart Chase 2P - paquete publicable (solo codigo fuente, nada de la ROM).

Arma un .zip con el codigo del proyecto y las recetas de etapas, y deja
fuera todo lo que sale del juego (ROM, codigo generado, estados, mapas,
pieles, graficos del editor), los datos personales (records, mandos,
registros, bitacora) y lo que tiene otra licencia y se descarga aparte
(snesrecomp, desensamblado de referencia).

Antes de cerrar el zip, ESCANEA cada archivo contra la ROM: cualquier
tramo de 32 bytes identico a la ROM (salvo relleno) detiene el paquete.

Uso (desde la raiz del workspace):
  python tools\\cart2p\\empaquetar_publico.py --salida <carpeta>
  python tools\\cart2p\\empaquetar_publico.py --solo-escanear   (lista y escanea, sin zip)
"""
import argparse
import fnmatch
import hashlib
import os
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
VENTANA, PASO = 32, 8

# Lo propio del mod (no esta en git del motor original). Todo lo demas sale
# de los archivos que sigue git (codigo del motor DKC1Recomp, MIT).
INCLUIR = [
    'runner/cart2p/*', 'tools/cart2p/*.py', 'tools/cart2p/huellas_preparado.txt',
    'cart2p/JUGAR.bat', 'cart2p/EDITOR_WEB.bat', 'cart2p/etapas.txt',
    'cart2p/LICENCIA_Y_AVISOS.txt', 'cart2p/README_GITHUB.md',
    'cart2p/docs/*.txt', 'cart2p/etapas_fuente/*.json', 'cart2p/etapas_fuente/*.txt',
    'cart2p/editor/*.html', 'cart2p/editor/*.js', 'cart2p/editor/*.css',
    'cart2p/editor/*.ico', 'cart2p/editor/*.png',
    'cart2p/states/*.dks', 'rom/LEEME.txt',
]
EXCLUIR = [
    # sale de la ROM o del juego corriendo
    'generated/*', 'rom/*.sfc', 'rom/*.smc', '*.state', '*.c2p', '*.c2t', '*.srm',
    'cart2p/stages/*', 'cart2p/editor/datos/*', '*.ppm', '*.pcm', '*.wav',
    # otra licencia, se descarga aparte
    'snesrecomp/*', 'reference/*',
    # compilados y temporales
    'build/*', 'build-*/*', '*/__pycache__/*', '*.pyc', '*/bin/*', '*/obj/*',
    # datos personales y de desarrollo
    'cart2p/archivo/*', 'cart2p/logs/*', 'cart2p/records.txt', 'cart2p/mandos.txt',
    'cart2p/opciones.txt', 'cart2p/docs/BITACORA.txt', '@ CONTEXTO PARA CLAUDE.md',
    'Cart Chase 2P.lnk', '.claude/skills/cart-chase-2p-*/*', '.vscode/*',
    'Cart-Chase-2P.code-workspace', 'CLAUDE.md', 'cart2p/docs/LIBERAR_PROYECTO.txt',
    # notas internas de desarrollo y el catalogo (se genera, en espanol)
    'cart2p/docs/LEEME_WORKSPACE.txt', 'cart2p/docs/PASO_A_PASO.txt',
    'cart2p/docs/PLAN_CART_CHASE_2P.txt', 'cart2p/etapas_fuente/CATALOGO_ETAPAS.txt',
    'cart2p/publico_en/*',
    # el motor lo baja JUGAR.bat (no hay submodulo en el repo publico)
    '.gitmodules',
    # nunca ejecutables ni binarios compilados
    '*.exe', '*.dll', '*.lib', '*.obj', '*.pdb', '*.ilk', '*.exp',
]


# Version en ingles de un documento publico: cart2p/publico_en/<ruta>
# reemplaza a <ruta> en el paquete (el original en espanol queda local).
PUBLICO_EN = os.path.join(ROOT, 'cart2p', 'publico_en')


def rel(p):
    return os.path.relpath(p, ROOT).replace(os.sep, '/')


def origen(r):
    en = os.path.join(PUBLICO_EN, *r.split('/'))
    return en if os.path.isfile(en) else os.path.join(ROOT, *r.split('/'))


def coincide(r, pats):
    return any(fnmatch.fnmatch(r, p) for p in pats)


def lista():
    out = set()
    try:
        git = subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT, capture_output=True, check=True)
        for r in git.stdout.decode('utf-8').split('\0'):
            if r and os.path.isfile(os.path.join(ROOT, r)):
                out.add(r)
    except (OSError, subprocess.CalledProcessError):
        sys.exit('hace falta git para listar el codigo del motor')
    for base, _, files in os.walk(ROOT):
        for f in files:
            r = rel(os.path.join(base, f))
            if coincide(r, INCLUIR):
                out.add(r)
    return sorted(r for r in out if not coincide(r, EXCLUIR))


def indice_rom(rom):
    idx = set()
    for o in range(0, len(rom) - VENTANA + 1, PASO):
        w = rom[o:o + VENTANA]
        if len(set(w)) >= 8:  # sin relleno ni rampas triviales
            idx.add(w)
    return idx


def escanear(archivos, rom_path, base=None):
    rom = open(rom_path, 'rb').read()
    idx = indice_rom(rom)
    print('indice de la ROM: %d ventanas de %d bytes' % (len(idx), VENTANA))
    fugas = []
    for r in archivos:
        d = open(os.path.join(base, r) if base else origen(r), 'rb').read()
        if len(d) < VENTANA:
            continue
        for o in range(len(d) - VENTANA + 1):
            if d[o:o + VENTANA] in idx:
                fugas.append((r, o))
                break
    return fugas


def datos_privados(rutas):
    """Usuario de Windows, nombre del equipo y carpeta personal de quien arma
    el paquete (mas CART2P_PRIVADO="a;b;c"), en UTF-8 y UTF-16."""
    import getpass
    import socket
    terminos = {getpass.getuser(), socket.gethostname(), os.path.expanduser('~')}
    terminos |= set(os.environ.get('CART2P_PRIVADO', '').split(';'))
    terminos = [t.lower() for t in terminos if len(t) >= 3]
    hallados = []
    for p in rutas:
        d = open(p, 'rb').read()
        texto = (d.decode('utf-8', 'ignore') + d.decode('utf-16-le', 'ignore')).lower()
        for t in terminos:
            if t in texto:
                hallados.append((p, t))
                break
    return hallados


GITATTRIBUTES = ('* text=auto\n*.bat text eol=crlf\n*.cmd text eol=crlf\n*.ps1 text eol=crlf\n'
                 '*.sh text eol=lf\n*.png binary\n*.ico binary\n*.icns binary\n')


def carpeta_github(archivos, destino, rom_path):
    """Arbol listo para 'git init': README.md del mod en la raiz (el del
    motor pasa a docs/DKC1RECOMP_README.md), .gitattributes, sin zip."""
    import shutil
    if os.path.exists(destino):
        sys.exit('ya existe %s (no se sobrescribe)' % destino)
    renombres = {'README.md': 'docs/DKC1RECOMP_README.md',
                 'cart2p/README_GITHUB.md': 'README.md'}
    for r in archivos:
        dst = os.path.join(destino, *renombres.get(r, r).split('/'))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(origen(r), dst)
    with open(os.path.join(destino, '.gitattributes'), 'w', newline='\n') as f:
        f.write(GITATTRIBUTES)
    # segunda pasada sobre lo copiado: sin ejecutables y sin datos de la ROM
    final = []
    for base, _, files in os.walk(destino):
        for f in files:
            p = os.path.join(base, f)
            if open(p, 'rb').read(2) == b'MZ':
                sys.exit('ejecutable en la carpeta: %s' % p)
            final.append(p)
    fugas = escanear([os.path.relpath(p, destino) for p in final], rom_path, destino)
    if fugas:
        sys.exit('CARPETA CON DATOS DE LA ROM: %s' % fugas)
    privados = datos_privados(final)
    if privados:
        for p, t in privados:
            print('  PRIVADO: %s (%s)' % (os.path.relpath(p, destino), t))
        sys.exit('CARPETA CON DATOS PERSONALES: %d archivos' % len(privados))
    total = sum(os.path.getsize(p) for p in final)
    print('carpeta: %s (%d archivos, %.1f MB, 0 ejecutables, 0 datos de la ROM)'
          % (destino, len(final), total / 1e6))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--salida', help='carpeta donde dejar el zip')
    ap.add_argument('--rom', default=os.path.join(ROOT, 'rom', 'dkc1.sfc'))
    ap.add_argument('--solo-escanear', action='store_true')
    ap.add_argument('--carpeta', help='arbol listo para GitHub en vez de zip (carpeta nueva)')
    a = ap.parse_args()
    archivos = lista()
    total = sum(os.path.getsize(origen(r)) for r in archivos)
    print('archivos: %d (%.1f MB)' % (len(archivos), total / 1e6))
    if not os.path.isfile(a.rom):
        sys.exit('hace falta la ROM para escanear (no se incluye): %s' % a.rom)
    fugas = escanear(archivos, a.rom)
    if fugas:
        for r, o in fugas:
            print('  FUGA: %s (byte %d coincide con la ROM)' % (r, o))
        sys.exit('PAQUETE DETENIDO: %d archivos traen datos de la ROM' % len(fugas))
    print('escaneo: 0 archivos con datos de la ROM')
    if a.carpeta:
        return carpeta_github(archivos, a.carpeta, a.rom)
    if a.solo_escanear or not a.salida:
        return 0
    os.makedirs(a.salida, exist_ok=True)
    zpath = os.path.join(a.salida, 'CartChase2P-fuente.zip')
    if os.path.exists(zpath):
        sys.exit('ya existe %s (no se sobrescribe)' % zpath)
    man = []
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for r in archivos:
            p = origen(r)
            z.write(p, 'CartChase2P/' + r)
            man.append('%s  %s' % (hashlib.sha256(open(p, 'rb').read()).hexdigest(), r))
        z.writestr('CartChase2P/MANIFIESTO_SHA256.txt', '\r\n'.join(man) + '\r\n')
    print('zip: %s (%.1f MB)' % (zpath, os.path.getsize(zpath) / 1e6))
    return 0


if __name__ == '__main__':
    sys.exit(main())
