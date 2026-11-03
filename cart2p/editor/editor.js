/* Cart Chase 2P - editor de etapas.
 *
 * Una etapa (receta) es una lista de piezas del nivel original:
 *   {tipo:'tramo', desde:a, hasta:b}      columnas a..b-1 con sus objetos
 *   {tipo:'recta', columna:c, largo:n}     columna plana c repetida n veces
 *   {tipo:'hoyo'|'doble'|'columnas', columnas:[...]}  columnas sueltas
 * mas objetos quitados/agregados. Formato completo: cart2p\docs\FORMATO_ETAPAS.txt.
 * Las reglas (via que calza, primera/ultima pieza, limites) son las mismas
 * de tools\cart2p\etapa_lib.py; el servidor vuelve a validar al guardar.
 * Coordenadas: X en px desde el inicio de la etapa (32 px por columna);
 * Y del juego crece hacia arriba: fila en pantalla = $200 - Y.
 */
'use strict';

const COL = 32;
const ALTO = 512;
const INICIO_KONG = 0x3A6;          // X de los Kongs en el estado inicial
const $ = (id) => document.getElementById(id);

const S = {
  niveles: {},          // datos de cart2p/editor/datos/<nivel>.json
  imgs: {},             // imagen del nivel completo
  etapas: [],
  receta: null,
  guardada: '',         // JSON de la ultima version guardada
  nueva: false,
  sel: 0,               // pieza elegida
  deshacer: [],
  rehacer: [],
  zoom: 0.6,
  x0: 0,                // px de la etapa en el borde izquierdo de la vista
  plantilla: null,      // modo agregar objeto
  arrastre: null,       // objeto agregado que se arrastra
  validacion: null,     // ultima respuesta del servidor
  timerVal: 0,
};

// ------------------------------------------------------------------ util

function hx(v) { return '$' + (v >>> 0).toString(16).toUpperCase().padStart(4, '0'); }
function clon(o) { return JSON.parse(JSON.stringify(o)); }
function nivel() { return S.receta ? S.niveles[S.receta.nivel] : null; }

function toast(msg, mal) {
  const t = $('toast');
  t.textContent = msg;
  t.classList.toggle('mal', !!mal);
  t.classList.remove('oculta');
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => t.classList.add('oculta'), mal ? 6000 : 2600);
}

async function api(ruta, cuerpo) {
  const r = await fetch(ruta, cuerpo === undefined ? {} : {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(cuerpo),
  });
  const j = await r.json();
  if (!r.ok) { const e = new Error(j.error || r.statusText); e.datos = j; throw e; }
  return j;
}

// ------------------------------------------------------------------ modelo

function columnasDe(p) {
  if (p.tipo === 'tramo') { const o = []; for (let c = p.desde; c < p.hasta; c++) o.push(c); return o; }
  if (p.tipo === 'recta') return Array(Math.max(0, p.largo | 0)).fill(p.columna | 0);
  return (p.columnas || []).map((c) => c | 0);
}

function disposicion() {
  const piezas = [];
  const src = [];
  let pos = 0;
  S.receta.piezas.forEach((p, i) => {
    const cols = columnasDe(p);
    piezas.push({ i, p, inicio: pos, cols });
    for (const c of cols) src.push(c);
    pos += cols.length;
  });
  return { piezas, src, total: pos };
}

function borde(c, lado) {
  const b = nivel().bordes[c];
  return b ? b[lado] : null;
}

function piezaEn(lay, x) {
  const c = Math.floor(x / COL);
  for (const L of lay.piezas) if (c >= L.inicio && c < L.inicio + L.cols.length) return L;
  return lay.piezas[lay.piezas.length - 1];
}

// corte visible: la columna que seguiria en el original no es identica a la
// siguiente (misma regla que etapa_lib.corte_visible)
function corteVisible(lay, k) {
  const a = lay.piezas[k], b = lay.piezas[k + 1];
  if (!a || !b || !a.cols.length || !b.cols.length) return false;
  const nat = a.cols[a.cols.length - 1] + 1;
  if (nat === b.cols[0]) return false;
  const f = nivel().firma;
  return !f || nat >= f.length || f[nat] !== f[b.cols[0]];
}

function objetosDe(lay) {
  // mismos criterios que etapa_lib.resolver_objetos
  const nv = nivel();
  const quitar = new Set((S.receta.objetos_quitar || []).map((q) => q.pieza + ':' + q.indice));
  const vistos = new Set();
  const lista = [];
  let salida = null;
  for (const L of lay.piezas) {
    if (L.p.tipo !== 'tramo') continue;
    for (const o of nv.objetos) {
      if (o.x < L.p.desde * COL || o.x >= L.p.hasta * COL || vistos.has(o.indice)) continue;
      vistos.add(o.indice);
      const x = o.x - L.p.desde * COL + L.inicio * COL;
      const e = { ...o, x, pieza: L.i, origen: 'nivel', quitado: quitar.has(L.i + ':' + o.indice) };
      if (o.tipo === 2) salida = e; else lista.push(e);
    }
  }
  (S.receta.objetos_agregar || []).forEach((a, n) => {
    const L = lay.piezas[a.pieza];
    if (!L) return;
    lista.push({ nombre: a.nombre, puntero: a.puntero, x: L.inicio * COL + a.dx, y: a.y, pieza: a.pieza,
      origen: 'agregado', agregado: n, enemigo: !!a.enemigo, quitado: false });
  });
  lista.sort((a, b) => a.x - b.x);
  const activos = lista.filter((o) => !o.quitado);
  return { lista, activos, salida, limite: nv.limite_objetos };
}

function platanosDe(lay) {
  const nv = nivel();
  const vistos = new Set();
  const out = [];
  for (const L of lay.piezas) {
    if (L.p.tipo !== 'tramo') continue;
    for (const g of nv.platanos) {
      if (g.x < L.p.desde * COL || g.x >= L.p.hasta * COL || vistos.has(g.indice)) continue;
      vistos.add(g.indice);
      out.push({ ...g, x: g.x - L.p.desde * COL + L.inicio * COL });
    }
  }
  return out;
}

function revisar(lay) {
  // reglas de etapa_lib.validar (el servidor tiene la ultima palabra)
  const nv = nivel();
  const errores = [];
  const malas = new Set();
  const ps = S.receta.piezas;
  if (!ps.length) return { errores: ['la etapa no tiene piezas'], malas };
  const f = ps[0], u = ps[ps.length - 1];
  if (f.tipo !== 'tramo' || f.desde !== 0 || f.hasta < nv.primer_corte_min)
    errores.push('La primera pieza debe ser el tramo de partida (desde la columna 0).');
  if (u.tipo !== 'tramo' || u.hasta !== nv.columnas || u.desde * COL > nv.salida_x)
    errores.push('La ultima pieza debe ser el tramo final del nivel (con la puerta de salida).');
  if (lay.total > nv.max_columnas)
    errores.push(`Etapa de ${lay.total} columnas: el maximo es ${nv.max_columnas}.`);
  for (const L of lay.piezas)
    if (!L.cols.length) errores.push(`La pieza ${L.i + 1} esta vacia.`);
    else if (L.cols.some((c) => c < 0 || c >= nv.columnas)) errores.push(`La pieza ${L.i + 1} usa columnas fuera del nivel.`);
  for (let k = 0; k + 1 < lay.piezas.length; k++) {
    const a = lay.piezas[k], b = lay.piezas[k + 1];
    if (!a.cols.length || !b.cols.length) continue;
    const ya = borde(a.cols[a.cols.length - 1], 1), yb = borde(b.cols[0], 0);
    if (ya == null || yb == null || Math.abs(ya - yb) > 2) {
      malas.add(k);
      errores.push(`Union ${k + 1} -> ${k + 2}: la via sale a ${ya == null ? 'sin via' : hx(ya)} y entra a ${yb == null ? 'sin via' : hx(yb)}.`);
    }
  }
  return { errores, malas };
}

