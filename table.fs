\ table fomerly in search.fs

\ Copyright (C) 1996,1997,1999,2001,2003 Free Software Foundation, Inc.

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

require hash.fs

\ table (case-sensitive wordlist)

: table-find ( addr len wordlist -- nfa / false )
    >r 2dup r> bucket @ (tablelfind) ;

Create tablesearch-map ( -- wordlist-map )
    ' table-find A, ' hash-reveal A, ' (rehash) A, ' (rehash) A,

: table ( -- wid ) \ gforth
    \g Create a case-sensitive wordlist.
    tablesearch-map mappedwordlist ;

