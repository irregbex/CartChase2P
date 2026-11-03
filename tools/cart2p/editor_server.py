#!/usr/bin/env python3
"""Cart Chase 2P - servidor local del editor de etapas.

Sirve cart2p\\editor\\ (editor.html) en http://127.0.0.1:8765/ y una API que
lee y escribe las recetas de cart2p\\etapas_fuente\\ y compila con
etapa_lib.py. Solo escucha en la propia maquina (127.0.0.1).

API (JSON):
  GET  /api/niveles            datos de los niveles (cart2p\\editor\\datos\\*.json)
  GET  /api/etapas             lista de recetas (id, nombre, nivel, familia, ...)
  GET  /api/etapa?id=X         una receta
  POST /api/validar            {receta} -> {errores, avisos, estadisticas, notas}
  POST /api/guardar            {receta, nueva} -> guarda (respaldando la version
                               anterior en etapas_fuente\\historial\\), compila el
                               .c2p, la agrega al menu y rehace el catalogo
  POST /api/probar             {id} -> abre el juego directo en esa etapa
  POST /api/menu               {ids:[...]} -> reordena el menu (etapas.txt):
                               primero las oficiales, luego estas en ese orden

Uso: python tools\\cart2p\\editor_server.py [--puerto 8765] [--sin-navegador]
"""
import argparse
import datetime
import glob
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import urllib.parse
import webbrowser

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import etapa_lib as L  # noqa: E402
import compilar_etapas as C  # noqa: E402

EDITOR = os.path.join(L.ROOT, 'cart2p', 'editor')
HIST = os.path.join(L.FUENTE_DIR, 'historial')
EXE = os.path.join(L.ROOT, 'build-windows', 'release', 'DKC1Recomp.exe')
ID_RE = re.compile(r'^[a-z0-9_]{3,40}$')
LOCK = threading.Lock()


def lista_recetas():
    out = []
    menu = leer_menu()
    for p in sorted(glob.glob(os.path.join(L.FUENTE_DIR, '*.json'))):
        try:
            r = L.cargar_receta(p)
        except (OSError, ValueError):
            continue
        st = r.get('estadisticas', {})
        out.append({'id': r.get('id'), 'nombre': r.get('nombre'), 'nivel': r.get('nivel'),
                    'familia': r.get('familia', ''), 'dificultad': r.get('dificultad', ''),
                    'columnas': st.get('columnas'), 'segundos': st.get('segundos_aprox'),
                    'hoyos': st.get('hoyos_inventados'), 'enemigos': st.get('enemigos'),
                    'modificada': r.get('modificada', r.get('creada', '')),
                    'en_menu': r.get('id') in menu})
    return out


def leer_menu():
    ids = []
    with open(L.LISTA, encoding='utf-8') as f:
        for line in f:
            if line.strip().startswith('#') or '|' not in line:
                continue
            m = re.search(r'stages\\([^|]+)\.c2p', line)
            if m:
                ids.append(m.group(1).strip())
    return ids


def reordenar_menu(ids):
    with open(L.LISTA, encoding='utf-8') as f:
        lines = f.read().splitlines()
    cabecera = [l for l in lines if not l.strip() or l.strip().startswith('#')]
    entradas = [l for l in lines if l.strip() and not l.strip().startswith('#')]
    oficiales = [l for l in entradas if not re.search(r'stages\\', l)]
    por_id = {}
    for l in entradas:
        m = re.search(r'stages\\([^|]+)\.c2p', l)
        if m:
            por_id[m.group(1).strip()] = l
    nuevas = [por_id.pop(i) for i in ids if i in por_id]
    resto = list(por_id.values())
    with open(L.LISTA, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(cabecera + oficiales + nuevas + resto) + '\n')


def validar(receta):
    nv = L.nivel(receta['nivel'])
    errs = L.validar(receta, nv)
    avisos = [e for e in errs if e.startswith('demasiados objetos')]
    errores = [e for e in errs if e not in avisos]
    st = notas = None
    if not errores:
        r = json.loads(json.dumps(receta))
        L.documentar(r, nv)
        st = r['estadisticas']
        notas = [p.get('nota', '') for p in r['piezas']]
    return {'errores': errores, 'avisos': avisos, 'estadisticas': st, 'notas': notas}


def guardar(body):
    receta = body['receta']
    ident = receta.get('id', '')
    if not ID_RE.match(ident):
        return 400, {'error': 'id invalido: minusculas, numeros y _ (3 a 40)'}
    path = os.path.join(L.FUENTE_DIR, ident + '.json')
    if body.get('nueva') and os.path.exists(path):
        return 409, {'error': 'ya existe una etapa con id %s' % ident}
    v = validar(receta)
    if v['errores']:
        return 400, {'error': 'la etapa tiene errores', **v}
    hoy = datetime.datetime.now().strftime('%Y-%m-%d %H:%M')
    if os.path.exists(path):
        os.makedirs(HIST, exist_ok=True)
        stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
        shutil.copy2(path, os.path.join(HIST, '%s_%s.json' % (ident, stamp)))
    receta.setdefault('formato', 1)
    receta.setdefault('creada', hoy[:10])
    receta['modificada'] = hoy
    receta.setdefault('historial', [])
    if body.get('cambio'):
        receta['historial'].append('%s %s' % (hoy, body['cambio']))
    errs, out = L.compilar_receta(receta)
    L.guardar_receta(receta, path)
    L.asegurar_en_lista(receta)
    recetas = [L.cargar_receta(p) for p in sorted(glob.glob(os.path.join(L.FUENTE_DIR, '*.json')))]
    with open(os.path.join(L.FUENTE_DIR, 'CATALOGO_ETAPAS.txt'), 'w', encoding='utf-8',
              newline='\r\n') as f:
        f.write(C.catalogo(recetas))
    return 200, {'ok': True, 'receta': receta, 'c2p': out, **v}