// ------------------------------------------------------------------ cambios

function cambiar(fn, conservarSel) {
  S.deshacer.push(JSON.stringify(S.receta));
  if (S.deshacer.length > 200) S.deshacer.shift();
  S.rehacer.length = 0;
  fn(S.receta);
  if (!conservarSel) S.sel = Math.max(0, Math.min(S.sel, S.receta.piezas.length - 1));
  todo();
}

function deshacer() {
  if (!S.deshacer.length) return;
  S.rehacer.push(JSON.stringify(S.receta));
  S.receta = JSON.parse(S.deshacer.pop());
  S.sel = Math.min(S.sel, S.receta.piezas.length - 1);
  todo();
}

function rehacer() {
  if (!S.rehacer.length) return;
  S.deshacer.push(JSON.stringify(S.receta));
  S.receta = JSON.parse(S.rehacer.pop());
  S.sel = Math.min(S.sel, S.receta.piezas.length - 1);
  todo();
}

function insertarPieza(p) {
  const n = S.receta.piezas.length;
  const donde = Math.min(S.sel + 1, n - 1);  // nunca despues de la ultima
  cambiar((r) => {
    r.piezas.splice(donde, 0, p);
    moverIndices(r, donde, +1);
  });
  S.sel = donde;
  todo();
}

function moverIndices(r, desde, delta) {
  // objetos quitados/agregados apuntan a indices de pieza
  for (const lst of [r.objetos_quitar || [], r.objetos_agregar || []])
    for (const q of lst) if (q.pieza >= desde) q.pieza += delta;
}

function borrarPieza(i) {
  const n = S.receta.piezas.length;
  if (i <= 0 || i >= n - 1) { toast('La primera y la ultima pieza no se pueden borrar.', true); return; }
  cambiar((r) => {
    r.piezas.splice(i, 1);
    r.objetos_quitar = (r.objetos_quitar || []).filter((q) => q.pieza !== i);
    r.objetos_agregar = (r.objetos_agregar || []).filter((q) => q.pieza !== i);
    moverIndices(r, i + 1, -1);
  });
}

function moverPieza(i, j) {
  const n = S.receta.piezas.length;
  if (i <= 0 || i >= n - 1 || j <= 0 || j >= n - 1 || i === j) return;
  cambiar((r) => {
    const mapa = [...Array(n).keys()];
    const [p] = r.piezas.splice(i, 1);
    r.piezas.splice(j, 0, p);
    const [m] = mapa.splice(i, 1);
    mapa.splice(j, 0, m);
    const nuevoIdx = {};
    mapa.forEach((viejo, nuevo) => { nuevoIdx[viejo] = nuevo; });
    for (const lst of [r.objetos_quitar || [], r.objetos_agregar || []])
      for (const q of lst) q.pieza = nuevoIdx[q.pieza];
  });
  S.sel = j;
  todo();
}

function duplicarPieza(i) {
  const n = S.receta.piezas.length;
  if (i <= 0 || i >= n - 1) { toast('La primera y la ultima pieza no se duplican.', true); return; }
  const p = clon(S.receta.piezas[i]);
  delete p.nota;
  cambiar((r) => { r.piezas.splice(i + 1, 0, p); moverIndices(r, i + 1, +1); });
  S.sel = i + 1;
  todo();
}

// ------------------------------------------------------------------ dibujo pista

const pista = $('pista');
const ctx = pista.getContext('2d');

function tamPista() {
  const marco = $('pista-marco');
  const w = marco.clientWidth;
  const h = Math.round(ALTO * S.zoom);
  if (pista.width !== w || pista.height !== h) { pista.width = w; pista.height = h; }
  marco.style.height = h + 'px';
}

const ABREV = {
  ZINGER: 'Z', NECKY: 'N', GNAWTY: 'G', CARRO: 'C', 'LETRA KONG': 'K', RACIMO: 'R',
  'BARRIL DK': 'DK', CANON: 'CA', COFRE: 'CF', FICHA: 'F', 'BARRIL CONTROL': 'BC', GLOBO: 'GL',
  SENAL: 'S', NEUMATICO: 'NE', 'MEDIO NEUMATICO': 'NE', CUERDA: 'CU', SALIDA: 'META',
};
function colorObj(o) {
  if (o.enemigo || ['ZINGER', 'NECKY', 'GNAWTY', 'CARRO'].includes(o.nombre)) return '#ef5350';
  if (o.nombre === 'LETRA KONG') return '#f2b632';
  if (['BARRIL DK', 'CANON', 'BARRIL CONTROL'].includes(o.nombre)) return '#c08a52';
  return '#5aa9ff';
}

