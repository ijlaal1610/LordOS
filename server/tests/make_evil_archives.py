#!/usr/bin/env python3
"""Arma comprimidos maliciosos para tests/doc_evil.sh (extracción segura de lord-doc).

Uso: python3 -I make_evil_archives.py <carpeta>
Cada uno intenta escribir fuera de la carpeta de destino de una forma distinta. Los objetivos
apuntan a <carpeta>/../FUERA-* (un nombre que la prueba revisa después)."""
import io, os, sys, tarfile, zipfile

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

def tar(name, members, mode='w'):
    with tarfile.open(os.path.join(out, name), mode) as t:
        for m, data in members:
            t.addfile(m, io.BytesIO(data) if data is not None else None)

def reg(path, data=b'x', mode=0o644):
    i = tarfile.TarInfo(path); i.size = len(data); i.mode = mode; return (i, data)

def sym(path, target):
    i = tarfile.TarInfo(path); i.type = tarfile.SYMTYPE; i.linkname = target; return (i, None)

def hard(path, target):
    i = tarfile.TarInfo(path); i.type = tarfile.LNKTYPE; i.linkname = target; return (i, None)

def dev(path):
    i = tarfile.TarInfo(path); i.type = tarfile.CHRTYPE; i.devmajor = 1; i.devminor = 3; return (i, None)

# 1. «zip slip»: ../ en el nombre
with zipfile.ZipFile(os.path.join(out, 'slip.zip'), 'w') as z:
    z.writestr('ok.txt', 'bien')
    z.writestr('../FUERA-slip.txt', 'mal')
    z.writestr('a/../../FUERA-slip2.txt', 'mal')
# 2. ruta absoluta
tar('absoluta.tar', [reg('ok.txt'), reg('/tmp/FUERA-abs.txt')])
# 3. enlace a afuera y después un archivo «a través» del enlace
tar('enlace.tar', [sym('salida', '..'), reg('salida/FUERA-link.txt'), sym('etc', '/etc'), reg('etc/FUERA-etc.txt'), reg('ok.txt')])
# 4. cadena de enlaces: cada uno parece adentro, juntos salen
tar('cadena.tar.gz', [reg('d/ok.txt'), sym('d/l1', '..'), sym('d/l2', 'l1/..'), reg('d/l2/FUERA-chain.txt')], 'w:gz')
# 5. enlace duro a un archivo de afuera
tar('duro.tar', [hard('passwd', '/etc/passwd'), hard('p2', '../FUERA-hard'), reg('ok.txt')])
# 6. dispositivo, setuid y FIFO
fifo = tarfile.TarInfo('fifo'); fifo.type = tarfile.FIFOTYPE
tar('especiales.tar.xz', [dev('null'), reg('suid', b'#!/bin/sh\n', 0o4755), (fifo, None), reg('ok.txt')], 'w:xz')
# 7. archivo que ya existe dos veces (no pisa)
tar('repetido.tar', [reg('mismo.txt', b'uno'), reg('mismo.txt', b'dos')])
# 8. comprimido sano con carpetas y nombres en varias escrituras
with zipfile.ZipFile(os.path.join(out, 'sano.zip'), 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('carpeta/ñandú.txt', 'hola')
    z.writestr('carpeta/日本語.txt', 'こんにちは')
    z.writestr('carpeta/sub/vacío/', '')
    z.writestr('README', 'x' * 100000)
# 9. no es un comprimido
open(os.path.join(out, 'basura.zip'), 'wb').write(os.urandom(4096))
# 10. .gz suelto (formato raw)
import gzip
with gzip.open(os.path.join(out, 'suelto.txt.gz'), 'wb') as g:
    g.write(b'texto comprimido suelto\n')
print('listo:', sorted(os.listdir(out)))
