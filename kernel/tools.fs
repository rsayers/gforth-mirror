\ TOOLS.FS     Toolkit extentions                      2may93jaw

\ Copyright (C) 1995,1998 Free Software Foundation, Inc.

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

\ May be cross-compiled

hex

\ .S            CORE / CORE EXT                         9may93jaw

variable maxdepth-.s
9 maxdepth-.s !

: .s ( -- ) \ tools dot-s
    \G Display the number of items on the data stack,
    \G followed by a list of the items; TOS is the right-most item.
    ." <" depth 0 .r ." > "
    depth 0 max maxdepth-.s @ min
    dup 0
    ?do
	dup i - pick .
    loop
    drop ;

\ DUMP                       2may93jaw - 9may93jaw    06jul93py
\ looks very nice, I know

Variable /dump

: .4 ( addr -- addr' )
    3 FOR  -1 /dump +!  /dump @ 0<
        IF  ."    "  ELSE  dup c@ 0 <<# # # #> type #>> space  THEN
    char+ NEXT ;
: .chars ( addr -- )
    /dump @ bounds
    ?DO I c@ dup 7f bl within
	IF  drop [char] .  THEN  emit
    LOOP ;

: .line ( addr -- )
  dup .4 space .4 ." - " .4 space .4 drop  10 /dump +!  space .chars ;

: dump  ( addr u -- ) \ tools dump
    \G Display @var{u} lines of memory starting at address @var{addr}. Each line
    \G displays the contents of 16 bytes. When Gforth is running under
    \G an operating system you may get @file{Invalid memory address} errors
    \G if you attempt to access arbitrary locations.
    cr base @ >r hex        \ save base on return stack
    0 ?DO  I' I - 10 min /dump !
	dup 8 u.r ." : " dup .line cr  10 +
	10 +LOOP
    drop r> base ! ;

\ ?                                                     17may93jaw

: ? ( a-addr -- ) \ tools question
    \G Display the contents of address @var{a-addr} in the current number base.
    @ . ;

\ words visible in roots                               14may93py

include  ../termsize.fs

: words ( -- ) \ tools
    \G ** this will not get annotated. See other defn in search.fs .. **
    cr 0 context @ wordlist-id
    BEGIN
	@ dup
    WHILE
	2dup name>string nip 2 + dup >r +
	cols >=
	IF
	    cr nip 0 swap
	THEN
	dup name>string type space r> rot + swap
    REPEAT
    2drop ;

' words alias vlist ( -- ) \ gforth
\g Old (pre-Forth-83) name for @code{WORDS}.