function dibujarPista() {
  if (!S.receta) return;
  tamPista();
  const nv = nivel(), img = S.imgs[S.receta.nivel], z = S.zoom;
  const lay = disposicion();
  const W = pista.width, H = pista.height;
  const maxX0 = Math.max(0, lay.total * COL - W / z);
  S.x0 = Math.max(0, Math.min(S.x0, maxX0));
  ctx.imageSmoothingEnabled = z < 1;
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, W, H);
  const c0 = Math.max(0, Math.floor(S.x0 / COL)), c1 = Math.min(lay.total, Math.ceil((S.x0 + W / z) / COL) + 1);
  const sx = (x) => (x - S.x0) * z;
  const sy = (y) => (0x200 - y) * z;
  if (img && img.complete) {
    for (let n = c0; n < c1; n++)
      ctx.drawImage(img, lay.src[n] * COL, 0, COL, ALTO, Math.floor(sx(n * COL)), 0, Math.ceil(COL * z) + 1, H);
  }
  const rev = revisar(lay);

  // zonas donde un enemigo impide hoyos
  const objs = objetosDe(lay);
  if ($('v-zonas').checked) {
    ctx.fillStyle = 'rgba(239,83,80,0.13)';
    for (const o of objs.activos) {
      const zona = o.zona_hoyos || (o.origen === 'agregado' && o.enemigo ? [0x40, 0x200] : null);
      if (!zona) continue;
      ctx.fillRect(sx(o.x - zona[0]), 0, (zona[0] + zona[1]) * z, H);
    }
  }

  // via medida por el juego
  if ($('v-via').checked) {
    ctx.strokeStyle = 'rgba(255,80,60,0.9)';
    ctx.lineWidth = Math.max(1, 2 * z);
    ctx.beginPath();
    let pen = false;
    for (let n = c0; n < c1; n++) {
      for (let k = 0; k < 4; k++) {
        const y = nv.perfil_cada_8px[lay.src[n] * 4 + k];
        const x = sx(n * COL + k * 8 + 4);
        if (y == null) { pen = false; continue; }
        if (!pen) { ctx.moveTo(x, sy(y)); pen = true; } else ctx.lineTo(x, sy(y));
      }
    }
    ctx.stroke();
  }

  // piezas
  if ($('v-piezas').checked) {
    ctx.font = `${Math.max(10, Math.round(12 * Math.min(1, z + 0.3)))}px Segoe UI, sans-serif`;
    for (const L of lay.piezas) {
      const xa = L.inicio * COL, xb = (L.inicio + L.cols.length) * COL;
      if (xb < S.x0 || xa > S.x0 + W / z) continue;
      const col = { tramo: '79,143,214', recta: '108,194,138', hoyo: '224,121,74', doble: '224,121,74' }[L.p.tipo] || '154,127,209';
      ctx.fillStyle = `rgba(${col},${L.i === S.sel ? 0.95 : 0.6})`;
      ctx.fillRect(sx(xa), 0, (xb - xa) * z, 16);
      ctx.fillStyle = '#fff';
      ctx.fillText(`${L.i + 1} ${L.p.tipo}`, sx(xa) + 4, 12);
      ctx.fillStyle = 'rgba(255,255,255,0.35)';
      ctx.fillRect(Math.round(sx(xa)), 0, 1, H);
      if (L.i === S.sel) {
        ctx.strokeStyle = '#f2b632';
        ctx.lineWidth = 2;
        ctx.strokeRect(sx(xa) + 1, 1, (xb - xa) * z - 2, H - 2);
      }
    }
    for (let k = 0; k + 1 < lay.piezas.length; k++) {
      if (rev.malas.has(k) || !corteVisible(lay, k)) continue;
      const x = sx(lay.piezas[k + 1].inicio * COL);
      ctx.fillStyle = 'rgba(255,169,77,0.95)';
      for (let y = 18; y < H; y += 10) ctx.fillRect(x - 1, y, 3, 5);
      ctx.fillText('CORTE', x + 5, 44);
    }
    for (const k of rev.malas) {
      const L = lay.piezas[k + 1];
      const x = sx(L.inicio * COL);
      ctx.fillStyle = 'rgba(239,83,80,0.9)';
      ctx.fillRect(x - 3, 0, 6, H);
      ctx.fillStyle = '#fff';
      ctx.fillText('NO CALZA', x + 5, 30);
    }
  }

  // partida y meta
  ctx.fillStyle = 'rgba(82,194,107,0.9)';
  ctx.fillRect(sx(INICIO_KONG), 18, 2, H - 18);
  ctx.fillText('PARTIDA', sx(INICIO_KONG) + 4, 30);
  if (objs.salida) {
    ctx.fillRect(sx(objs.salida.x), 18, 3, H - 18);
    ctx.fillStyle = '#9be4ac';
    ctx.fillText('META', sx(objs.salida.x) + 5, 30);
  }

  // platanos
  if ($('v-platanos').checked) {
    ctx.fillStyle = '#ffe14d';
    for (const g of platanosDe(lay)) {
      if (g.x < S.x0 - 20 || g.x > S.x0 + W / z + 20) continue;
      ctx.beginPath();
      ctx.arc(sx(g.x), sy(g.y), Math.max(2.5, 4 * z), 0, 7);
      ctx.fill();
    }
  }

  // objetos
  if ($('v-objetos').checked) {
    const r = Math.max(7, 11 * z);
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.font = `bold ${Math.max(8, Math.round(10 * Math.min(1.2, z + 0.4)))}px Segoe UI, sans-serif`;
    for (const o of objs.lista) {
      if (o.x < S.x0 - 30 || o.x > S.x0 + W / z + 30) continue;
      const x = sx(o.x), y = sy(o.y);
      ctx.globalAlpha = o.quitado ? 0.35 : 1;
      ctx.fillStyle = colorObj(o);
      ctx.beginPath();
      ctx.arc(x, y, r, 0, 7);
      ctx.fill();
      ctx.lineWidth = o.origen === 'agregado' ? 3 : 1.5;
      ctx.strokeStyle = o.origen === 'agregado' ? '#fff' : 'rgba(0,0,0,0.7)';
      ctx.stroke();
      ctx.fillStyle = '#111';
      ctx.fillText(ABREV[o.nombre] || '?', x, y + 0.5);
      if (o.quitado) {
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(x - r, y - r); ctx.lineTo(x + r, y + r);
        ctx.moveTo(x + r, y - r); ctx.lineTo(x - r, y + r);
        ctx.stroke();
      }
    }
    ctx.globalAlpha = 1;
    ctx.textAlign = 'start';
    ctx.textBaseline = 'alphabetic';
  }
  S._lay = lay;
  S._objs = objs;
  dibujarMinimapa(lay);
  const sc = $('scroll-pista');
  $('scroll-largo').style.width = Math.max(W, lay.total * COL * z) + 'px';
  if (Math.abs(sc.scrollLeft - S.x0 * z) > 1) { S._scrollPropio = true; sc.scrollLeft = S.x0 * z; }
}

const mini = $('minimapa');
function dibujarMinimapa(lay) {
  const img = S.imgs[S.receta.nivel];
  const w = mini.clientWidth, h = mini.clientHeight;
  if (mini.width !== w) mini.width = w;
  if (mini.height !== h) mini.height = h;
  const m = mini.getContext('2d');
  m.fillStyle = '#000';
  m.fillRect(0, 0, w, h);
  const esc = w / Math.max(1, lay.total * COL);
  if (img && img.complete)
    for (let n = 0; n < lay.total; n++)
      m.drawImage(img, lay.src[n] * COL, 64, COL, ALTO - 64, n * COL * esc, 0, COL * esc + 0.6, h);
  for (const L of lay.piezas) {
    if (L.p.tipo === 'tramo') continue;
    m.fillStyle = L.p.tipo === 'recta' ? 'rgba(108,194,138,0.5)' : 'rgba(224,121,74,0.7)';
    m.fillRect(L.inicio * COL * esc, h - 6, L.cols.length * COL * esc, 6);
  }
  m.strokeStyle = '#f2b632';
  m.lineWidth = 2;
  m.strokeRect(S.x0 * esc, 1, (pista.width / S.zoom) * esc, h - 2);
}

// ------------------------------------------------------------------ miniaturas

function miniatura(canvas, cols, nv) {
  const img = S.imgs[nv.nivel];
  const w = canvas.width = canvas.clientWidth || 112;
  const h = canvas.height = canvas.clientHeight || 44;
  const m = canvas.getContext('2d');
  m.fillStyle = '#000';
  m.fillRect(0, 0, w, h);
  if (!img || !img.complete || !cols.length) return;
  // franja vertical alrededor de la via
  const ys = cols.map((c) => nv.bordes[c][0]).filter((y) => y != null);
  const yv = ys.length ? ys.reduce((a, b) => a + b) / ys.length : 0x100;
  const fila = Math.max(0, Math.min(ALTO - 220, 0x200 - yv - 120));
  // ancho: toda la pieza en el ancho de la miniatura (se estira o comprime)
  const ex = w / (cols.length * COL);
  m.imageSmoothingEnabled = true;
  cols.forEach((c, n) => m.drawImage(img, c * COL, fila, COL, 220, n * COL * ex, 0, COL * ex + 0.6, h));
}

// ------------------------------------------------------------------ linea de piezas

