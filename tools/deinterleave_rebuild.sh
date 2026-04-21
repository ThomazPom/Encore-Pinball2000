#!/usr/bin/env bash
set -euo pipefail

interleave_2bytes() {
    local f1="$1"
    local f2="$2"
    local out="$3"

    if [[ ! -f "$f1" ]]; then
        echo "Fichier introuvable: $f1" >&2
        exit 1
    fi
    if [[ ! -f "$f2" ]]; then
        echo "Fichier introuvable: $f2" >&2
        exit 1
    fi

    python3 -c "
import sys, os

f1, f2, out = sys.argv[1], sys.argv[2], sys.argv[3]
s1, s2 = os.path.getsize(f1), os.path.getsize(f2)
if s1 != s2:
    print(f'Tailles différentes: {f1} ({s1}) / {f2} ({s2})', file=sys.stderr)
    sys.exit(1)
if s1 % 2 != 0:
    print(f'La taille de {f1} n est pas multiple de 2', file=sys.stderr)
    sys.exit(1)

with open(f1,'rb') as a, open(f2,'rb') as b, open(out,'wb') as o:
    CHUNK = 8192
    while True:
        da, db = a.read(CHUNK), b.read(CHUNK)
        if not da:
            break
        buf = bytearray(len(da) + len(db))
        for i in range(0, len(da), 2):
            j = i * 2
            buf[j]   = da[i]
            buf[j+1] = da[i+1]
            buf[j+2] = db[i]
            buf[j+3] = db[i+1]
        o.write(buf)
" "$f1" "$f2" "$out"

    echo "Créé: $out ($(stat -c%s "$out") octets)"
}

echo "Reconstruction des banques interleavées..."

interleave_2bytes "swe1_u100.rom" "swe1_u101.rom" "bank0.bin"
interleave_2bytes "swe1_u102.rom" "swe1_u103.rom" "bank1.bin"
interleave_2bytes "swe1_u104.rom" "swe1_u105.rom" "bank2.bin"
interleave_2bytes "swe1_u106.rom" "swe1_u107.rom" "bank3.bin"

echo "Concaténation finale..."
cat bank0.bin bank1.bin bank2.bin bank3.bin > swe1_rebuilt.bin

echo "Créé: swe1_rebuilt.bin ($(stat -c%s swe1_rebuilt.bin) octets)"
echo "Terminé."
