\ ???

\ Copyright (C) 1998 Free Software Foundation, Inc.

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

\ for 16 bit machines only

[IFUNDEF] 8>>
: 8>> 8 rshift ;
[THEN]

: .digit
  $0f and
   dup 9 u>
   IF   
        [ char A char 9 - 1- ] Literal +
   THEN 
  '0 + (emit) ;

: .w
	dup 8>> 2/ 2/ 2/ 2/ .digit
	dup 8>> .digit
	dup 2/ 2/ 2/ 2/ .digit
	.digit ;

: .x 	
	dup 8>> 8>> .w .w $20 (emit) ;

\ !! depth reibauen

: .sx
  \ SP@ SP0 @ swap - 2/ 
  depth
  dup '< emit .x '> emit dup
  0 ?DO dup pick .x 1- LOOP drop ;