function dibujarLinea() {
  const cont = $('linea-piezas');
  cont.innerHTML = '';
  if (!S.receta) return;
  const lay = disposicion();
  const rev = revisar(lay);
  const nv = nivel();
  const n = lay.piezas.length;
  lay.piezas.forEach((L) => {
    const d = document.createElement('div');
    const fija = L.i === 0 || L.i === n - 1;
    d.className = `carta ${L.p.tipo}${L.i === S.sel ? ' elegida' : ''}${fija ? ' fija' : ''}`;
    d.draggable = !fija;
    const yin = L.cols.length ? borde(L.cols[0], 0) : null;
    const yout = L.cols.length ? borde(L.cols[L.cols.length - 1], 1) : null;
    const malIn = rev.malas.has(L.i - 1), malOut = rev.malas.has(L.i);
    const titulo = L.p.tipo === 'tramo' ? `Tramo ${L.p.desde}-${L.p.hasta - 1}` :
      L.p.tipo === 'recta' ? `Recta x${L.p.largo}` : L.p.tipo === 'doble' ? 'Doble hoyo' :
        L.p.tipo === 'hoyo' ? 'Hoyo' : 'Columnas';
    d.innerHTML = `<div class="t">${L.i + 1}. ${titulo}${fija ? (L.i === 0 ? ' (partida)' : ' (meta)') : ''}</div>
      <canvas></canvas>
      <div class="a"><span class="${malIn ? 'mal' : ''}">${yin == null ? '-' : hx(yin)}</span> &rarr; <span class="${malOut ? 'mal' : ''}">${yout == null ? '-' : hx(yout)}</span></div>
      <div class="a">${L.cols.length} col &middot; X ${hx(L.inicio * COL)}</div>`;
    d.addEventListener('click', () => { S.sel = L.i; irAPieza(L); todo(); });
    d.addEventListener('dragstart', (e) => { e.dataTransfer.setData('text/plain', String(L.i)); d.classList.add('arrastrando'); });
    d.addEventListener('dragend', () => d.classList.remove('arrastrando'));
    d.addEventListener('dragover', (e) => { if (!fija || L.i === n - 1) { e.preventDefault(); d.classList.add('destino'); } });
    d.addEventListener('dragleave', () => d.classList.remove('destino'));
    d.addEventListener('drop', (e) => {
      e.preventDefault();
      d.classList.remove('destino');
      const de = parseInt(e.dataTransfer.getData('text/plain'), 10);
      let a = L.i;
      if (de < a) a -= 1;
      moverPieza(de, Math.max(1, Math.min(n - 2, a)));
    });
    cont.appendChild(d);
    requestAnimationFrame(() => miniatura(d.querySelector('canvas'), L.cols.slice(0, 40), nv));
  });
  const sel = cont.children[S.sel];
  if (sel) sel.scrollIntoView({ block: 'nearest', inline: 'nearest' });
}

function irAPieza(L) {
  const W = pista.width / S.zoom;
  const xa = L.inicio * COL, xb = (L.inicio + L.cols.length) * COL;
  if (xa < S.x0 || xb > S.x0 + W) S.x0 = Math.max(0, xa - W * 0.2);
}

// ------------------------------------------------------------------ paleta

function salidaElegida(lay) {
  const L = lay.piezas[Math.min(S.sel, lay.piezas.length - 2)];
  return L && L.cols.length ? { y: borde(L.cols[L.cols.length - 1], 1), col: L.cols[L.cols.length - 1] } : { y: null, col: 0 };
}

function opcion(cont, cols, titulo, detalle, boton, accion, calza) {
  const d = document.createElement('div');
  d.className = 'op' + (calza ? '' : ' no-calza');
  d.innerHTML = `<canvas></canvas><div><div class="t">${titulo}</div><div class="a">${detalle}</div></div>`;
  const b = document.createElement('button');
  b.textContent = boton;
  b.addEventListener('click', accion);
  d.appendChild(b);
  cont.appendChild(d);
  requestAnimationFrame(() => miniatura(d.querySelector('canvas'), cols, nivel()));
}

function dibujarPaleta() {
  const nv = nivel();
  for (const id of ['pal-tramos', 'pal-rectas', 'pal-hoyos', 'pal-sincorte']) $(id).innerHTML = '';
  if (!nv) return;
  const lay = disposicion();
  const sal = salidaElegida(lay);
  const solo = $('solo-compat').checked;
  // la pieza nueva debe entrar a la altura de salida de la elegida y salir a
  // la altura de entrada de la siguiente
  const sig = lay.piezas[Math.min(S.sel + 1, lay.piezas.length - 1)];
  const yNext = sig && sig.cols.length ? borde(sig.cols[0], 0) : null;
  const encaja = (y1, y2) => (y1 == null || y2 == null) ? false : Math.abs(y1 - y2) <= 2;
  const calza2 = (yin, yout) => (sal.y == null || encaja(yin, sal.y)) && (yNext == null || encaja(yout, yNext));
  const calza = (y) => calza2(y, y);

  nv.tramos.forEach((t, k) => {
    if (k === 0 || k === nv.tramos.length - 1) return;   // partida y meta son fijas
    const yin = borde(t.a, 0), yout = borde(t.b - 1, 1);
    const ok = calza2(yin, yout);
    if (solo && !ok) return;
    const objs = nv.objetos.filter((o) => o.x >= t.a * COL && o.x < t.b * COL);
    const ene = objs.filter((o) => o.enemigo).length;
    const usado = S.receta.piezas.some((p) => p.tipo === 'tramo' && p.desde === t.a && p.hasta === t.b);
    const cols = [];
    for (let c = t.a; c < t.b; c++) cols.push(c);
    opcion($('pal-tramos'), cols, `Tramo ${t.a}-${t.b - 1}${usado ? ' (ya usado)' : ''}`,
      `${t.b - t.a} col &middot; via <b>${hx(yin)}</b> &rarr; <b>${hx(yout)}</b> &middot; ${ene} enemigos, ${objs.length - ene} otros`,
      'Insertar', () => insertarPieza({ tipo: 'tramo', desde: t.a, hasta: t.b }), ok);
  });

  const largo = Math.max(1, Math.min(20, parseInt($('recta-largo').value, 10) || 4));
  Object.keys(nv.planas).map(Number).sort((a, b) => a - b).forEach((y) => {
    const ok = calza(y);
    if (solo && !ok) return;
    const cs = nv.planas[y];
    const c = cs.reduce((a, b) => (Math.abs(b - sal.col) < Math.abs(a - sal.col) ? b : a));
    opcion($('pal-rectas'), Array(largo).fill(c), `Recta a ${hx(y)}`,
      `${cs.length} columnas planas a esta altura &middot; usa la columna ${c}`,
      'Insertar', () => insertarPieza({ tipo: 'recta', columna: c, largo }), ok);
  });

  nv.bloques_hoyo.forEach((b) => {
    const ok = calza(b.y);
    if (solo && !ok) return;
    const cs = nv.planas[b.y] || [];
    const f = cs.length ? cs.reduce((a, c) => (Math.abs(c - b.cols[0]) < Math.abs(a - b.cols[0]) ? c : a)) : b.cols[0] - 1;
    const simple = [f, f, ...b.cols, f, f];
    const d = document.createElement('div');
    opcion($('pal-hoyos'), simple, `Hoyo de ${b.w} px a ${hx(b.y)}`,
      `borde del hoyo original en X ${hx(b.x)} &middot; ${simple.length} col`,
      'Insertar', () => insertarPieza({ tipo: 'hoyo', hoyos_px: [b.w], columnas: simple }), ok);
    const doble = [f, f, ...b.cols, f, f, f, f, f, ...b.cols, f, f];
    opcion($('pal-hoyos'), doble, `Doble hoyo ${b.w}+${b.w} px a ${hx(b.y)}`,
      `dos veces el mismo borde con 5 columnas entre medio &middot; ${doble.length} col`,
      'Insertar', () => insertarPieza({ tipo: 'doble', hoyos_px: [b.w, b.w], columnas: doble }), ok);
  });
  // sin corte: seguir en una columna identica a la que seguiria
  {
    const Lsel = lay.piezas[Math.min(S.sel, lay.piezas.length - 2)];
    const nat = Lsel && Lsel.cols.length ? Lsel.cols[Lsel.cols.length - 1] + 1 : 0;
    const largo = Math.max(4, Math.min(200, parseInt($('sincorte-largo').value, 10) || 32));
    const destinos = [...(nv.saltos[String(nat)] || [])];
    if (nat < nv.columnas) destinos.push(nat);
    for (const t of destinos) {
      const hasta = Math.min(nv.columnas, t + largo);
      const yin = borde(t, 0), yout = borde(hasta - 1, 1);
      const ok = calza2(yin, yout);
      if (solo && !ok) continue;
      const cols = [];
      for (let c = t; c < hasta; c++) cols.push(c);
      opcion($('pal-sincorte'), cols, `${t === nat ? 'Seguir' : 'Salto a'} ${t}-${hasta - 1}`,
        `sin corte visible &middot; ${hasta - t} col &middot; via <b>${hx(yin)}</b> &rarr; <b>${hx(yout)}</b>`,
        'Insertar', () => insertarPieza({ tipo: 'tramo', desde: t, hasta }), ok);
    }
  }
  for (const id of ['pal-tramos', 'pal-rectas', 'pal-hoyos', 'pal-sincorte'])
    if (!$(id).children.length) $(id).innerHTML = '<p class="ayuda-corta">Ninguna calza a la altura de salida de la pieza elegida.</p>';
}

