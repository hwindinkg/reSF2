#!/bin/bash
# Decompile DZ decoder functions using radare2 pdc
export LD_LIBRARY_PATH=$(find /tmp/radare2 -name 'libr_*.so' -exec dirname {} \; | sort -u | tr '\n' ':')
R2=/tmp/radare2/binr/radare2/radare2
SO=/home/z/my-project/work/sf2_data/sf2/lib/armeabi-v7a/libs3e_android.so
OUT=/home/z/my-project/scripts/dz_decompiled.c

echo "// DZ decoder decompilation via radare2 pdc" > $OUT
echo "// Source: libs3e_android.so" >> $OUT
echo "" >> $OUT

for addr in 0x37a5c 0x37adc 0x37e28 0x3751c 0x37c44 0x3772c 0x385b4 0x389f8; do
    echo "========================================================" >> $OUT
    echo "// Function at 0x$addr" >> $OUT
    echo "========================================================" >> $OUT
    $R2 -a arm -b 32 -e bin.relocs.apply=true -q -c "aaa; af @ $addr; pdc @ $addr" "$SO" 2>/dev/null >> $OUT
    echo "" >> $OUT
done

wc -l $OUT
echo "Saved to $OUT"
