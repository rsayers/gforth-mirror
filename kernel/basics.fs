\ kernel.fs    GForth kernel                        17dec92py

\ Copyright (C) 1995,1998,2000 Free Software Foundation, Inc.

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
\ Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.

\ Idea and implementation: Bernd Paysan (py)

\ Needs:

require ./vars.fs

hex

\ labels for some code addresses

\- NIL NIL AConstant NIL \ gforth

\ Aliases

[IFUNDEF] r@
' i Alias r@ ( -- w ; R: w -- w ) \ core r-fetch
[THEN]

\ !! this is machine-dependent, but works on all but the strangest machines

: maxaligned ( addr1 -- addr2 ) \ gforth
    \G @i{addr2} is the first address after @i{addr1} that satisfies
    \G all alignment restrictions.
    [ /maxalign 1 - ] Literal + [ 0 /maxalign - ] Literal and ;
\ !! machine-dependent and won't work if "0 >body" <> "0 >body
    \G maxaligned"
' maxaligned Alias cfaligned ( addr1 -- addr2 ) \ gforth
\G @i{addr2} is the first address after @i{addr1} that is aligned for
\G a code field (i.e., such that the corresponding body is maxaligned).

: chars ( n1 -- n2 ) \ core
\G @i{n2} is the number of address units of @i{n1} chars.""
; immediate


\ : A!    ( addr1 addr2 -- ) \ gforth
\    dup relon ! ;
\ : A,    ( addr -- ) \ gforth
\    here cell allot A! ;
' ! alias A! ( addr1 addr2 -- ) \ gforth

\ UNUSED                                                17may93jaw

has? ec 
[IF]
unlock ram-dictionary borders nip lock
AConstant dictionary-end
[ELSE]
: dictionary-end ( -- addr )
    forthstart [ 3 cells image-header + ] Aliteral @ + ;
[THEN]

: usable-dictionary-end ( -- addr )
    dictionary-end [ word-pno-size pad-minsize + ] Literal - ;

: unused ( -- u ) \ core-ext
    \G Return the amount of free space remaining (in address units) in
    \G the region addressed by @code{here}.
    usable-dictionary-end here - ;

\ here is used for pad calculation!

: dp    ( -- addr ) \ gforth
    dpp @ ;
: here  ( -- addr ) \ core
    \G Return the address of the next free location in data space.
    dp @ ;

\ on off                                               23feb93py

\ on is used by docol:
: on  ( a-addr -- ) \ gforth
    \G Set the (value of the) variable  at @i{a-addr} to @code{true}.
    true  swap ! ;
: off ( a-addr -- ) \ gforth
    \G Set the (value of the) variable at @i{a-addr} to @code{false}.
    false swap ! ;

\ dabs roll                                           17may93jaw

: dabs ( d -- ud ) \ double d-abs
    dup 0< IF dnegate THEN ;

: roll  ( x0 x1 .. xn n -- x1 .. xn x0 ) \ core-ext
  dup 1+ pick >r
  cells sp@ cell+ dup cell+ rot move drop r> ;

\ place bounds                                         13feb93py

: place  ( addr len to -- ) \ gforth
    over >r  rot over 1+  r> move c! ;
: bounds ( addr u -- addr+u addr ) \ gforth
    \G Given a memory block represented by starting address @i{addr}
    \G and length @i{u} in aus, produce the end address @i{addr+u} and
    \G the start address in the right order for @code{u+do} or
    \G @code{?do}.
    over + swap ;

\ (word)                                               22feb93py

: scan   ( addr1 n1 char -- addr2 n2 ) \ gforth
    \ skip all characters not equal to char
    >r
    BEGIN
	dup
    WHILE
	over c@ r@ <>
    WHILE
	1 /string
    REPEAT  THEN
    rdrop ;
: skip   ( addr1 n1 char -- addr2 n2 ) \ gforth
    \ skip all characters equal to char
    >r
    BEGIN
	dup
    WHILE
	over c@ r@  =
    WHILE
	1 /string
    REPEAT  THEN
    rdrop ;

\ digit?                                               17dec92py

: digit?   ( char -- digit true/ false ) \ gforth
  base @ $100 =
  IF
    true EXIT
  THEN
  toupper [char] 0 - dup 9 u> IF
    [ char A char 9 1 + -  ] literal -
    dup 9 u<= IF
      drop false EXIT
    THEN
  THEN
  dup base @ u>= IF
    drop false EXIT
  THEN
  true ;

