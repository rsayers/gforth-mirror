\ ANSI.STR      Define terminal attributes              20may93jaw

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


\ If you want another terminal you can redefine
\ the colours.

\ But a better way is it only to redefine SET-ATTR
\ to have compatible colours.

\ Attributes description:
\ <A ( -- -1 )       Entry attributes description
\ >B ( colour -- x ) Colour is Background colour
\ >F ( colour -- x ) Colour is Foreground colour
\                    Attributes may be used freely
\ A> ( -1 x .. x -- attr )
\                    Return over all attribute
\                    only 12 Bits are used up to now!

\ SET-ATTR ( attr -- ) Send attributes to terminal

\ To do:        Make <A A> State smart and compile
\               only literals!

needs vt100.fs

decimal

0 CONSTANT Black
1 CONSTANT Red
2 CONSTANT Green
3 CONSTANT Yellow
4 CONSTANT Blue
5 CONSTANT Brown
6 CONSTANT Cyan
7 CONSTANT White

1 CONSTANT Bold
2 CONSTANT Underline
4 CONSTANT Blink
8 CONSTANT Invers

\ For portable programs don't use invers and underline

: >B    4 lshift ;
: >F    >B >B ;

: B>    4 rshift 15 and ;
: F>    8 rshift 15 and ;

: <A    -1 0 ;
: A>    BEGIN over -1 <> WHILE or REPEAT nip ;

VARIABLE Attr   -1 Attr !

DEFER Attr!

: (Attr!)       ( attr -- ) dup Attr @ = IF drop EXIT THEN
                dup Attr !
                ESC[    0 pn
                        dup F> ?dup IF 30 + ;pn THEN
                        dup B> ?dup IF 40 + ;pn THEN
                        dup Bold and IF 1 ;pn THEN
                        dup Underline and IF 4 ;pn THEN
                        dup Blink and IF 5 ;pn THEN
                        Invers and IF 7 ;pn THEN
                        [char] m emit ;

' (Attr!) IS Attr!

: BlackSpace    Attr @ dup B> Black =
                IF drop space
                ELSE 0 attr! space attr! THEN ;

