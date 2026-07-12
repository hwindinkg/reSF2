#!/bin/bash
# scripts/decompile_original.sh — Decompile functions from ShadowFight2.s86
# Usage: bash scripts/decompile_original.sh <function_address> [output_file]
#
# Requires: radare2 built at /tmp/radare2/
#
# Example:
#   bash scripts/decompile_original.sh 0x10164fa0 playInfo
#   bash scripts/decompile_original.sh 0x10161ad0 model_step

export LD_LIBRARY_PATH=$(find /tmp/radare2 -name 'libr_*.so' -exec dirname {} \; | sort -u | tr '\n' ':') 2>/dev/null
R2=/tmp/radare2/binr/radare2/radare2
SO=/home/z/my-project/reverse/binaries/ShadowFight2.s86

if [ ! -f "$R2" ]; then
    echo "Error: radare2 not found at $R2"
    echo "Build it first: cd /tmp/radare2 && make -j4"
    exit 1
fi

if [ ! -f "$SO" ]; then
    echo "Error: ShadowFight2.s86 not found at $SO"
    exit 1
fi

ADDR=$1
NAME=${2:-func_${ADDR}}

if [ -z "$ADDR" ]; then
    echo "Usage: $0 <function_address> [output_name]"
    echo "Example: $0 0x10164fa0 playInfo"
    exit 1
fi

OUT="/home/z/my-project/scripts/dz_${NAME}_decompiled.c"

echo "Decompiling function at $ADDR..."
$R2 -a x86 -b 32 -e bin.relocs.apply=true -q -c "aaa; af @ $ADDR; pdc @ $ADDR" "$SO" 2>/dev/null > "$OUT"

# Remove INFO/WARN lines
grep -v "^INFO:\|^WARN:" "$OUT" > "${OUT}.tmp" && mv "${OUT}.tmp" "$OUT"

LINES=$(wc -l < "$OUT")
echo "Saved $LINES lines to $OUT"
