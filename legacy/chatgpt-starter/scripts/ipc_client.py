#!/usr/bin/env python3
import os, socket, sys
if len(sys.argv) < 2:
    sys.exit('Uso: python3 scripts/ipc_client.py list | move ID X Y | focus ID')
command = sys.argv[1].lower()
if command == 'list' and len(sys.argv) == 2:
    data = 'LIST\n'
elif command == 'move' and len(sys.argv) == 5:
    data = f'MOVE {int(sys.argv[2])} {int(sys.argv[3])} {int(sys.argv[4])}\n'
elif command == 'focus' and len(sys.argv) == 3:
    data = f'FOCUS {int(sys.argv[2])}\n'
else:
    sys.exit('Argumentos invalidos')
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
    sock.connect(f'/tmp/lordos-core-{os.getuid()}.sock')
    sock.sendall(data.encode())
    sock.shutdown(socket.SHUT_WR)
    while True:
        piece = sock.recv(4096)
        if not piece: break
        print(piece.decode(), end='')
