\ Interpretation semantics

\ Copyright (C) 1996 Free Software Foundation, Inc.

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


\ This file defines a mechanism for specifying special interpretation
\ semantics as well as the interpretation semantics of several words.

table constant interpretation-semantics

:noname ( c-addr u word-ident -- )
    \ word-ident is currently an xt, but it might be the nfa
    sp@ cell interpretation-semantics search-wordlist
    if ( c-addr u word-ident xt )
	nip nip nip execute
    else
	defers interpret-special
    endif ;
IS interpret-special

: interpretation: ( -- colon-sys ) \ gforth
    \G make the last word one with special interpretation semantics and
    \G start the (colon) definition of these semantics.
    \ !! fix reveal such that it is not necessary to do it before the
    \    set-current
    restrict
    lastcfa cell nextname \ !! use nfa instead of cfa
    get-current >r
    interpretation-semantics set-current : reveal
    r> set-current ;

\ : foo
\     ." compilation semantics" ; immediate
\ interpretation:
\     ." interpretation semantics" ;

\ foo			\ interpretation semantics ok
\ : xxx foo ;		\ compilation semantics ok
\ : yyy postpone foo ;	\ ok
\ yyy			\ compilation semantics ok
