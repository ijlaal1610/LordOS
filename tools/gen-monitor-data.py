#!/usr/bin/env python3
"""gen-monitor-data.py — datos de la app Monitor, generados una vez en la máquina de desarrollo.

Uso:
  python3 tools/gen-monitor-data.py <ne_50m_admin_0_countries.geojson> <dbip-country-lite-AAAA-MM.csv>

Genera (solo con la biblioteca estándar de Python, sin red):
  assets/monitor/land.bin             contornos de los países (Natural Earth 1:50m, dominio público),
                                      simplificados y en centésimos de grado; lord-server los
                                      rellena en vectores a cualquier zoom (server/src/monitor/map.c)
  assets/monitor/geoip-country.mmdb.xz   IP → país (DB-IP Lite Country, CC BY 4.0), en formato
                                      MaxMind DB mínimo (solo {"country": {"iso_code"}}), comprimido con xz
  apps/monitor/countries.js           [longitud, latitud] de la etiqueta de cada país (LABEL_X/LABEL_Y)
  assets/locales/{es,en,pt}/countries.json   nombres de los países (NAME_ES/EN/PT de Natural Earth)

Descargas (a mano, una vez):
  https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_50m_admin_0_countries.geojson
  https://download.db-ip.com/free/dbip-country-lite-AAAA-MM.csv.gz   (descomprimido)

Formato de land.bin (little-endian):
  "LORDMAP1", u32 países; por país: 2 bytes ISO (o "--"), i16 bbox[4] (lon0 lat0 lon1 lat1),
  u16 anillos; por anillo: u32 puntos y i16 (lon, lat) × puntos, en centésimos de grado.
  Los anillos interiores (agujeros) van como anillos comunes: se rellena par-impar.
"""
import csv
import ipaddress
import json
import lzma
import os
import struct
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAX4, MAX6 = 24, 32                 # precisión del archivo de países (ver build_mmdb)
SIMPLIFY = 0.02                     # tolerancia de Douglas-Peucker, en grados (~2 km)


def iso_of(props):
    for k in ('ISO_A2_EH', 'ISO_A2', 'WB_A2'):
        v = props.get(k)
        if isinstance(v, str) and len(v) == 2 and v.isalpha() and v.upper() == v:
            return v
    return None


def rings_of(geom):
    if geom['type'] == 'Polygon':
        return [geom['coordinates']]
    if geom['type'] == 'MultiPolygon':
        return geom['coordinates']
    return []


# ---- contornos (land.bin) -------------------------------------------------------------------

def simplify(pts, eps):
    """Douglas-Peucker iterativo."""
    if len(pts) < 4:
        return pts
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        i, j = stack.pop()
        (x0, y0), (x1, y1) = pts[i], pts[j]
        dx, dy = x1 - x0, y1 - y0
        n = (dx * dx + dy * dy) ** 0.5
        best, at = 0.0, -1
        for k in range(i + 1, j):
            x, y = pts[k]
            d = abs(dy * (x - x0) - dx * (y - y0)) / n if n else ((x - x0) ** 2 + (y - y0) ** 2) ** 0.5
            if d > best:
                best, at = d, k
        if best > eps and at > 0:
            keep[at] = True
            stack.append((i, at))
            stack.append((at, j))
    return [p for p, k in zip(pts, keep) if k]


def build_land(geojson, out):
    blob, count, npts = b'', 0, 0
    for feat in geojson['features']:
        cc = iso_of(feat['properties']) or '--'
        if cc == 'AQ':                      # la Antártida queda fuera del mapa (corta en 56° S)
            continue
        rings = []
        for poly in rings_of(feat['geometry']):
            for ring in poly:
                q = []
                for lon, lat in simplify([tuple(p[:2]) for p in ring], SIMPLIFY):
                    pt = (round(lon * 100), round(lat * 100))
                    if not q or q[-1] != pt:
                        q.append(pt)
                if len(q) >= 4:
                    rings.append(q)
        if not rings:
            continue
        xs = [x for r in rings for x, _ in r]
        ys = [y for r in rings for _, y in r]
        blob += cc.encode()[:2] + struct.pack('<4hH', min(xs), min(ys), max(xs), max(ys), len(rings))
        for r in rings:
            blob += struct.pack('<I', len(r)) + b''.join(struct.pack('<2h', x, y) for x, y in r)
            npts += len(r)
        count += 1
    with open(out, 'wb') as f:
        f.write(b'LORDMAP1' + struct.pack('<I', count) + blob)
    return count, npts


