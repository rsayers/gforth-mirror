\ paths.fs path file handling                                    03may97jaw

\ Copyright (C) 1995-1997 Free Software Foundation, Inc.

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

0 [IF]

-Changing the search-path:
fpath+ <path> 		adds a directory to the searchpath
fpath= <path>|<path>	makes complete now searchpath
			seperator is |
.fpath			displays the search path
remark I: 
a ~+ in the beginning of filename is expanded to the directory the
current file comes from. ~+ can also be included in the search-path!

remark II:
if there is no sufficient space for the search path increase it!


-Creating custom paths:

It is possible to use the search mechanism on yourself.

Make a buffer for the path:
create mypath	100 chars , 	\ maximum length (is checked)
		0 ,		\ real len
		100 chars allot \ space for path
use the same functions as above with:
mypath path+ 
mypath path=
mypath .path

do a open with the search path:
open-path-file ( adr len path -- fd adr len )
the file is opened read-only; if the file is not found an error is generated

\ questions to: wilke@jwdt.com
[THEN]

[IFUNDEF] +place
: +place ( adr len adr )
        2dup >r >r
        dup c@ char+ + swap move
        r> r> dup c@ rot + swap c! ;
[THEN]

[IFUNDEF] place
: place ( c-addr1 u c-addr2 )
        2dup c! char+ swap move ;
[THEN]

create sourcepath 256 chars , 0 , 256 chars allot
sourcepath avalue fpath

: also-path ( adr len path^ -- )
  >r
  \ len check
  r@ cell+ @ over + r@ @ u> ABORT" path buffer too small!"
  \ copy into
  tuck r@ cell+ dup @ cell+ + swap cmove
  \ make delemiter
  0 r@ cell+ dup @ cell+ + 2 pick + c! 1 + r> cell+ +!
  ;

: only-path ( adr len path^ -- )
  dup 0 swap cell+ ! also-path ;

: path+		name rot also-path ;
: fpath+	fpath path+ ;

: path=        	name 2dup bounds ?DO i c@ '| = IF 0 i c! THEN LOOP
               	rot only-path ;
: fpath=	fpath path= ;

: path>counted  cell+ dup cell+ swap @ ;

: next-path ( adr len -- adr2 len2 )
  2dup 0 scan
  dup 0= IF     2drop 0 -rot 0 -rot EXIT THEN
  >r 1+ -rot r@ 1- -rot
  r> - ;

: privous-path ( path^ -- )
  dup path>counted
  BEGIN tuck dup WHILE repeat ;

: .path
  path>counted
  BEGIN next-path dup WHILE type space REPEAT 2drop 2drop ;

: .fpath fpath .path ;

: absolut-path? ( addr u -- flag ) \ gforth
    \G a path is absolute, if it starts with a / or a ~ (~ expansion),
    \G or if it is in the form ./* or ../*, extended regexp: ^[/~]|./|../
    \G Pathes simply containing a / are not absolute!
    2dup 2 u> swap 1+ c@ ': = and >r \ dos absoulte: c:/....
    over c@ '/ = >r
    over c@ '~ = >r
    2dup 2 min S" ./" compare 0= >r
         3 min S" ../" compare 0=
    r> r> r> r> or or or or ;

Create ofile 0 c, 255 chars allot
Create tfile 0 c, 255 chars allot

: pathsep? dup [char] / = swap [char] \ = or ;

: need/   ofile dup c@ + c@ pathsep? 0= IF s" /" ofile +place THEN ;

: check-path ( adr1 len1 adr2 len2 -- fd 0 | 0 <>0 )
  0 ofile ! >r >r ofile place need/
  r> r> ofile +place
  ofile count r/o open-file ;

: expandtopic
  ofile count 2 min s" ~+" compare 0=
  IF 	ofile count 2 /string tfile place
	0 ofile c! sourcefilename onlypath ofile place need/
	tfile count ofile +place
  THEN ;
	
: onlypath ( adr len -- adr len2 )
  BEGIN dup WHILE 1-
        2dup + c@ pathsep? IF EXIT THEN
  REPEAT ;

: open-path-file ( adr len path -- fd adr1 len2 )
  >r
  2dup absolut-path?
  IF    rdrop
        ofile place expandtopic ofile count r/o open-file throw
        ofile count EXIT
  ELSE  r> path>counted
        BEGIN  next-path dup
        WHILE  5 pick 5 pick check-path
        0= IF >r 2drop 2drop r> ofile count EXIT ELSE drop THEN
  REPEAT
        2drop 2drop 2drop -&38 throw
  THEN ;

: open-fpath-file fpath open-path-file ;
