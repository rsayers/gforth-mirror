\ ASSEMBLER, CODE etc.

\ Copyright (C) 1995 Free Software Foundation, Inc.

\ This file is part of Gforth.

\ Gforth is free software; you can redistribute it and/or
\ modify it under the terms of the GNU General Public License
\ as published by the Free Software Foundation; either version 2
\ of the License, or (at your option) any later version.

\ This program is distributed in the hope that it will be useful,
\ but WITHOUT ANY WARRANTY; without even the implied warranty of
\ MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
\ GNU General Public License for more details.

\ You should have received a copy of the GNU General Public License
\ along with this program; if not, write to the Free Software
\ Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

\ does not include the actual assembler (which is machine-dependent),
\ only words like CODE that are implementation-dependent, but can be
\ defined for all machines.

vocabulary assembler ( -- ) \ tools-ext

: init-asm ( -- )
    also assembler ;

: code ( "name" -- colon-sys )	\ tools-ext
    \ start a native code definition
    header
    threading-method
    if
	here >body cfa,
    then
    defstart init-asm ;

: (;code) ( -- ) \ gforth
    \ execution semantics of @code{;code}
    r> lastxt code-address! ;

:noname ( -- colon-sys )
    align here lastxt code-address!
    defstart init-asm ;
:noname ( colon-sys1 -- colon-sys2 )	\ tools-ext	semicolon-code
    ( create the [;code] part of a low level defining word )
    ;-hook postpone (;code) ?struc postpone [
    defstart init-asm ;
interpret/compile: ;code ( compilation. colon-sys1 -- colon-sys2 )	\ tools-ext	semicolon-code

: end-code ( colon-sys -- )	\ gforth	end_code
    ( end a code definition )
    lastxt here over - flush-icache
    previous ?struc reveal ;
