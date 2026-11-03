#!/usr/bin/env python3
"""Cart Chase 2P - verifica la ROM del usuario antes de compilar o preparar.

La unica ROM aceptada es Donkey Kong Country (USA) v1.0 sin cabecera:
  4 194 304 bytes, SHA-256
  fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15

Si la ROM trae la cabecera de 512 bytes de los copiadores (.smc) y sin ella
es la correcta, escribe la copia limpia en rom\\dkc1.sfc (nunca sobrescribe).
Si es otra version, dice cual segun la cabecera interna del cartucho
(region y numero de version en $FFD9/$FFDB).

Uso:
  python tools\\cart2p\\verificar_rom.py                 (rom\\dkc1.sfc o lo que haya en rom\\)
  python tools\\cart2p\\verificar_rom.py <archivo>
Devuelve 0 si la ROM sirve, 1 si no.
"""
import glob
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
DESTINO = os.path.join(ROOT, 'rom', 'dkc1.sfc')
SHA256 = 'fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15'
SIZE = 0x400000
HEADER = 512
REGIONES = {0: 'Japon', 1: 'USA', 2: 'Europa'}


def describir(data):
    """Titulo, region y version de la cabecera interna HiROM ($FFC0)."""
    if len(data) < 0x10000:
        return 'archivo demasiado chico'
    h = data[0xFFC0:0xFFE0]
    titulo = h[:21].decode('ascii', 'replace').strip()
    region = REGIONES.get(h[0x19], 'region %02X' % h[0x19])
    return '"%s", %s, version 1.%d' % (titulo, region, h[0x1B])


def revisar(path):
    """(ok, mensaje, datos limpios o None)."""
    data = open(path, 'rb').read()
    candidatos = [data]
    if len(data) == SIZE + HEADER:
        candidatos.append(data[HEADER:])
    for i, d in enumerate(candidatos):
        if len(d) == SIZE and hashlib.sha256(d).hexdigest() == SHA256:
            return True, ('correcta' if i == 0 else 'correcta tras quitar la cabecera de 512 bytes'), d
    d = candidatos[-1]
    desc = describir(d)
    if desc.endswith('USA, version 1.0'):
        desc += ', pero alterada o mal copiada'
    return False, ('no es Donkey Kong Country USA v1.0 (es %s; %d bytes, SHA-256 %s)'
                   % (desc, len(data), hashlib.sha256(data).hexdigest())), None


def main():
    if len(sys.argv) > 1:
        paths = [sys.argv[1]]
    elif os.path.exists(DESTINO):
        paths = [DESTINO]
    else:
        paths = sorted(p for e in ('*.sfc', '*.smc', '*.SFC', '*.SMC')
                       for p in glob.glob(os.path.join(ROOT, 'rom', e)))
        paths = list(dict.fromkeys(paths))
    if not paths:
        print('ROM: falta. Copiar Donkey Kong Country (USA) v1.0 a rom\\dkc1.sfc')
        return 1
    for p in paths:
        ok, msg, limpio = revisar(p)
        print('ROM %s: %s' % (os.path.basename(p), msg))
        if not ok:
            continue
        if os.path.normcase(os.path.abspath(p)) == os.path.normcase(DESTINO) and limpio is not None \
                and os.path.getsize(p) == SIZE:
            return 0
        if os.path.exists(DESTINO):
            if os.path.getsize(DESTINO) == SIZE and revisar(DESTINO)[0]:
                print('ROM: rom\\dkc1.sfc ya es la copia verificada')
                return 0
            print('ROM: rom\\dkc1.sfc ya existe (con cabecera o de otra version) y no se '
                  'sobrescribe. Renombrarla (por ejemplo a rom\\original.smc) y volver a correr.')
            return 1
        with open(DESTINO, 'wb') as f:
            f.write(limpio)
        print('ROM: copia verificada escrita en rom\\dkc1.sfc')
        return 0
    print('Solo sirve Donkey Kong Country (USA) v1.0 (SHA-256 %s).\n'
          'v1.1, v1.2, Europa y Japon no sirven: el juego esta recompilado desde la v1.0.' % SHA256)
    return 1


if __name__ == '__main__':
    sys.exit(main())