// ------------------------------------------------------------------ inspector

function dibujarInspector() {
  const t = $('t-pieza');
  t.innerHTML = '';
  if (!S.receta) return;
  const lay = disposicion();
  const L = lay.piezas[S.sel];
  if (!L) return;
  const nv = nivel();
  const n = lay.piezas.length;
  const fija = L.i === 0 || L.i === n - 1;
  const p = L.p;
  const yin = L.cols.length ? borde(L.cols[0], 0) : null;
  const yout = L.cols.length ? borde(L.cols[L.cols.length - 1], 1) : null;
  const nota = S.validacion && S.validacion.notas ? S.validacion.notas[L.i] : (p.nota || '');
  let h = `<h3>Pieza ${L.i + 1} de ${n}: ${p.tipo}${fija ? (L.i === 0 ? ' (partida, fija)' : ' (meta, fija)') : ''}</h3>
    <div class="stats"><span>Columnas</span><span>${L.cols.length} (${L.cols.length * COL} px)</span>
    <span>En la etapa</span><span>X ${hx(L.inicio * COL)} - ${hx((L.inicio + L.cols.length) * COL - 1)}</span>
    <span>Via entra / sale</span><span>${yin == null ? '-' : hx(yin)} / ${yout == null ? '-' : hx(yout)}</span></div>`;
  if (nota) h += `<div class="nota">${nota}</div>`;
  if (p.tipo === 'tramo') {
    h += `<div class="insp-fila">Columnas del original: desde <input type="number" id="i-desde" value="${p.desde}" ${L.i === 0 ? 'disabled' : ''}>
      hasta <input type="number" id="i-hasta" value="${p.hasta}" ${L.i === n - 1 ? 'disabled' : ''}> (sin incluir)</div>
      <p class="ayuda-corta">Achicar un tramo cambia la altura de entrada o salida: revisa que siga calzando.</p>`;
  } else if (p.tipo === 'recta') {
    const ys = Object.keys(nv.planas).map(Number).sort((a, b) => a - b);
    const yAct = nv.bordes[p.columna] ? nv.bordes[p.columna][0] : null;
    h += `<div class="insp-fila">Altura <select id="i-altura">${ys.map((y) => `<option value="${y}" ${y === yAct ? 'selected' : ''}>${hx(y)}</option>`).join('')}</select>
      Largo <input type="number" id="i-largo" min="1" max="40" value="${p.largo}"> columnas</div>
      <div class="insp-fila">Columna de origen (fondo) <input type="number" id="i-columna" value="${p.columna}"></div>`;
  } else {
    h += `<div class="insp-fila"><textarea id="i-cols" rows="3">${L.cols.join(', ')}</textarea></div>
      <div class="insp-fila"><button id="i-aplicar">Aplicar columnas</button></div>`;
  }
  h += `<div class="insp-fila">
      <button id="i-izq" ${fija || L.i <= 1 ? 'disabled' : ''}>&larr; Mover</button>
      <button id="i-der" ${fija || L.i >= n - 2 ? 'disabled' : ''}>Mover &rarr;</button>
      <button id="i-dup" ${fija ? 'disabled' : ''}>Duplicar</button>
      <button id="i-borrar" class="peligro" ${fija ? 'disabled' : ''}>Borrar</button></div>`;
  const objs = (S._objs || objetosDe(lay)).lista.filter((o) => o.pieza === L.i);
  h += '<h3>Objetos de esta pieza</h3><div class="lista-obj">';
  if (!objs.length) h += '<p class="ayuda-corta">Sin objetos.</p>';
  for (const o of objs)
    h += `<div class="${o.quitado ? 'quitado' : ''}"><span>${o.nombre} &middot; X ${hx(o.x)} Y ${hx(o.y)}${o.origen === 'agregado' ? ' (agregado)' : ''}</span>
      <button data-o="${o.origen}:${o.origen === 'agregado' ? o.agregado : o.indice}">${o.origen === 'agregado' ? 'Borrar' : o.quitado ? 'Restaurar' : 'Quitar'}</button></div>`;
  h += '</div>';
  t.innerHTML = h;

  const num = (id) => parseInt($(id).value, 10);
  if (p.tipo === 'tramo') {
    const aplicar = () => {
      const a = num('i-desde'), b = num('i-hasta');
      if (!(a >= 0 && b > a && b <= nv.columnas)) { toast('Rango de columnas invalido.', true); return; }
      cambiar((r) => { r.piezas[L.i].desde = a; r.piezas[L.i].hasta = b; }, true);
    };
    $('i-desde').addEventListener('change', aplicar);
    $('i-hasta').addEventListener('change', aplicar);
  } else if (p.tipo === 'recta') {
    $('i-largo').addEventListener('change', () => {
      const v = num('i-largo');
      if (v >= 1 && v <= 40) cambiar((r) => { r.piezas[L.i].largo = v; }, true);
    });
    $('i-altura').addEventListener('change', () => {
      const y = parseInt($('i-altura').value, 10);
      const cs = nv.planas[y];
      const c = cs.reduce((a, b) => (Math.abs(b - p.columna) < Math.abs(a - p.columna) ? b : a));
      cambiar((r) => { r.piezas[L.i].columna = c; }, true);
    });
    $('i-columna').addEventListener('change', () => {
      const c = num('i-columna');
      if (!(c >= 0 && c < nv.columnas)) { toast('Columna fuera del nivel.', true); return; }
      cambiar((r) => { r.piezas[L.i].columna = c; }, true);
    });
  } else {
    $('i-aplicar').addEventListener('click', () => {
      const cs = $('i-cols').value.split(/[^0-9]+/).filter(Boolean).map(Number);
      if (!cs.length || cs.some((c) => c >= nv.columnas)) { toast('Lista de columnas invalida.', true); return; }
      cambiar((r) => { r.piezas[L.i].columnas = cs; }, true);
    });
  }
  const b = (id, fn) => { const e = $(id); if (e) e.addEventListener('click', fn); };
  b('i-izq', () => moverPieza(L.i, L.i - 1));
  b('i-der', () => moverPieza(L.i, L.i + 1));
  b('i-dup', () => duplicarPieza(L.i));
  b('i-borrar', () => borrarPieza(L.i));
  t.querySelectorAll('button[data-o]').forEach((e) => e.addEventListener('click', () => {
    const [origen, k] = e.dataset.o.split(':');
    if (origen === 'agregado') borrarAgregado(parseInt(k, 10));
    else alternarQuitado(L.i, parseInt(k, 10));
  }));
}

// ------------------------------------------------------------------ objetos

function alternarQuitado(pieza, indice) {
  cambiar((r) => {
    r.objetos_quitar = r.objetos_quitar || [];
    const k = r.objetos_quitar.findIndex((q) => q.pieza === pieza && q.indice === indice);
    if (k >= 0) r.objetos_quitar.splice(k, 1);
    else r.objetos_quitar.push({ pieza, indice });
  }, true);
}

function borrarAgregado(n) {
  cambiar((r) => { r.objetos_agregar.splice(n, 1); }, true);
}

