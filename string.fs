\ dynamic string handling                              10aug99py

\ Copyright (C) 2000,2005,2007,2010,2011 Free Software Foundation, Inc.

\ This file is part of Gforth.

\ Gforth is free software; you can redistribute it and/or
\ modify it under the terms of the GNU General Public License
\ as published by the Free Software Foundation, either version 3
\ of the License, or (at your option) any later version.

\ This program is distributed in the hope that it will be useful,
\ but WITHOUT ANY WARRANTY; without even the implied warranty of
\ MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
\ GNU General Public License for more details.

\ You should have received a copy of the GNU General Public License
\ along with this program. If not, see http://www.gnu.org/licenses/.

[IFUNDEF] $!
: delete   ( buffer size n -- ) \ gforth-string
    \G deletes the first @var{n} bytes from a buffer and fills the
    \G rest at the end with blanks.
    over min >r  r@ - ( left over )  dup 0>
    IF  2dup swap dup  r@ +  -rot swap move  THEN  + r> bl fill ;

: insert   ( string length buffer size -- ) \ gforth-string
    \G inserts a string at the front of a buffer. The remaining
    \G bytes are moved on.
    rot over min >r  r@ - ( left over )
    over dup r@ +  rot move   r> move  ;

: $padding ( n -- n' ) \ gforth-string
    [ 6 cells ] Literal + [ -4 cells ] Literal and ;
: $! ( addr1 u addr2 -- ) \ gforth-string string-store
    \G stores a string at an address, If there was a string there
    \G already, that string will be lost.
    dup @ IF  dup @ free throw  THEN
    over $padding allocate throw over ! @
    over >r  rot over cell+  r> move 2dup ! + cell+ bl swap c! ;
: $@len ( addr -- u ) \ gforth-string string-fetch-len
    \G returns the length of the stored string.
    @ @ ;
: $@ ( addr1 -- addr2 u ) \ gforth-string string-fetch
    \G returns the stored string.
    @ dup cell+ swap @ ;
: $!len ( u addr -- ) \ gforth-string string-store-len
    \G changes the length of the stored string.  Therefore we must
    \G change the memory area and adjust address and count cell as
    \G well.
    over $padding over @ swap resize throw over ! @ ! ;
: $del ( addr off u -- ) \ gforth-string string-del
    \G deletes @var{u} bytes from a string with offset @var{off}.
    >r >r dup $@ r> /string r@ delete
    dup $@len r> - swap $!len ;
: $ins ( addr1 u addr2 off -- ) \ gforth-string string-ins
    \G inserts a string at offset @var{off}.
    >r 2dup dup $@len rot + swap $!len  $@ 1+ r> /string insert ;
: $+! ( addr1 u addr2 -- ) \ gforth-string string-plus-store
    \G appends a string to another.
    dup $@len $ins ;
: $off ( addr -- ) \ gforth-string string-off
    \G releases a string.
    dup @ dup IF  free throw off  ELSE  2drop  THEN ;
: $init ( addr -- ) \ gforth-string string-init
    \G initializes a string to empty (doesn't look at what was there before).
    >r r@ off s" " r> $! ;

\ dynamic string handling                              12dec99py

: $split ( addr u char -- addr1 u1 addr2 u2 ) \ gforth-string string-split
    \G divides a string into two, with one char as separator (e.g. '?'
    \G for arguments in an HTML query)
    >r 2dup r> scan dup >r dup IF  1 /string  THEN
    2swap r> - 2swap ;

: $iter ( .. $addr char xt -- .. ) \ gforth-string string-iter
    \G takes a string apart piece for piece, also with a character as
    \G separator. For each part a passed token will be called. With
    \G this you can take apart arguments -- separated with '&' -- at
    \G ease.
    >r >r
    $@ BEGIN  dup  WHILE  r@ $split i' -rot >r >r execute r> r>
    REPEAT  2drop rdrop rdrop ;

: $over ( addr u $addr off -- )
    \G overwrite string at offset off with addr u
    swap >r
    r@ @ 0= IF  s" " r@ $!  THEN
    2dup + r@ $@len > IF
	2dup + r@ $@len tuck max r@ $!len
	r@ $@ rot /string bl fill
    THEN
    r> $@ rot /string rot umin move ;

\ string array words

: $[] ( n addr -- addr' ) >r
    \G index into the string array and return the address at index n
    r@ @ 0= IF  s" " r@ $!  THEN
    r@ $@ 2 pick cells /string
    dup cell < IF
	2drop r@ $@len
	over 1+ cells r@ $!len
	r@ $@ rot /string 0 fill
	r@ $@ 2 pick cells /string
    THEN  drop nip rdrop ;

: $[]! ( addr u n $[]addr -- )  $[] $! ;
\G store a string into an array at index n
: $[]+! ( addr u n $[]addr -- )  $[] $+! ;
\G add a string to the string at addr n
: $[]@ ( n $[]addr -- addr u )  $[] dup @ IF $@ ELSE drop s" " THEN ;
\G fetch a string from array index n -- return the zero string if empty
[THEN]