#!/bin/sh
# doc_evil.sh — extracción segura de lord-doc con comprimidos maliciosos y prueba del encierro.
# Uso (en la VM, desde server/): sh tests/doc_evil.sh
# Necesita build/lord-doc y build/doc-test (make build/lord-doc build/doc-test).
set -u
T=$(mktemp -d /tmp/lord-doc-evil.XXXXXX)
python3 -I tests/make_evil_archives.py "$T/arch" >/dev/null
fails=0; total=0
check() { total=$((total+1)); if eval "$2"; then :; else fails=$((fails+1)); echo "FALLA: $1"; fi; }

for a in slip.zip absoluta.tar enlace.tar cadena.tar.gz duro.tar especiales.tar.xz repetido.tar sano.zip basura.zip suelto.txt.gz; do
  d="$T/dest-$a/adentro"; mkdir -p "$d"
  build/doc-test build/lord-doc extract "$T/arch/$a" "$d" > "$T/$a.out" 2>&1
  echo "== $a: $(tail -1 "$T/$a.out" | cut -c1-300)"
done
# nada fuera de las carpetas de destino
check "archivos FUERA-* afuera" '[ -z "$(find "$T" -name "FUERA-*" -not -path "*/adentro/*" 2>/dev/null; find /tmp -maxdepth 1 -name "FUERA-*")" ]'
check "/tmp/FUERA-abs.txt" '[ ! -e /tmp/FUERA-abs.txt ]'
check "/etc/FUERA-etc.txt" '[ ! -e /etc/FUERA-etc.txt ]'
# lo sano sí se extrajo, lo malo se salteó
check "slip: ok.txt" '[ -f "$T/dest-slip.zip/adentro/ok.txt" ]'
check "slip: avisos" 'grep -q unsafe-path "$T/slip.zip.out"'
check "absoluta: ok.txt" '[ -f "$T/dest-absoluta.tar/adentro/ok.txt" ]'
check "enlace: salida y etc no son enlaces" '[ ! -L "$T/dest-enlace.tar/adentro/salida" ] && [ ! -L "$T/dest-enlace.tar/adentro/etc" ]'
check "cadena: l2 no es enlace" '[ ! -L "$T/dest-cadena.tar.gz/adentro/d/l2" ]'
check "duro: sin /etc/passwd" '[ ! -e "$T/dest-duro.tar/adentro/passwd" ]'
check "especiales: sin dispositivo ni FIFO" '[ ! -e "$T/dest-especiales.tar.xz/adentro/null" ] && [ ! -e "$T/dest-especiales.tar.xz/adentro/fifo" ]'
check "especiales: sin setuid" '[ -f "$T/dest-especiales.tar.xz/adentro/suid" ] && [ "$(stat -c %a "$T/dest-especiales.tar.xz/adentro/suid")" = 755 ]'
check "repetido: no pisa" '[ "$(cat "$T/dest-repetido.tar/adentro/mismo.txt")" = uno ] && grep -q exists "$T/repetido.tar.out"'
check "sano: escrituras" '[ "$(cat "$T/dest-sano.zip/adentro/carpeta/日本語.txt")" = こんにちは ] && [ -d "$T/dest-sano.zip/adentro/carpeta/sub/vacío" ]'
check "basura: error" 'grep -q "^err" "$T/basura.zip.out"'
check "suelto: raw" '[ "$(cat "$T/dest-suelto.txt.gz/adentro/raw.bin")" = "texto comprimido suelto" ]'
# el encierro
build/doc-test build/lord-doc selftest > "$T/self.out" 2>&1
# el mismo encierro en el modo de extracción (con una carpeta de escritura)
mkdir -p "$T/rw"
build/doc-test build/lord-doc selftest-arc "$T/rw" > "$T/self-arc.out" 2>&1
echo "== selftest (extraer): $(tail -1 "$T/self-arc.out")"
check "extraer: no lee ~/.ssh" 'grep -q "\"ssh\":[1-9]" "$T/self-arc.out"'
check "extraer: no escribe /tmp" 'grep -q "\"write_tmp\":[1-9]" "$T/self-arc.out"'
check "extraer: sin sockets" 'grep -q "\"unix_socket\":[1-9]" "$T/self-arc.out" && grep -q "\"inet\":[1-9]" "$T/self-arc.out"'

echo "== selftest: $(cat "$T/self.out")"
check "no lee ~/.ssh" 'grep -q "\"ssh\":[1-9]" "$T/self.out"'
check "no lee la carpeta personal" 'grep -q "\"home_file\":[1-9]" "$T/self.out"'
check "no escribe /tmp" 'grep -q "\"write_tmp\":[1-9]" "$T/self.out"'
check "sin red (AF_INET)" 'grep -q "\"inet\":[1-9]" "$T/self.out"'
check "sin sockets (ni unix: D-Bus, CUPS, Wayland)" 'grep -q "\"unix_socket\":[1-9]" "$T/self.out"'
check "sin fork" 'grep -q "\"fork\":[1-9]" "$T/self.out"'
check "lee las fuentes" 'grep -q "\"fonts_ok\":0" "$T/self.out"'
# formatos: 7z, tar.xz, tar.zst, tar.gz y zip (si están bsdtar y zstd para armarlos)
if command -v bsdtar >/dev/null && command -v zstd >/dev/null; then
  mkdir -p "$T/fmt/src/sub"; echo hola > "$T/fmt/src/a.txt"; echo 日本語 > "$T/fmt/src/sub/b.txt"
  ( cd "$T/fmt" && bsdtar -cf t.7z --format 7zip -C src . && tar -C src -cJf t.tar.xz . && tar -C src --zstd -cf t.tar.zst . \
    && tar -C src -czf t.tar.gz . && bsdtar -cf t.zip --format zip -C src . )
  for f in t.7z t.tar.xz t.tar.zst t.tar.gz t.zip; do
    mkdir -p "$T/fmt/out-$f"
    build/doc-test build/lord-doc extract "$T/fmt/$f" "$T/fmt/out-$f" > "$T/fmt/$f.out" 2>&1
    check "formato $f" '[ "$(cat "$T/fmt/out-$f/sub/b.txt")" = 日本語 ] && grep -q "\"skipped\":0" "$T/fmt/$f.out"'
  done
fi
# guardar una copia de un PDF con anotaciones (el ayudante sigue vivo al revisar el archivo)
if [ -f /usr/share/cups/data/form_english.pdf ]; then
  build/doc-test build/lord-doc save /usr/share/cups/data/form_english.pdf "$T/copia.pdf" > "$T/save.out" 2>&1
  check "PDF: copia completa con anotaciones" 'grep -q "completa$" "$T/save.out" && pdfinfo "$T/copia.pdf" >/dev/null 2>&1'
fi
echo "doc-evil: $((total-fails))/$total"
rm -rf "$T"
[ "$fails" = 0 ]