function agregarObjeto(x, y) {
  const lay = disposicion();
  const L = piezaEn(lay, x);
  const t = S.plantilla;
  const objs = objetosDe(lay);
  if (objs.activos.length >= objs.limite) { toast(`Limite de objetos: ${objs.limite} (como el nivel original). Quita uno antes.`, true); return; }
  cambiar((r) => {
    r.objetos_agregar = r.objetos_agregar || [];
    r.objetos_agregar.push({ pieza: L.i, dx: Math.round(x - L.inicio * COL), y: Math.round(y),
      nombre: t.nombre, puntero: t.puntero, enemigo: t.enemigo });
  }, true);
}

function dibujarObjetos() {
  const nv = nivel();
  const pal = $('pal-objetos');
  pal.innerHTML = '';
  if (!nv) return;
  for (const t of nv.plantillas) {
    const d = document.createElement('div');
    d.className = 'op' + (S.plantilla === t ? ' activa' : '');
    d.innerHTML = `<div class="ico" style="background:${colorObj(t)}">${ABREV[t.nombre] || '?'}</div>
      <div><div class="t">${t.nombre}</div><div class="a">plantilla ${t.puntero} &middot; ${t.usos} en el original</div></div>`;
    const b = document.createElement('button');
    b.textContent = S.plantilla === t ? 'Usando' : 'Usar';
    b.addEventListener('click', () => { S.plantilla = S.plantilla === t ? null : t; modoAgregar(); dibujarObjetos(); });
    d.appendChild(b);
    pal.appendChild(d);
  }
  const lay = disposicion();
  const objs = objetosDe(lay);
  const ag = $('lista-agregados');
  ag.innerHTML = '';
  (S.receta.objetos_agregar || []).forEach((a, n) => {
    const d = document.createElement('div');
    d.className = 'lista-obj';
    d.innerHTML = `<div><span>${a.nombre} &middot; pieza ${a.pieza + 1} +${a.dx} px &middot; Y ${hx(a.y)}</span><button>Borrar</button></div>`;
    d.querySelector('button').addEventListener('click', () => borrarAgregado(n));
    ag.appendChild(d);
  });
  if (!ag.children.length) ag.innerHTML = '<p class="ayuda-corta">Ninguno.</p>';
  const qu = $('lista-quitados');
  qu.innerHTML = '';
  for (const o of objs.lista.filter((o) => o.quitado)) {
    const d = document.createElement('div');
    d.className = 'lista-obj';
    d.innerHTML = `<div class="quitado"><span>${o.nombre} &middot; pieza ${o.pieza + 1} &middot; X ${hx(o.x)}</span><button>Restaurar</button></div>`;
    d.querySelector('button').addEventListener('click', () => alternarQuitado(o.pieza, o.indice));
    qu.appendChild(d);
  }
  if (!qu.children.length) qu.innerHTML = '<p class="ayuda-corta">Ninguno.</p>';
  $('cuenta-objetos').textContent = `Objetos en la etapa: ${objs.activos.length} de ${objs.limite} posibles (mas la puerta de salida).`;
}

function modoAgregar() {
  $('modo-agregar').classList.toggle('oculta', !S.plantilla);
  pista.style.cursor = S.plantilla ? 'copy' : 'crosshair';
}

// ------------------------------------------------------------------ revision

function dibujarRevision() {
  const lay = disposicion();
  const rev = revisar(lay);
  const v = S.validacion;
  let h = '';
  const errores = v && v.errores && v.errores.length ? v.errores : rev.errores;
  if (errores.length) h += errores.map((e) => `<div class="err">${e}</div>`).join('');
  else h += '<div class="okmsg">La etapa es valida: se puede guardar y probar.</div>';
  if (v && v.avisos) h += v.avisos.map((e) => `<div class="avi">${e}</div>`).join('');
  $('rev-errores').innerHTML = h;
  const st = v && v.estadisticas;
  if (st) {
    const filas = [['Columnas', `${st.columnas} (${Math.round(st.largo_vs_original * 100)} % del original)`],
      ['Recorrido aprox.', `${st.segundos_aprox} s`], ['Piezas', `${st.piezas} (${st.tramos_originales} tramos, ${st.piezas_inventadas} inventadas)`],
      ['Hoyos inventados', st.hoyos_inventados], ['Enemigos', st.enemigos], ['Grupos de platanos', st.grupos_platanos]];
    for (const [k, n] of Object.entries(st.objetos || {})) filas.push([k, n]);
    $('rev-stats').innerHTML = `<div class="stats">${filas.map(([a, b]) => `<span>${a}</span><span>${b}</span>`).join('')}</div>`;
  } else $('rev-stats').innerHTML = '<p class="ayuda-corta">Se calculan cuando la etapa es valida.</p>';
  const hist = S.receta.historial || [];
  $('rev-historial').innerHTML = hist.length ? hist.map((x) => `<div class="hist">${x}</div>`).join('') : '<p class="ayuda-corta">Sin cambios registrados.</p>';
  const est = $('estado');
  let cortes = 0;
  for (let k = 0; k + 1 < lay.piezas.length; k++) if (corteVisible(lay, k)) cortes++;
  est.textContent = errores.length ? `${errores.length} problema(s)` :
    cortes ? `valida, ${cortes} corte(s) visible(s)` : 'valida sin cortes';
  est.className = 'pildora ' + (errores.length ? 'mal' : 'ok');
}

function pedirValidacion() {
  clearTimeout(S.timerVal);
  S.timerVal = setTimeout(async () => {
    if (!S.receta) return;
    const enviado = JSON.stringify(S.receta);
    try {
      const v = await api('/api/validar', { receta: S.receta });
      if (JSON.stringify(S.receta) === enviado) { S.validacion = v; dibujarRevision(); dibujarInspector(); }
    } catch (e) { /* sin servidor: queda la revision local */ }
  }, 350);
}

// ------------------------------------------------------------------ propiedades

const PROPS = { 'p-nombre': 'nombre', 'p-familia': 'familia', 'p-tema': 'tema', 'p-musica': 'musica', 'p-descripcion': 'descripcion', 'p-notas': 'notas', 'p-dificultad': 'dificultad' };

function dibujarProps() {
  const r = S.receta;
  for (const [id, k] of Object.entries(PROPS)) if (document.activeElement !== $(id)) $(id).value = r[k] || '';
  $('p-id').value = r.id || '';
  $('p-id').disabled = !S.nueva;
  $('p-nivel').value = S.niveles[r.nivel].titulo;
  $('tit-etapa').textContent = `${r.nombre || r.id} (${r.id})`;
  $('sucia').classList.toggle('oculta', JSON.stringify(r) === S.guardada);
  $('b-deshacer').disabled = !S.deshacer.length;
  $('b-rehacer').disabled = !S.rehacer.length;
}

// musica del nivel de donde sale cada tema (etapa_lib.TEMA_MUSICA)
const TEMA_MUSICA = { jungla: 'isla', cueva: 'cueva', fabrica: 'fabrica', nieve: 'nieve',
  arboles: 'arboles', bosque: 'bosque', templo: 'templo', mina: 'niebla' };

for (const [id, k] of Object.entries(PROPS)) {
  $(id).addEventListener('change', () => {
    const v = $(id).value;
    cambiar((r) => {
      // la musica sigue al tema, salvo que se haya elegido otra
      if (k === 'tema' && (!r.musica || r.musica === TEMA_MUSICA[r.tema || ''])) r.musica = TEMA_MUSICA[v] || '';
      r[k] = k === 'nombre' ? v.toUpperCase() : v;
    }, true);
  });
}
$('p-id').addEventListener('change', () => {
  const v = $('p-id').value.trim().toLowerCase();
  if (!/^[a-z0-9_]{3,40}$/.test(v)) { toast('Id: minusculas, numeros y _ (3 a 40).', true); $('p-id').value = S.receta.id; return; }
  cambiar((r) => { r.id = v; }, true);
});

// ------------------------------------------------------------------ biblioteca