# ---- IP → país (MaxMind DB) -------------------------------------------------------------

def mm_str(s):
    b = s.encode()
    assert len(b) < 29
    return bytes([0x40 | len(b)]) + b


def mm_map(n):
    return bytes([0xE0 | n])


def mm_uint(v, typ):
    b = v.to_bytes((v.bit_length() + 7) // 8, 'big') if v else b''
    if typ <= 7:
        return bytes([(typ << 5) | len(b)]) + b
    return bytes([len(b), typ - 7]) + b


def build_mmdb(csv_path, out):
    codes, ranges = {}, []
    with open(csv_path, newline='') as f:
        for a, b, cc in csv.reader(f):
            if cc == 'ZZ' or len(cc) != 2:
                continue
            lo, hi = ipaddress.ip_address(a), ipaddress.ip_address(b)
            if lo.version == 4:
                lo, hi = int(lo), int(hi)               # IPv4 en ::/96, como en GeoIP2
            else:
                lo, hi = int(lo), int(hi)
                if lo >> 32 == 0:                       # no pisar el subárbol de IPv4
                    continue
            ranges.append((lo, hi, cc))
            codes.setdefault(cc, len(codes))
    ranges.sort()
    merged = []
    for lo, hi, cc in ranges:                           # rangos contiguos del mismo país
        if merged and merged[-1][2] == cc and merged[-1][1] + 1 == lo:
            merged[-1][1] = hi
        else:
            merged.append([lo, hi, cc])
    # árbol binario de 128 bits: L/R = índice de nodo (>0), 0 = vacío, -(país+1) = hoja
    L, R = [0], [0]

    def insert(net, plen, val):
        node = 0
        for d in range(plen):
            bit = (net >> (127 - d)) & 1
            arr = R if bit else L
            nxt = arr[node]
            if d == plen - 1:
                assert nxt == 0, 'rangos superpuestos'
                arr[node] = val
                return
            assert nxt >= 0, 'rangos superpuestos'
            if nxt == 0:
                L.append(0)
                R.append(0)
                nxt = len(L) - 1
                arr[node] = nxt
            node = nxt

    # Para que el archivo sea chico, los prefijos más largos que /24 (IPv4: el bloque más chico
    # que se enruta en Internet) o /MAX6 (IPv6) no se guardan: el bloque entero va al país que
    # tiene más direcciones adentro.
    blocks = {}
    for lo, hi, cc in merged:
        val = -(codes[cc] + 1)
        while lo <= hi:                                 # rango → prefijos CIDR
            size = (lo & -lo).bit_length() - 1 if lo else 128
            while size > 0 and lo + (1 << size) - 1 > hi:
                size -= 1
            plen = 128 - size
            lim = 96 + MAX4 if lo >> 32 == 0 else MAX6
            if plen <= lim:
                insert(lo, plen, val)
            else:
                cnt = blocks.setdefault((lo >> (128 - lim), lim), {})
                cnt[val] = cnt.get(val, 0) + (1 << size)
            lo += 1 << size
    for (net, lim), cnt in blocks.items():
        insert(net << (128 - lim), lim, max(cnt.items(), key=lambda kv: kv[1])[0])
    # nodos que terminaron con dos hojas iguales se pliegan (de abajo hacia arriba)
    parent_val = {}
    changed = True
    while changed:
        changed = False
        for i in range(len(L) - 1, -1, -1):
            for arr in (L, R):
                c = arr[i]
                if c > 0 and L[c] < 0 and L[c] == R[c]:
                    arr[i] = L[c]
                    L[c] = R[c] = 0
                    changed = True
    # renumerar los nodos alcanzables en orden BFS
    order, idx = [0], {0: 0}
    k = 0
    while k < len(order):
        n = order[k]
        k += 1
        for c in (L[n], R[n]):
            if c > 0 and c not in idx:
                idx[c] = len(order)
                order.append(c)
    node_count = len(order)
    data, offs = b'', {}
    for cc, i in sorted(codes.items(), key=lambda x: x[1]):
        offs[i] = len(data)
        data += mm_map(1) + mm_str('country') + mm_map(1) + mm_str('iso_code') + mm_str(cc)

    def rec(c):
        if c > 0:
            return idx[c]
        if c == 0:
            return node_count
        return node_count + 16 + offs[-c - 1]

    tree = bytearray()
    for n in order:
        l, r = rec(L[n]), rec(R[n])
        assert l < 1 << 24 and r < 1 << 24, 'hace falta record_size 28'
        tree += l.to_bytes(3, 'big') + r.to_bytes(3, 'big')
    meta = mm_map(8)
    meta += mm_str('node_count') + mm_uint(node_count, 6)
    meta += mm_str('record_size') + mm_uint(24, 5)
    meta += mm_str('ip_version') + mm_uint(6, 5)
    meta += mm_str('database_type') + mm_str('DBIP-Country-Lite')
    meta += mm_str('binary_format_major_version') + mm_uint(2, 5)
    meta += mm_str('binary_format_minor_version') + mm_uint(0, 5)
    meta += mm_str('build_epoch') + mm_uint(int(time.time()), 9)
    meta += mm_str('languages') + bytes([0x00, 0x04])   # arreglo vacío
    db = bytes(tree) + b'\x00' * 16 + data + b'\xab\xcd\xefMaxMind.com' + meta
    # comprimido con xz (solo LZMA2, CRC64): lord-server lo descomprime con su xz propio
    with open(out, 'wb') as f:
        f.write(lzma.compress(db, format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC64, preset=9 | lzma.PRESET_EXTREME))
    return node_count, len(merged)


# ---- países -----------------------------------------------------------------------------

def countries(geojson):
    pos, names = {}, {'es': {}, 'en': {}, 'pt': {}}
    for feat in geojson['features']:
        p = feat['properties']
        cc = iso_of(p)
        if not cc or cc in pos:
            continue
        lon, lat = p.get('LABEL_X'), p.get('LABEL_Y')
        if lon is None or lat is None:
            continue
        pos[cc] = [round(lon, 2), round(lat, 2)]
        for lang, key in (('es', 'NAME_ES'), ('en', 'NAME_EN'), ('pt', 'NAME_PT')):
            names[lang][cc] = p.get(key) or p.get('NAME_EN') or p.get('NAME') or cc
    return pos, names


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    ne50, dbip = sys.argv[1:]
    os.makedirs(os.path.join(ROOT, 'assets/monitor'), exist_ok=True)
    geo = json.load(open(ne50))
    nc, npts = build_land(geo, os.path.join(ROOT, 'assets/monitor/land.bin'))
    pos, names = countries(geo)
    with open(os.path.join(ROOT, 'apps/monitor/countries.js'), 'w') as f:
        f.write('// countries.js — generado por tools/gen-monitor-data.py (Natural Earth 1:50m, dominio público).\n')
        f.write('// [longitud, latitud] del lugar de la etiqueta de cada país.\n')
        f.write('export const COUNTRY_LL = ' + json.dumps(pos, separators=(',', ':'), sort_keys=True) + ';\n')
    for lang, obj in names.items():
        with open(os.path.join(ROOT, f'assets/locales/{lang}/countries.json'), 'w') as f:
            json.dump(dict(sorted(obj.items())), f, ensure_ascii=False, indent=2)
            f.write('\n')
    nodes, nranges = build_mmdb(dbip, os.path.join(ROOT, 'assets/monitor/geoip-country.mmdb.xz'))
    print(f'contornos: {nc} países, {npts} puntos; {len(pos)} etiquetas; mmdb: {nranges} rangos, {nodes} nodos')


if __name__ == '__main__':
    main()
