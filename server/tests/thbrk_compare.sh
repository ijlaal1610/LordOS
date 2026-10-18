#!/bin/sh
# thbrk_compare.sh — compara los cortes del tailandés de LordOS (build/thbrk-test) con los de
# libthai, renglón por renglón. Solo cuentan los cortes entre dos caracteres del bloque
# tailandés: los espacios, el latín y la puntuación los resuelve el corte común de la interfaz.
# Necesita libthai-dev (solo para la prueba), libthai-data y python3.
#   sh tests/thbrk_compare.sh [texto.txt]
cd "$(dirname "$0")/.." || exit 1
in=${1:-tests/th-break.txt}
mkdir -p build
cc -O2 -o build/thbrk-libthai tests/thbrk_libthai.c -lthai || exit 1
make -s build/thbrk-test || exit 1
build/thbrk-test < "$in" > build/thbrk-lordos.txt
build/thbrk-libthai < "$in" > build/thbrk-ref.txt
python3 - build/thbrk-lordos.txt build/thbrk-ref.txt <<'PY'
import re, sys
th = lambda c: '฀' <= c <= '๿'
def norm(l):
    out = []
    for i, c in enumerate(l):
        if c == '|':
            a = l[i - 1] if i else ''
            b = l[i + 1] if i + 1 < len(l) else ''
            if not (a and b and th(a) and th(b)): continue
        out.append(c)
    return ''.join(out)
a = open(sys.argv[1], encoding='utf-8').read().splitlines()
b = open(sys.argv[2], encoding='utf-8').read().splitlines()
same = 0
for x, y in zip(a, b):
    if norm(x) == norm(y): same += 1
    else: print('lordos :', norm(x)); print('libthai:', norm(y))
print(f'{same}/{len(b)} iguales a libthai')
sys.exit(0 if same == len(b) else 1)
PY
