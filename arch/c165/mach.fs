
    2 Constant cell
    1 Constant cell<<
    4 Constant cell>bit
    8 Constant bits/byte
    8 Constant float
    2 Constant /maxalign
 false Constant bigendian
( true=big, false=little )

false Constant NIL  \ relocating

: prims-include  ." Include primitives" cr s" arch/c165/prim.fs" included ;
: asm-include    ." Include assembler" cr s" arch/c165/asm.fs" included ;
: >boot ;

>ENVIRON

false Constant files
false Constant OS
false Constant prims
false Constant floats
false Constant locals
false Constant dcomps
false Constant hash
false Constant xconds
false Constant header
true Constant interpreter
true Constant crlf
true Constant ITC
true Constant ec
\ true Constant rom
