\ require.fs

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

\ Now: Kernel Module, Reloadable

\ $Id$

create included-files 0 , 0 , ( pointer to and count of included files )
here ," the terminal" dup c@ swap 1 + swap , A, here 2 cells -
create image-included-files  1 , A, ( pointer to and count of included files )
\ included-files points to ALLOCATEd space, while image-included-files
\ points to ALLOTed objects, so it survives a save-system

: loadfilename ( -- a-addr )
    \G a-addr 2@ produces the current file name ( c-addr u )
    included-files 2@ loadfilename# @ min 2* cells + ;

: sourcefilename ( -- c-addr u ) \ gforth
    \G the name of the source file which is currently the input
    \G source.  The result is valid only while the file is being
    \G loaded.  If the current input source is no (stream) file, the
    \G result is undefined.
    loadfilename 2@ ;

: sourceline# ( -- u ) \ gforth		sourceline-number
    \G the line number of the line that is currently being interpreted
    \G from a (stream) file. The first line has the number 1. If the
    \G current input source is no (stream) file, the result is
    \G undefined.
    loadline @ ;

: init-included-files ( -- )
    image-included-files 2@ 2* cells save-mem drop ( addr )
    image-included-files 2@ nip included-files 2! ;

: included? ( c-addr u -- f ) \ gforth
    \G true, iff filename c-addr u is in included-files
    included-files 2@ 0
    ?do ( c-addr u addr )
	dup >r 2@ 2over compare 0=
	if
	    2drop rdrop unloop
	    true EXIT
	then
	r> cell+ cell+
    loop
    2drop drop false ;

: add-included-file ( c-addr u -- ) \ gforth
    \G add name c-addr u to included-files
    included-files 2@ 2* cells 2 cells extend-mem
    2/ cell / included-files 2!
    2! ;

: included1 ( i*x file-id c-addr u -- j*x ) \ gforth
    \G include the file file-id with the name given by c-addr u
    loadfilename# @ >r
    save-mem add-included-file ( file-id )
    included-files 2@ nip 1- loadfilename# !
    ['] include-file catch
    r> loadfilename# !
    throw ;
    
: included ( i*x addr u -- j*x ) \ file
    open-fpath-file throw included1 ;

: required ( i*x addr u -- j*x ) \ gforth
    \G include the file with the name given by addr u, if it is not
    \G included already. Currently this works by comparing the name of
    \G the file (with path) against the names of earlier included
    \G files; however, it would probably be better to fstat the file,
    \G and compare the device and inode. The advantages would be: no
    \G problems with several paths to the same file (e.g., due to
    \G links) and we would catch files included with include-file and
    \G write a require-file.
    open-fpath-file throw 2dup included?
    if
	2drop close-file throw
    else
	included1
    then ;

\ INCLUDE                                               9may93jaw

: include  ( "file" -- ) \ gforth
\G includes a file and keeps in mind that this file is loaded
  name included ;

: require  ( "file" -- ) \ gforth
\G load a file only when it is not included already
  name required ;

0 [IF]
: \I
  here 
  0 word count
  string,
  needsrcs^ @ ! ;

: .modules
  cr
  needs^ @
  BEGIN		dup 
  WHILE		dup cell+ count type cr
		5 spaces
		dup cell+ count + aligned
		@ dup IF count type ELSE drop THEN cr
		@
  REPEAT
  drop ;

: loadfilename#>str ( n -- adr len )
\ this converts the filenumber into the string
  loadfilenamecount @ swap -
  needs^ @
  swap 0 ?DO dup 0= IF LEAVE THEN @ LOOP 
  dup IF cell+ count ELSE drop s" NOT FOUND" THEN ;
[THEN]

: loadfilename#>str ( n -- adr len )
    included-files 2@ drop swap 2* cells + 2@ ;

: .modules
    included-files 2@ 2* cells bounds ?DO
	cr I 2@ type  2 cells +LOOP ;  

\ contains tools/newrequire.fs
\ \I $Id$