def probar(body):
    ident = body.get('id', '')
    c2p = os.path.join(L.STAGES_DIR, ident + '.c2p')
    path = os.path.join(L.FUENTE_DIR, ident + '.json')
    if not ID_RE.match(ident) or not os.path.exists(c2p) or not os.path.exists(path):
        return 404, {'error': 'guardar la etapa antes de probarla'}
    if not os.path.exists(EXE):
        return 500, {'error': 'falta %s: compilar el juego (JUGAR.bat opcion 4)' % EXE}
    r = L.cargar_receta(path)
    logs = os.path.join(L.ROOT, 'cart2p', 'logs')
    os.makedirs(logs, exist_ok=True)
    env = dict(os.environ)
    for k in list(env):
        if k.startswith('DKC1_'):
            del env[k]
    env.update({
        'DKC1_CART2P': '2', 'DKC1_CART2P_STAGE': c2p, 'DKC1_CART2P_DIRECTO': '1',
        'DKC1_SAVESTATE_INPUT': os.path.join(L.ROOT, 'cart2p', L.NIVELES[r['nivel']]['estado']),
        'DKC1_CART2P_RECORDS': os.path.join(L.ROOT, 'cart2p', 'records.txt'),
        'DKC1_CART2P_MANDOS': os.path.join(L.ROOT, 'cart2p', 'mandos.txt'),
        'DKC1_CART2P_LOGFILE': os.path.join(logs, 'prueba_editor.txt'),
        'DKC1_WIDESCREEN': '0'})
    subprocess.Popen([EXE, L.ROM_PATH], env=env, cwd=L.ROOT)
    return 200, {'ok': True}


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=EDITOR, **k)

    def log_message(self, fmt, *args):
        pass

    def send_json(self, code, obj):
        data = json.dumps(obj, ensure_ascii=False).encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        try:
            if u.path == '/api/niveles':
                out = {}
                for n in L.NIVELES:
                    with open(os.path.join(EDITOR, 'datos', n + '.json'), encoding='utf-8') as f:
                        out[n] = json.load(f)
                return self.send_json(200, out)
            if u.path == '/api/etapas':
                return self.send_json(200, {'etapas': lista_recetas(), 'menu': leer_menu()})
            if u.path == '/api/etapa':
                ident = q.get('id', [''])[0]
                path = os.path.join(L.FUENTE_DIR, ident + '.json')
                if not ID_RE.match(ident) or not os.path.exists(path):
                    return self.send_json(404, {'error': 'no existe %s' % ident})
                return self.send_json(200, L.cargar_receta(path))
        except Exception as e:  # noqa: BLE001 - el editor muestra el error
            return self.send_json(500, {'error': str(e)})
        if u.path == '/':
            self.path = '/editor.html'
        return super().do_GET()

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        try:
            n = int(self.headers.get('Content-Length', 0))
            body = json.loads(self.rfile.read(n).decode('utf-8') or '{}')
            with LOCK:
                if u.path == '/api/validar':
                    return self.send_json(200, validar(body['receta']))
                if u.path == '/api/guardar':
                    return self.send_json(*guardar(body))
                if u.path == '/api/probar':
                    return self.send_json(*probar(body))
                if u.path == '/api/menu':
                    reordenar_menu(body.get('ids', []))
                    return self.send_json(200, {'ok': True, 'menu': leer_menu()})
        except Exception as e:  # noqa: BLE001
            return self.send_json(500, {'error': str(e)})
        return self.send_json(404, {'error': 'ruta desconocida'})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--puerto', type=int, default=8765)
    ap.add_argument('--sin-navegador', action='store_true')
    args = ap.parse_args()
    if not os.path.exists(os.path.join(EDITOR, 'datos', 'carnage.json')):
        print('Preparando los datos del editor (una vez)...')
        subprocess.run([sys.executable, '-I', os.path.join(os.path.dirname(__file__),
                                                          'exportar_editor.py')], check=True)
    srv = http.server.ThreadingHTTPServer(('127.0.0.1', args.puerto), Handler)
    url = 'http://127.0.0.1:%d/' % args.puerto
    print('Editor de etapas en %s  (cerrar esta ventana para terminar)' % url)
    if not args.sin_navegador:
        webbrowser.open(url)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == '__main__':
    sys.exit(main())
