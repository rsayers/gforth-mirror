#!/bin/bash
GFORTHD="./gforth-ditc -p .:." GFORTH="./gforth-ditc --die-on-signal -p .:. -i kernl32l.fi exboot.fs startup.fs arch/386/asm.fs arch/386/disasm.fs" includedir=`pwd`/include bindir=`pwd` libccdir=`pwd`/lib/gforth/0.7.0-20081108/libcc-named/ ./gforthmi gforth.fi  --die-on-signal -p ".:~+:." -i kernl32l.fi exboot.fs startup.fs arch/386/asm.fs arch/386/disasm.fs
