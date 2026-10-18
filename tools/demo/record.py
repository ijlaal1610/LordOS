#!/usr/bin/env python3
"""Graba la pantalla de una VM de Proxmox como secuencia de PNG (para tools/demo/make-video.sh).

Uso (en el host Proxmox):  python3 record.py <vmid> <carpeta> [fps=5]
Corre hasta que se crea <carpeta>/STOP, o 30 minutos como máximo. Por cada cuadro manda un `screendump` por el socket
QMP de la VM y se desconecta enseguida: así no le bloquea el socket a Proxmox (`qm` lo usa).
En <carpeta>/frames.txt anota el tiempo de cada cuadro para respetar la velocidad real."""
import json, os, socket, sys, time

vmid, out = sys.argv[1], sys.argv[2]
fps = float(sys.argv[3]) if len(sys.argv) > 3 else 5.0
sock_path = f'/var/run/qemu-server/{vmid}.qmp'
os.makedirs(out, exist_ok=True)


def qmp(cmd):
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(3)
    s.connect(sock_path)
    f = s.makefile('rwb')
    f.readline()                                                  # saludo
    for c in ({'execute': 'qmp_capabilities'}, cmd):
        f.write((json.dumps(c) + '\n').encode()); f.flush()
        while True:
            r = json.loads(f.readline())
            if 'return' in r or 'error' in r:
                break
    s.close()
    return r


n, t0 = 0, time.time()
with open(os.path.join(out, 'frames.txt'), 'w') as log:
    while not os.path.exists(os.path.join(out, 'STOP')) and time.time() - t0 < 1800:
        start = time.time()
        name = f'f{n:06d}.png'
        try:
            r = qmp({'execute': 'screendump', 'arguments': {'filename': os.path.join(out, name), 'format': 'png'}})
            if 'return' in r:
                log.write(f'{start - t0:.3f} {name}\n'); log.flush()
                n += 1
        except OSError:
            time.sleep(0.2)                                       # la VM se está reiniciando
        time.sleep(max(0.0, 1.0 / fps - (time.time() - start)))
print(f'{n} cuadros en {time.time() - t0:.0f} s')
