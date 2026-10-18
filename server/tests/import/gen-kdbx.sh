#!/bin/sh
# Genera las bóvedas KDBX de prueba (VM 116). Contraseña de todas: "lordos-prueba"
#   kx3-aeskdf.kdbx: KeePassXC 2.7.10 (keepassxc-cli import de sample.xml): KDBX 3.1, AES-256, AES-KDF
#   kx4-*.kdbx: pykeepass 4.2.0 (KDBX 4), con las mismas entradas
set -e
cd "$(dirname "$0")"
rm -f kx*.kdbx kx-*.key kx-*.keyx
printf 'lordos-prueba\nlordos-prueba\n' | keepassxc-cli import -q -p -t 100 sample.xml kx3-aeskdf.kdbx
venv/bin/python3 conv.py
