\ MAIN.FS      Kernal main load file                   20may93jaw

\ : include bl word count included ;
\ we want write include...

\ : : ( -- colon-sys )  Header [ ' : @ ] ALiteral cfa, 0 ] ;
\ : ; ( colon-sys -- )  ?struc postpone exit reveal postpone [ ; immediate
\ : :noname ( -- xt colon-sys )  here [ ' : @ ] ALiteral cfa, 0 ] ;

include search-order.fs

include cross.fs               \ include cross-compiler

decimal

128 KB makekernal , 0 , 0 , 0 A,

UNLOCK ghost - drop \ ghost must exist because - would be treated as number
LOCK

0 AConstant forthstart

include aliases.fs             \ include primitive aliases
\ include cond.fs                \ conditional compile
\ include patches.fs             \ include primitive patches

include vars.fs                \ variables and other stuff
include add.fs                 \ additional things
include environ.fs             \ create environmental query strings
include errore.fs
include kernal.fs              \ load kernal
include extend.fs              \ load core-extended
include tools.fs               \ load tools ( .s dump )
\ include words.fs
\ include wordinfo.fs
\ include see.fs                 \ load see
include toolsext.fs
\ include search-order.fs

\ Setup                                                13feb93py

here normal-dp !
tudp H @ minimal udp !
decimal

\ 64 KB       0 cells !  \ total Space... defined above!
here         1 cells !  \ Size of the system
  2 KB       2 cells !  \ Return and fp stack size
' boot >body 3 cells !  \ Entry point

UNLOCK Tlast @
LOCK
1 cells - dup forth-wordlist ! Last !
.unresolved

cr cr 
endian [IF]
   save-cross kernal.32bimg
[ELSE]
   save-cross kernal.32limg
[THEN] cr

bye
