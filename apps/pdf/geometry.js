// geometry.js — de puntos de la página (lo que devuelve lord-doc, origen arriba a la izquierda) a
// píxeles de la vista con zoom y rotación, y al revés. Misma transformación que usa el ayudante
// al dibujar (h_pdf.c, op_render).

// Tamaño mostrado de una página [w, h] en puntos con la rotación (0..3, en cuartos de vuelta).
export const shown = ([w, h], rot) => (rot % 2 ? [h, w] : [w, h]);

// punto de la página → vista (s = píxeles por punto)
export function toView(x, y, [pw, ph], rot, s) {
  switch (rot) {
    case 1: return [(ph - y) * s, x * s];
    case 2: return [(pw - x) * s, (ph - y) * s];
    case 3: return [y * s, (pw - x) * s];
    default: return [x * s, y * s];
  }
}

// vista → punto de la página
export function toPage(vx, vy, [pw, ph], rot, s) {
  const a = vx / s, b = vy / s;
  switch (rot) {
    case 1: return [b, ph - a];
    case 2: return [pw - a, ph - b];
    case 3: return [pw - b, a];
    default: return [a, b];
  }
}

// [x, y, w, h] de la página → { left, top, width, height } en la vista
export function rectToView([x, y, w, h], size, rot, s) {
  const a = toView(x, y, size, rot, s), b = toView(x + w, y + h, size, rot, s);
  return { left: Math.min(a[0], b[0]), top: Math.min(a[1], b[1]), width: Math.abs(a[0] - b[0]), height: Math.abs(a[1] - b[1]) };
}

// Disposición continua: la posición vertical de cada página y el alto total.
export function layoutPages(sizes, rot, s, gap) {
  const tops = new Array(sizes.length);
  let y = gap, maxW = 0;
  for (let i = 0; i < sizes.length; i++) {
    const [w, h] = shown(sizes[i], rot);
    tops[i] = y;
    y += h * s + gap;
    if (w * s > maxW) maxW = w * s;
  }
  return { tops, height: y, width: maxW };
}

// Páginas que se ven entre y0 e y1 (búsqueda binaria sobre las posiciones).
export function visibleRange(tops, sizes, rot, s, y0, y1) {
  if (!tops.length) return [0, -1];
  let lo = 0, hi = tops.length - 1;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (tops[mid] <= y0) lo = mid; else hi = mid - 1;
  }
  let last = lo;
  while (last + 1 < tops.length && tops[last + 1] < y1) last++;
  return [lo, last];
}