function dibujarBiblioteca() {
  const q = $('f-buscar').value.trim().toLowerCase();
  const nv = $('f-nivel').value, fam = $('f-familia').value;
  const lista = $('lista-etapas');
  lista.innerHTML = '';
  const vis = S.etapas.filter((e) => (!nv || e.nivel === nv) && (!fam || e.familia === fam) &&
    (!q || (e.nombre + ' ' + e.id).toLowerCase().includes(q)));
  for (const e of vis) {
    const d = document.createElement('div');
    d.className = 'item-etapa' + (S.receta && S.receta.id === e.id && !S.nueva ? ' activa' : '');
    d.innerHTML = `<span class="n">${e.nombre}</span><span class="d ${e.dificultad || ''}">${e.dificultad || ''}</span>
      <span class="m">${e.nivel} &middot; ${e.columnas} col &middot; ${e.segundos} s &middot; ${e.hoyos} hoyos${e.en_menu ? '' : ' &middot; fuera del menu'}</span>`;
    d.addEventListener('click', () => abrir(e.id));
    lista.appendChild(d);
  }
  $('cuenta-etapas').textContent = `${vis.length} de ${S.etapas.length} etapas`;
}

async function cargarBiblioteca() {
  const j = await api('/api/etapas');
  S.etapas = j.etapas;
  const fams = [...new Set(S.etapas.map((e) => e.familia).filter(Boolean))].sort();
  const sel = $('f-familia');
  const actual = sel.value;
  sel.innerHTML = '<option value="">Todas las familias</option>' + fams.map((f) => `<option>${f}</option>`).join('');
  sel.value = actual;
  dibujarBiblioteca();
}

function sinGuardar() {
  return S.receta && JSON.stringify(S.receta) !== S.guardada;
}

async function abrir(id) {
  if (sinGuardar() && !confirm('Hay cambios sin guardar en esta etapa. ¿Descartarlos?')) return;
  try {
    const r = await api('/api/etapa?id=' + encodeURIComponent(id));
    cargarReceta(r, false);
  } catch (e) { toast('No se pudo abrir: ' + e.message, true); }
}

function cargarReceta(r, nueva) {
  r.objetos_quitar = r.objetos_quitar || [];
  r.objetos_agregar = r.objetos_agregar || [];
  r.platanos_quitar = r.platanos_quitar || [];
  S.receta = r;
  S.nueva = nueva;
  S.guardada = nueva ? '' : JSON.stringify(r);
  S.deshacer = [];
  S.rehacer = [];
  S.sel = Math.min(1, r.piezas.length - 1);
  S.x0 = 0;
  S.validacion = null;
  S.plantilla = null;
  modoAgregar();
  todo();
  dibujarBiblioteca();
}

// ------------------------------------------------------------------ guardar / probar / nueva

async function guardar() {
  if (!S.receta) return false;
  const rev = revisar(disposicion());
  if (rev.errores.length) { toast('No se puede guardar: ' + rev.errores[0], true); verPestana('revision'); return false; }
  const lay = disposicion();
  try {
    const j = await api('/api/guardar', {
      receta: S.receta, nueva: S.nueva,
      cambio: S.nueva ? `creada en el editor (${lay.piezas.length} piezas, ${lay.total} columnas)` :
        `editada en el editor (${lay.piezas.length} piezas, ${lay.total} columnas)`,
    });
    S.receta = j.receta;
    S.guardada = JSON.stringify(S.receta);
    S.nueva = false;
    S.validacion = j;
    toast(`Guardada y compilada: stages\\${S.receta.id}.c2p (en el menu del juego).`);
    await cargarBiblioteca();
    todo();
    return true;
  } catch (e) {
    toast('No se pudo guardar: ' + e.message + (e.datos && e.datos.errores ? ' - ' + e.datos.errores.join(' ') : ''), true);
    return false;
  }
}

async function probar() {
  if (!S.receta) return;
  if (sinGuardar() || S.nueva) {
    if (!confirm('Para probarla hay que guardarla primero. ¿Guardar ahora?')) return;
    if (!(await guardar())) return;
  }
  try {
    await api('/api/probar', { id: S.receta.id });
    toast('Abriendo el juego en ' + S.receta.nombre + '...');
  } catch (e) { toast('No se pudo abrir el juego: ' + e.message, true); }
}

function recetaOriginal(nv, id, nombre) {
  return {
    formato: 1, id, nombre, nivel: nv.nivel, familia: 'PROPIA', dificultad: '',
    autor: 'editor', descripcion: '', notas: '',
    piezas: nv.tramos.map((t) => ({ tipo: 'tramo', desde: t.a, hasta: t.b })),
    objetos_quitar: [], objetos_agregar: [], platanos_quitar: [], historial: [],
  };
}

function dialogoNueva(duplicar) {
  if (sinGuardar() && !confirm('Hay cambios sin guardar en esta etapa. ¿Descartarlos?')) return;
  const dlg = $('dlg-nueva');
  $('dlg-titulo').textContent = duplicar ? 'Duplicar etapa' : 'Nueva etapa';
  $('n-nivel').disabled = !!duplicar;
  $('n-ayuda').textContent = duplicar ? 'Copia de la etapa actual con otro id y nombre; la original no cambia.'
    : 'Empieza como el nivel original completo, cortado en sus tramos. Desde ahi se reordena, se quitan tramos y se agregan rectas y hoyos.';
  if (duplicar) {
    $('n-nivel').value = S.receta.nivel;
    $('n-id').value = S.receta.id + '_b';
    $('n-nombre').value = (S.receta.nombre + ' B').slice(0, 28);
  } else {
    $('n-id').value = 'mia_' + Date.now().toString(36).slice(-5);
    $('n-nombre').value = 'MI ETAPA';
  }
  dlg.returnValue = '';
  dlg.showModal();
  dlg.onclose = () => {
    if (dlg.returnValue !== 'ok') return;
    const id = $('n-id').value.trim().toLowerCase();
    if (!/^[a-z0-9_]{3,40}$/.test(id)) { toast('Id invalido.', true); return; }
    if (S.etapas.some((e) => e.id === id)) { toast('Ya existe una etapa con ese id.', true); return; }
    const nombre = $('n-nombre').value.toUpperCase().slice(0, 28);
    let r;
    if (duplicar) {
      r = clon(S.receta);
      r.id = id;
      r.nombre = nombre;
      r.historial = [...(r.historial || []), `${new Date().toISOString().slice(0, 10)} duplicada de ${S.receta.id}`];
      delete r.modificada;
    } else r = recetaOriginal(S.niveles[$('n-nivel').value], id, nombre);
    cargarReceta(r, true);
    toast('Etapa nueva (aun no guardada).');
  };
}

// ------------------------------------------------------------------ eventos pista

function aEtapa(ev) {
  const r = pista.getBoundingClientRect();
  return { x: S.x0 + (ev.clientX - r.left) / S.zoom, y: 0x200 - (ev.clientY - r.top) / S.zoom };
}

function objetoBajo(p) {
  if (!S._objs || !$('v-objetos').checked) return null;
  const rad = Math.max(7, 11 * S.zoom) / S.zoom;
  let mejor = null, d0 = 1e9;
  for (const o of S._objs.lista) {
    const d = Math.hypot(o.x - p.x, o.y - p.y);
    if (d <= rad + 2 && d < d0) { mejor = o; d0 = d; }
  }
  return mejor;
}

pista.addEventListener('mousedown', (ev) => {
  if (!S.receta || ev.button !== 0) return;
  const p = aEtapa(ev);
  if (S.plantilla) { agregarObjeto(p.x, p.y); return; }
  const o = objetoBajo(p);
  if (o && o.origen === 'agregado') {
    S.arrastre = { n: o.agregado, x: p.x, y: p.y, movio: false, antes: JSON.stringify(S.receta) };
    return;
  }
  if (o && o.origen === 'nivel') { alternarQuitado(o.pieza, o.indice); return; }
  const L = piezaEn(S._lay || disposicion(), p.x);
  S.sel = L.i;
  todo();
});

