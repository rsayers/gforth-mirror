\ lib.fs	shared library support package 		11may97py

\ Copyright (C) 1995,1996,1997,1998,2000,2003,2005,2006,2007,2008 Free Software Foundation, Inc.

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

s" os-type" environment? [IF]
    2dup s" linux-gnu" str= [IF] 2drop
	cell 8 = [IF] s" /usr/lib64/libffi.so" [ELSE] s" libffi.so" [THEN]
    [ELSE] 2dup s" bsd" search nip nip [IF] 2drop s" libffi.so"
	[ELSE] 2dup s" cygwin" str= [IF] 2drop s" libffi.dll"
	    [ELSE] 2dup s" darwin" string-prefix? [IF] 2drop s" libffi.dylib"
		[ELSE] 2drop s" libffi" [THEN] [THEN] [THEN] [THEN] [THEN]
open-lib [if]
    .( including libffi.fs )
    include libffi.fs
[ELSE]
    s" libavcall.so"     open-lib 0<>
    s" libcallback.so"   open-lib 0<> and [if]
	.( including fflib.fs [ffcall] )
	include fflib.fs
    [ELSE]
	.( Neither libffi nor ffcall are available ) cr
	abort
        .( Using oldlib.fs; incompatible with fflib.fs and libffi.fs) cr
	include oldlib.fs
    [THEN]
[THEN]

\ testing stuff

[IFUNDEF] libc
    s" os-type" environment? [IF]
	2dup s" linux-gnu" str= [IF]  2drop
	    cell 8 = [IF]
		library libc /lib64/libc.so.6
	    [ELSE]
		library libc /lib/libc.so.6
	    [THEN]
	[ELSE] 2dup s" cygwin" str= [IF]  2drop
		library libc cygwin1.dll
	    [ELSE]  2dup s" bsd" search nip nip [IF]  2drop
		    library libc libc.so
		[ELSE]  2dup s" darwin" string-prefix? [IF]  2drop
			library libc libc.dylib
		    [ELSE]  2drop \ or add your stuff here
		    [THEN]
		[THEN]
	    [THEN]
	[THEN]
    [THEN]
[THEN]

[ifdef] testing

library libc libc.so.6
                
libc sleep int (int) sleep
libc open  ptr int int (int) open
libc lseek int llong int (llong) lseek64
libc read  int ptr int (int) read
libc close int (int) close

library libm libm.so.6

libm fmodf sf sf (sf) fmodf
libm fmod  df df (fp) fmod

\ example for a windows callback
    
callback wincall (int) int int int int callback;

:noname ( a b c d -- e )  2drop 2drop 0 ; wincall do_timer

\ test a callback

callback 2:1 (int) int int callback;

: cb-test ( a b -- c )
    cr ." Testing callback"
    cr ." arguments: " .s
    cr ." result " + .s cr ;
' cb-test 2:1 c_plus

fptr 2:1call int int (int)

: test  c_plus 2:1call ;

\ 3 4 test

\ bigFORTH legacy library test

library libX11 libX11.so.6

legacy on

1 libX11 XOpenDisplay XOpenDisplay    ( name -- dpy )
5 libX11 XInternAtoms XInternAtoms    ( atoms flag count names dpy -- status )

legacy off

[then]    