: accumulate ( +d0 addr digit - +d1 addr )
  swap >r swap  base @  um* drop rot  base @  um* d+ r> ;

: >number ( ud1 c-addr1 u1 -- ud2 c-addr2 u2 ) \ core to-number
    \G Attempt to convert the character string @var{c-addr1 u1} to an
    \G unsigned number in the current number base. The double
    \G @var{ud1} accumulates the result of the conversion to form
    \G @var{ud2}. Conversion continues, left-to-right, until the whole
    \G string is converted or a character that is not convertable in
    \G the current number base is encountered (including + or -). For
    \G each convertable character, @var{ud1} is first multiplied by
    \G the value in @code{BASE} and then incremented by the value
    \G represented by the character. @var{c-addr2} is the location of
    \G the first unconverted character (past the end of the string if
    \G the whole string was converted). @var{u2} is the number of
    \G unconverted characters in the string. Overflow is not detected.
    0
    ?DO
	count digit?
    WHILE
	accumulate
    LOOP
        0
    ELSE
	1- I' I -
	UNLOOP
    THEN ;

\ s>d um/mod						21mar93py

: s>d ( n -- d ) \ core		s-to-d
    dup 0< ;

: ud/mod ( ud1 u2 -- urem udquot ) \ gforth
    >r 0 r@ um/mod r> swap >r
    um/mod r> ;

\ catch throw                                          23feb93py

has? glocals [IF]
: lp@ ( -- addr ) \ gforth	lp-fetch
 laddr# [ 0 , ] ;
[THEN]

defer catch ( x1 .. xn xt -- y1 .. ym 0 / z1 .. zn error ) \ exception
\G @code{Executes} @i{xt}.  If execution returns normally,
\G @code{catch} pushes 0 on the stack.  If execution returns through
\G @code{throw}, all the stacks are reset to the depth on entry to
\G @code{catch}, and the TOS (the @i{xt} position) is replaced with
\G the throw code.

:noname ( ... xt -- ... 0 )
    execute 0 ;
is catch

defer throw ( y1 .. ym nerror -- y1 .. ym / z1 .. zn error ) \ exception
\G If @i{nerror} is 0, drop it and continue.  Otherwise, transfer
\G control to the next dynamically enclosing exception handler, reset
\G the stacks accordingly, and push @i{nerror}.

:noname ( y1 .. ym error -- y1 .. ym / z1 .. zn error )
    ?dup if
	[ has? ec 0= [IF] here image-header 9 cells + ! [THEN] ]
	cr .error cr
	[ has? file [IF] ] script? IF  1 (bye)  ELSE  quit  THEN
	[ [ELSE] ] quit [ [THEN] ]
    then ;
is throw

\ (abort")

: (abort")
    "lit >r
    IF
	r> "error ! -2 throw
    THEN
    rdrop ;

: abort ( ?? -- ?? ) \ core,exception-ext
    \G @code{-1 throw}.
    -1 throw ;

\ ?stack                                               23feb93py

: ?stack ( ?? -- ?? ) \ gforth
    sp@ sp0 @ u> IF    -4 throw  THEN
[ has? floating [IF] ]
    fp@ fp0 @ u> IF  -&45 throw  THEN
[ [THEN] ]
;
\ ?stack should be code -- it touches an empty stack!

\ DEPTH                                                 9may93jaw

: depth ( -- +n ) \ core depth
    \G @var{+n} is the number of values that were on the data stack before
    \G @var{+n} itself was placed on the stack.
    sp@ sp0 @ swap - cell / ;

: clearstack ( ... -- ) \ gforth clear-stack
    \G remove and discard all/any items from the data stack.
    sp0 @ sp! ;

\ Strings						 22feb93py

: "lit ( -- addr )
  r> r> dup count + aligned >r swap >r ;

\ */MOD */                                              17may93jaw

\ !! I think */mod should have the same rounding behaviour as / - anton
: */mod ( n1 n2 n3 -- n4 n5 ) \ core	star-slash-mod
    \G n1*n2=n3*n5+n4, with the intermediate result (n1*n2) being double.
    >r m* r> sm/rem ;

: */ ( n1 n2 n3 -- n4 ) \ core	star-slash
    \G n4=(n1*n2)/n3, with the intermediate result being double.
    */mod nip ;

\ HEX DECIMAL                                           2may93jaw

: decimal ( -- ) \ core
    \G Set @code{base} to &10 (decimal).
    a base ! ;
: hex ( -- ) \ core-ext
    \G Set @code{base} to &16 (hexadecimal).
    10 base ! ;