window.addEventListener('mousemove', (ev) => {
  if (!S.receta) return;
  const r = pista.getBoundingClientRect();
  if (ev.clientX >= r.left && ev.clientX <= r.right && ev.clientY >= r.top && ev.clientY <= r.bottom) {
    const p = aEtapa(ev);
    const lay = S._lay || disposicion();
    const n = Math.floor(p.x / COL);
    const src = lay.src[n];
    $('pos-cursor').textContent = `X ${hx(Math.max(0, Math.round(p.x)))}  Y ${hx(Math.max(0, Math.round(p.y)))}  columna ${n}${src != null ? ' (original ' + src + ')' : ''}`;
  }
  if (S.arrastre) {
    const p = aEtapa(ev);
    const a = S.receta.objetos_agregar[S.arrastre.n];
    const lay = disposicion();
    const L = piezaEn(lay, p.x);
    a.pieza = L.i;
    a.dx = Math.round(p.x - L.inicio * COL);
    a.y = Math.round(Math.max(0x10, Math.min(0x1F0, p.y)));
    S.arrastre.movio = true;
    dibujarPista();
  }
});

window.addEventListener('mouseup', () => {
  if (!S.arrastre) return;
  if (S.arrastre.movio) {
    S.deshacer.push(S.arrastre.antes);
    S.rehacer.length = 0;
    todo();
  }
  S.arrastre = null;
});

pista.addEventListener('contextmenu', (ev) => {
  ev.preventDefault();
  const o = objetoBajo(aEtapa(ev));
  if (o && o.origen === 'agregado') borrarAgregado(o.agregado);
});

pista.addEventListener('wheel', (ev) => {
  ev.preventDefault();
  if (ev.ctrlKey) {
    const p = aEtapa(ev);
    const z = Math.max(0.25, Math.min(1.5, S.zoom * (ev.deltaY < 0 ? 1.1 : 0.9)));
    const r = pista.getBoundingClientRect();
    S.zoom = z;
    $('zoom').value = z;
    S.x0 = p.x - (ev.clientX - r.left) / z;
    $('zoom-val').textContent = Math.round(z * 100) + '%';
  } else {
    S.x0 += (ev.deltaY + ev.deltaX) / S.zoom;
  }
  dibujarPista();
}, { passive: false });

$('scroll-pista').addEventListener('scroll', () => {
  if (S._scrollPropio) { S._scrollPropio = false; return; }
  S.x0 = $('scroll-pista').scrollLeft / S.zoom;
  dibujarPista();
});

mini.addEventListener('click', (ev) => {
  if (!S._lay) return;
  const r = mini.getBoundingClientRect();
  const x = (ev.clientX - r.left) / r.width * S._lay.total * COL;
  S.x0 = x - pista.width / S.zoom / 2;
  dibujarPista();
});

$('zoom').addEventListener('input', () => {
  const centro = S.x0 + pista.width / S.zoom / 2;
  S.zoom = parseFloat($('zoom').value);
  S.x0 = centro - pista.width / S.zoom / 2;
  $('zoom-val').textContent = Math.round(S.zoom * 100) + '%';
  dibujarPista();
});
for (const id of ['v-objetos', 'v-platanos', 'v-via', 'v-piezas', 'v-zonas']) $(id).addEventListener('change', dibujarPista);
$('solo-compat').addEventListener('change', dibujarPaleta);
$('recta-largo').addEventListener('change', dibujarPaleta);
$('sincorte-largo').addEventListener('change', dibujarPaleta);
for (const id of ['f-buscar', 'f-nivel', 'f-familia']) $(id).addEventListener('input', dibujarBiblioteca);
window.addEventListener('resize', () => dibujarPista());

document.querySelectorAll('.pestanas button').forEach((b) => b.addEventListener('click', () => verPestana(b.dataset.p)));
function verPestana(p) {
  document.querySelectorAll('.pestanas button').forEach((b) => b.classList.toggle('activa', b.dataset.p === p));
  for (const t of ['piezas', 'pieza', 'objetos', 'revision']) $('t-' + t).classList.toggle('oculta', t !== p);
}

$('b-guardar').addEventListener('click', guardar);
$('b-probar').addEventListener('click', probar);
$('b-nueva').addEventListener('click', () => dialogoNueva(false));
$('b-duplicar').addEventListener('click', () => { if (S.receta) dialogoNueva(true); });
$('b-deshacer').addEventListener('click', deshacer);
$('b-rehacer').addEventListener('click', rehacer);
$('b-ayuda').addEventListener('click', () => $('dlg-ayuda').showModal());

document.addEventListener('keydown', (ev) => {
  const enCampo = ['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement.tagName);
  if (ev.key === 'F1') { ev.preventDefault(); $('dlg-ayuda').showModal(); return; }
  if (ev.ctrlKey && ev.key.toLowerCase() === 's') { ev.preventDefault(); guardar(); return; }
  if (enCampo || !S.receta) return;
  if (ev.ctrlKey && ev.key.toLowerCase() === 'z') { ev.preventDefault(); deshacer(); return; }
  if (ev.ctrlKey && ev.key.toLowerCase() === 'y') { ev.preventDefault(); rehacer(); return; }
  if (ev.ctrlKey && ev.key.toLowerCase() === 'd') { ev.preventDefault(); duplicarPieza(S.sel); return; }
  if (ev.key === 'Escape') { S.plantilla = null; modoAgregar(); dibujarObjetos(); return; }
  if (ev.key === 'Delete') { borrarPieza(S.sel); return; }
  if (ev.key === 'ArrowLeft' || ev.key === 'ArrowRight') {
    ev.preventDefault();
    const d = ev.key === 'ArrowLeft' ? -1 : 1;
    if (ev.altKey) moverPieza(S.sel, S.sel + d);
    else { S.sel = Math.max(0, Math.min(S.receta.piezas.length - 1, S.sel + d)); irAPieza(disposicion().piezas[S.sel]); todo(); }
  }
});

window.addEventListener('beforeunload', (ev) => { if (sinGuardar()) { ev.preventDefault(); ev.returnValue = ''; } });

// ------------------------------------------------------------------ inicio

function todo() {
  if (!S.receta) return;
  dibujarProps();
  dibujarPista();
  dibujarLinea();
  dibujarPaleta();
  dibujarInspector();
  dibujarObjetos();
  dibujarRevision();
  pedirValidacion();
}

async function iniciar() {
  try {
    S.niveles = await api('/api/niveles');
  } catch (e) {
    document.body.innerHTML = '<p style="padding:30px">No hay conexion con el servidor del editor. Abrirlo con cart2p\\JUGAR.bat, opcion 5.</p>';
    return;
  }
  await Promise.all(Object.values(S.niveles).map((nv) => new Promise((ok) => {
    const img = new Image();
    img.onload = ok;
    img.onerror = ok;
    img.src = 'datos/' + nv.imagen;
    S.imgs[nv.nivel] = img;
  })));
  await cargarBiblioteca();
  const ultima = (() => { try { return localStorage.getItem('cart2p_ultima'); } catch (e) { return null; } })();
  const id = ultima && S.etapas.some((e) => e.id === ultima) ? ultima : (S.etapas[0] && S.etapas[0].id);
  if (id) await abrir(id);
}
window.addEventListener('beforeunload', () => { try { if (S.receta) localStorage.setItem('cart2p_ultima', S.receta.id); } catch (e) { /* sin almacenamiento */ } });

iniciar();
