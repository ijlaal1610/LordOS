# media_cpu.py — docs/MEDIOS.md, «Mediciones».
# Uso: python3 cpu.py SEGUNDOS — CPU (% de un núcleo) de lord-server y de cada lord-media del usuario lord
import os, sys, time
def procs():
    out = {}
    for p in os.listdir('/proc'):
        if not p.isdigit(): continue
        try:
            st = open(f'/proc/{p}/status').read()
            name = open(f'/proc/{p}/comm').read().strip()
            uid = int([l for l in st.splitlines() if l.startswith('Uid:')][0].split()[1])
            cmd = open(f'/proc/{p}/cmdline').read().replace('\0', ' ')
        except Exception: continue
        if uid == 1000 and name in ('lord-server', 'lord-media') and '--greeter' not in cmd and '--term-holder' not in cmd:
            out[p] = name
    return out
def cpu(p):
    f = open(f'/proc/{p}/stat').read().rsplit(')', 1)[1].split()
    return int(f[11]) + int(f[12])
hz = os.sysconf('SC_CLK_TCK'); secs = float(sys.argv[1])
ps = procs(); a = {p: cpu(p) for p in ps}
tot0 = sum(map(int, open('/proc/stat').readline().split()[1:8]))
t0 = time.time(); time.sleep(secs); el = time.time() - t0
tot1 = sum(map(int, open('/proc/stat').readline().split()[1:8]))
for p, n in ps.items():
    try: print(f'{n} {p}: {100.0 * (cpu(p) - a[p]) / hz / el:.1f} % de un núcleo')
    except Exception: print(f'{n} {p}: terminó')
print(f'muestreo {el:.1f} s')
