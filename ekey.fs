\ ekey etc.

\ Copyright (C) 1999 Free Software Foundation, Inc.

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


\ this implementation of EKEY just translates VT100/ANSI escape
\ sequences to ekeys.

\ Caveats: It also translates the sequences if they were not generated
\ by pressing the key; moreover, it waits until a complete sequence or
\ an invalid prefix to a sequence has arrived before reporting true in
\ EKEY? and before completing EKEY.  One way to fix this would be to
\ use timeouts when waiting for the next key; however, this may lead
\ to situations in slow networks where single events result in several
\ EKEYs, which appears less desirable to me.

\ The keycode names are compatible with pfe-0.9.14

: keycode ( "name" -- ; name execution: -- u )
    create ;

keycode k-left
keycode k-right
keycode k-up
keycode k-down
keycode k-home
keycode k-end
\ keycode k-prior \ note: captured by xterm
\ keycode k-next \ note: captured by xterm
keycode k-insert \ not in pfe
\ function/keypad keys
keycode k1
keycode k2
keycode k3
keycode k4
keycode k5
keycode k6
keycode k7
keycode k8
keycode k9
keycode k10
keycode k11 \ not in pfe
keycode k12 \ not in pfe
\ shifted function/keypad keys have the same key sequences (in xterm)
\ and pfe gives the same keycodes; so what are these keycodes good for?
\ keycode s-k1
\ keycode s-k2
\ keycode s-k3
\ keycode s-k4
\ keycode s-k5
\ keycode s-k6
\ keycode s-k7
\ keycode s-k8
\ keycode s-k9
\ keycode s-k10
\ keycode s-k11 \ not in pfe
\ keycode s-k12 \ not in pfe

\ helper word
\ print a key sequence:
\ : key-sequence  ( -- )
\     key begin
\         cr dup . emit
\         key? while
\         key
\     repeat ;

create key-buffer 8 chars allot
2variable key-buffered  key-buffer 0 key-buffered 2!

: char-append-buffer ( c addr -- )
    tuck 2@ chars + c!
    dup 2@ 1+ rot 2! ;

:noname ( -- c )
    \ buffered key
    key-buffered 2@ dup if
	1- 2dup key-buffered 2!
	chars + c@
    else
	2drop defers key
    then ;
is key

: unkey ( c -- )
    key-buffered char-append-buffer ;
    
: unkeys ( addr u -- )
    -1 swap 1- -do
	dup i chars + c@ unkey
	1 -loop
    drop ;

:noname ( -- flag )
    key-buffered 2@ nip 0<> defers key? or ;
is key?

table constant esc-sequences \ and prefixes

create ekey-buffer 8 chars allot
2variable ekey-buffered

27 constant #esc

: esc-prefix ( -- u )
    key ekey-buffered char-append-buffer
    ekey-buffered 2@ esc-sequences search-wordlist
    if
	execute exit
    else
	ekey-buffered 2@ unkeys #esc
    then ;

: esc-sequence ( xt addr u -- ; name execution: -- u ) recursive
    \ define key "name" and all prefixes
    2dup 1- dup
    if
	2dup esc-sequences search-wordlist
	if
	    drop 2drop
	else
	    ['] esc-prefix -rot esc-sequence
	then
    else
	2drop
    then ( xt addr u )
    nextname alias ;

get-current esc-sequences set-current

\ esc sequences (derived by using key-sequence in an xterm)

' k-left	s" [D"	esc-sequence
' k-right	s" [C"	esc-sequence
' k-up		s" [A"	esc-sequence
' k-down	s" [B"	esc-sequence
' k-home	s" [H"	esc-sequence
' k-end		s" [F"	esc-sequence
\ ' k-prior	s" [5~"	esc-sequence \ from linux console
\ ' k-next	s" [6~"	esc-sequence \ from linux console
' k-insert	s" [2~"	esc-sequence

' k1	s" OP"	esc-sequence
' k2	s" OQ"	esc-sequence
' k3	s" OR"	esc-sequence
' k4	s" OS"	esc-sequence
' k5	s" [15~" esc-sequence
' k6	s" [17~" esc-sequence
' k7	s" [18~" esc-sequence
' k8	s" [19~" esc-sequence
' k9	s" [20~" esc-sequence
' k10	s" [21~" esc-sequence
' k11	s" [23~" esc-sequence
' k12	s" [24~" esc-sequence

set-current

: clear-ekey-buffer ( -- )
      ekey-buffer 0 ekey-buffered 2! ;

: ekey ( -- u )
    key dup #esc =
    if
	drop clear-ekey-buffer
	esc-prefix
    then ;

: ekey>char ( u -- u false | c true )
    dup 256 u< ;

: esc? ( -- flag ) recursive
    key? 0=
    if
	false exit
    then
    key ekey-buffered char-append-buffer
    ekey-buffered 2@ esc-sequences search-wordlist
    if
	['] esc-prefix =
	if
	    esc? exit
	then
    then
    true ;

: ekey? ( -- flag )
    key?
    if
	key dup #esc =
	if
	    clear-ekey-buffer esc?
	    ekey-buffered 2@ unkeys
	else
	    true
	then
	swap unkey
    else
	false
    then ;

\ : test-ekey?
\     begin
\ 	begin
\ 	    begin
\ 		key? until
\ 	    ekey? until
\ 	.s ekey .s drop
\     again ;
\ test-ekey?