\ converts primitives to, e.g., C code 

\ Copyright (C) 1995,1996,1997,1998,2000 Free Software Foundation, Inc.

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


\ This is not very nice (hard limits, no checking, assumes 1 chars = 1)

\ Optimizations:
\ superfluous stores are removed. GCC removes the superfluous loads by itself
\ TOS and FTOS can be kept in register( variable)s.
\ 
\ Problems:
\ The TOS optimization is somewhat hairy. The problems by example:
\ 1) dup ( w -- w w ): w=TOS; sp-=1; sp[1]=w; TOS=w;
\    The store is not superfluous although the earlier opt. would think so
\    Alternatively:    sp[0]=TOS; w=TOS; sp-=1; TOS=w;
\ 2) ( -- .. ): sp[0] = TOS; ... /* This additional store is necessary */
\ 3) ( .. -- ): ... TOS = sp[0]; /* as well as this load */
\ 4) ( -- ): /* but here they are unnecessary */
\ 5) Words that call NEXT themselves have to be done very carefully.
\
\ To do:
\ add the store optimization for doubles
\ regarding problem 1 above: It would be better (for over) to implement
\ 	the alternative

warnings off

[IFUNDEF] vocabulary	\ we are executed just with kernel image
			\ load the rest that is needed
			\ (require fails because this file is needed from a
			\ different directory with the wordlibraries)
include ./search.fs			
include ./extend.fs
[THEN]

[IFUNDEF] environment?
include ./environ.fs
[THEN]

: struct% struct ; \ struct is redefined in gray

include ./gray.fs

100 constant max-effect \ number of things on one side of a stack effect
255 constant maxchar
maxchar 1+ constant eof-char
#tab constant tab-char
#lf constant nl-char

variable rawinput \ pointer to next character to be scanned
variable endrawinput \ pointer to the end of the input (the char after the last)
variable cookedinput \ pointer to the next char to be parsed
variable line \ line number of char pointed to by input
1 line !
2variable filename \ filename of original input file
0 0 filename 2!
2variable f-comment
0 0 f-comment 2!
variable skipsynclines \ are sync lines ("#line ...") invisible to the parser?
skipsynclines on 

: start ( -- addr )
 cookedinput @ ;

: end ( addr -- addr u )
 cookedinput @ over - ;

variable output \ xt ( -- ) of output word

: printprim ( -- )
 output @ execute ;

\ stack types

struct%
    cell% 2* field stack-pointer \ stackpointer name
    cell% 2* field stack-cast \ cast string for assignments to stack elements
    cell%    field stack-in  \ number of stack items in effect in
    cell%    field stack-out \ number of stack items in effect out
end-struct stack%

: make-stack ( addr-ptr u1 addr-cast u2 "stack-name" -- )
    create stack% %allot >r
    save-mem r@ stack-cast 2!
    save-mem r> stack-pointer 2! ;

s" sp" save-mem s" (Cell)" make-stack data-stack 
s" fp" save-mem s" "       make-stack fp-stack
\ !! initialize stack-in and stack-out

\ stack items

struct%
 cell% 2* field item-name   \ name, excluding stack prefixes
 cell%    field item-stack  \ descriptor for the stack used, 0 is default
 cell%    field item-type   \ descriptor for the item type
 cell%    field item-offset \ offset in stack items, 0 for the deepest element
end-struct item%

: init-item ( addr u addr1 -- )
    \ initialize item at addr1 with name addr u
    \ !! remove stack prefix
    dup item% %size erase
    item-name 2! ;

\ various variables for storing stuff of one primitive

2variable forth-name
2variable wordset
2variable c-name
2variable doc
2variable c-code
2variable forth-code
2variable stack-string
create effect-in  max-effect item% %size * allot
create effect-out max-effect item% %size * allot
variable effect-in-end ( pointer )
variable effect-out-end ( pointer )
variable c-line
2variable c-filename
variable name-line
2variable name-filename
2variable last-name-filename

variable primitive-number -10 primitive-number !
Variable function-number 0 function-number !

\ for several reasons stack items of a word are stored in a wordlist
\ since neither forget nor marker are implemented yet, we make a new
\ wordlist for every word and store it in the variable items
variable items

\ a few more set ops

: bit-equivalent ( w1 w2 -- w3 )
 xor invert ;

: complement ( set1 -- set2 )
 empty ['] bit-equivalent binary-set-operation ;

\ the parser

eof-char max-member \ the whole character set + EOF

: getinput ( -- n )
 rawinput @ endrawinput @ =
 if
   eof-char
 else
   cookedinput @ c@
 endif ;

:noname ( n -- )
 dup bl > if
  emit space
 else
  .
 endif ;
print-token !

: testchar? ( set -- f )
 getinput member? ;
' testchar? test-vector !

: checksyncline ( -- )
    \ when input points to a newline, check if the next line is a
    \ sync line.  If it is, perform the appropriate actions.
    rawinput @ >r
    s" #line " r@ over compare 0<> if
	rdrop 1 line +! EXIT
    endif
    0. r> 6 chars + 20 >number drop >r drop line ! r> ( c-addr )
    dup c@ bl = if
	char+ dup c@ [char] " <> abort" sync line syntax"
	char+ dup 100 [char] " scan drop swap 2dup - save-mem filename 2!
	char+
    endif
    dup c@ nl-char <> abort" sync line syntax"
    skipsynclines @ if
	dup char+ rawinput !
	rawinput @ c@ cookedinput @ c!
    endif
    drop ;

: ?nextchar ( f -- )
    ?not? if
	filename 2@ type ." :" line @ 0 .r ." : syntax error, wrong char:"
	getinput . cr
	rawinput @ endrawinput @ over - 100 min type cr
	abort
    endif
    rawinput @ endrawinput @ <> if
	rawinput @ c@
	1 chars rawinput +!
	1 chars cookedinput +!
	nl-char = if
	    checksyncline
	endif
	rawinput @ c@ cookedinput @ c!
    endif ;

: charclass ( set "name" -- )
 ['] ?nextchar terminal ;

: .. ( c1 c2 -- set )
 ( creates a set that includes the characters c, c1<=c<=c2 )
 empty copy-set
 swap 1+ rot do
  i over add-member
 loop ;

: ` ( -- terminal ) ( use: ` c )
 ( creates anonymous terminal for the character c )
 char singleton ['] ?nextchar make-terminal ;

char a char z ..  char A char Z ..  union char _ singleton union  charclass letter
char 0 char 9 ..					charclass digit
bl singleton tab-char over add-member			charclass white
nl-char singleton eof-char over add-member complement	charclass nonl
nl-char singleton eof-char over add-member
    char : over add-member complement                   charclass nocolonnl
bl 1+ maxchar .. char \ singleton complement intersection
                                                        charclass nowhitebq
bl 1+ maxchar ..                                        charclass nowhite
char " singleton eof-char over add-member complement	charclass noquote
nl-char singleton					charclass nl
eof-char singleton					charclass eof


(( letter (( letter || digit )) **
)) <- c-name ( -- )

(( nowhitebq nowhite ** ))
<- name ( -- )

Variable forth-flag
Variable c-flag

(( (( ` f || ` F )) {{ start }} nonl ** 
   {{ end forth-flag @ IF type cr ELSE 2drop THEN }}
)) <- forth-comment ( -- )

(( (( ` c || ` C )) {{ start }} nonl ** 
   {{ end c-flag @ IF type cr ELSE 2drop THEN }}
)) <- c-comment ( -- )

(( ` - nonl ** {{ 
	forth-flag @ IF ." [ELSE]" cr THEN
	c-flag @ IF ." #else" cr THEN }}
)) <- else-comment

(( ` + {{ start }} nonl ** {{ end
	dup
	IF	c-flag @
		IF    ." #ifdef HAS_" bounds ?DO  I c@ toupper emit  LOOP cr
		THEN
		forth-flag @
		IF  ." has? " type ."  [IF]"  cr THEN
	ELSE	2drop
	    c-flag @      IF  ." #endif"  cr THEN
	    forth-flag @  IF  ." [THEN]"  cr THEN
	THEN }}
)) <- if-comment

(( (( forth-comment || c-comment || else-comment || if-comment )) ?? nonl ** )) <- comment-body

(( ` \ comment-body nl )) <- comment ( -- )

(( {{ effect-in }} (( {{ start }} c-name {{ end 2 pick init-item item% %size + }} white ** )) ** {{ effect-in-end ! }}
   ` - ` - white **
   {{ effect-out }} (( {{ start }} c-name {{ end 2 pick init-item item% %size + }} white ** )) ** {{ effect-out-end ! }}
)) <- stack-effect ( -- )

(( {{ s" " doc 2! s" " forth-code 2! }}
   (( {{ line @ name-line ! filename 2@ name-filename 2! }}
      {{ start }} name {{ end 2dup forth-name 2! c-name 2! }}  white ++
      ` ( white ** {{ start }} stack-effect {{ end stack-string 2! }} ` ) white **
        {{ start }} name {{ end wordset 2! }} white **
        (( {{ start }}  c-name {{ end c-name 2! }} )) ??  nl
   ))
   (( ` " ` "  {{ start }} (( noquote ++ ` " )) ++ {{ end 1- doc 2! }} ` " nl )) ??
   {{ skipsynclines off line @ c-line ! filename 2@ c-filename 2! start }} (( nocolonnl nonl **  nl )) ** {{ end c-code 2! skipsynclines on }}
   (( ` :  nl
      {{ start }} (( nonl ++  nl )) ++ {{ end forth-code 2! }}
   )) ?? {{ printprim }}
   (( nl || eof ))
)) <- primitive ( -- )

(( (( comment || primitive || nl )) ** eof ))
parser primitives2something
warnings @ [IF]
.( parser generated ok ) cr
[THEN]

: primfilter ( file-id xt -- )
\ fileid is for the input file, xt ( -- ) is for the output word
 output !
 here dup rawinput ! cookedinput !
 here unused rot read-file throw
 dup here + endrawinput !
 allot
 align
 checksyncline
\ begin
\     getinput dup eof-char = ?EXIT emit true ?nextchar
\ again ;
 primitives2something ;

\ types

struct%
    cell% 2* field type-c-name
    cell%    field type-stack \ default stack
    cell%    field type-size  \ size of type in stack items
    cell%    field type-fetch \ xt of fetch code generator ( item -- )
    cell%    field type-store \ xt of store code generator ( item -- )
end-struct type%

: stack-access ( n stack -- )
    \ print a stack access at index n of stack
    stack-pointer 2@ type
    dup
    if
	." [" 0 .r ." ]"
    else
	drop ." TOS"
    endif ;

: item-in-index ( item -- n )
    \ n is the index of item (in the in-effect)
    >r r@ item-stack @ stack-in @ r> item-offset @ - 1- ;

: fetch-single ( item -- )
 \ fetch a single stack item from its stack
 >r
 r@ item-name 2@ type
 ."  = (" 
 r@ item-type @ type-c-name 2@ type ." ) "
 r@ item-in-index r@ item-stack @ stack-access
 ." ;" cr
 rdrop ; 

: fetch-double ( item -- )
 \ fetch a double stack item from its stack
 >r
 ." FETCH_DCELL("
 r@ item-name 2@ type ." , "
 r@ item-in-index r@ item-stack @ 2dup stack-access
 ." , "                      -1 under+ stack-access
 ." );" cr
 rdrop ;

: same-as-in? ( item -- f )
 \ f is true iff the offset and stack of item is the same as on input
 >r
 r@ item-name 2@ items @ search-wordlist 0=
 abort" bug"
 execute @
 dup r@ =
 if \ item first appeared in output
   drop false
 else
   dup  item-stack  @ r@ item-stack  @ = 
   swap item-offset @ r@ item-offset @ = and
 endif
 rdrop ;

: item-out-index ( item -- n )
    \ n is the index of item (in the in-effect)
    >r r@ item-stack @ stack-out @ r> item-offset @ - 1- ;

: really-store-single ( item -- )
 >r
 r@ item-out-index r@ item-stack @ stack-access ."  = "
 r@ item-stack @ stack-cast 2@ type
 r@ item-name 2@ type ." ;"
 rdrop ;

: store-single ( item -- )
 >r
 r@ same-as-in?
 if
   r@ item-in-index 0= r@ item-out-index 0= xor
   if
       ." IF_" r@ item-stack @ stack-pointer 2@ type
       ." TOS(" r@ really-store-single ." );" cr
   endif
 else
   r@ really-store-single cr
 endif
 rdrop ;

: store-double ( item -- )
\ !! store optimization is not performed, because it is not yet needed
 >r
 ." STORE_DCELL(" r@ item-name 2@ type ." , "
 r@ item-out-index r@ item-stack @ 2dup stack-access
 ." , "                       -1 under+ stack-access
 ." );" cr
 rdrop ;


: single-type ( -- xt1 xt2 n stack )
 ['] fetch-single ['] store-single 1 data-stack ;

: double-type ( -- xt1 xt2 n stack )
 ['] fetch-double ['] store-double 2 data-stack ;

: float-type ( -- xt1 xt2 n stack )
 ['] fetch-single ['] store-single 1 fp-stack ;

: s, ( addr u -- )
\ allocate a string
 here swap dup allot move ;

: starts-with { addr u xt1 xt2 n stack -- } ( "prefix" -- )
    \ describes a type
    \ addr u specifies the C type name
    \ stack effect entries of the type start with prefix
    create type% %allot >r
    addr u save-mem r@ type-c-name 2!
    xt1   r@ type-fetch !
    xt2   r@ type-store !
    n     r@ type-size !
    stack r@ type-stack !
    rdrop ;

wordlist constant types
get-current
types set-current

s" Bool"	single-type starts-with f
s" Char"	single-type starts-with c
s" Cell"	single-type starts-with n
s" Cell"	single-type starts-with w
s" UCell"	single-type starts-with u
s" DCell"	double-type starts-with d
s" UDCell"	double-type starts-with ud
s" Float"	float-type  starts-with r
s" Cell *"	single-type starts-with a_
s" Char *"	single-type starts-with c_
s" Float *"	single-type starts-with f_
s" DFloat *"	single-type starts-with df_
s" SFloat *"	single-type starts-with sf_
s" Xt"		single-type starts-with xt
s" WID"		single-type starts-with wid
s" struct F83Name *"	single-type starts-with f83name

set-current

: get-type ( addr1 u1 -- type-descr )
\ get the type of the name in addr1 u1
\ type-descr is a pointer to a type-descriptor
 0 swap ?do
   dup i types search-wordlist
   if \ ok, we have the type ( addr1 xt )
     execute nip
     UNLOOP EXIT
   endif
 -1 s+loop
 \ we did not find a type, abort
 true abort" unknown type prefix" ;

: declare ( addr "name" -- )
\ remember that there is a stack item at addr called name
 create , ;

: declaration ( item -- )
 dup item-name 2@ items @ search-wordlist
 if \ already declared ( item xt )
     execute @ 2dup item-type @ swap item-type !
     item-stack @ swap item-stack ! \ !! does not generalize to stack prefixes
 else ( addr )
   dup item-name 2@ nextname dup declare ( addr )
   dup >r item-name 2@ 2dup get-type ( addr1 u type-descr )
   dup type-stack @ r@ item-stack ! \ !! only if item-stack @ 0=
   dup r> item-type ! ( addr1 u type-descr )
   type-c-name 2@ type space type ." ;" cr
 endif ;

: declaration-list ( addr1 addr2 -- )
 swap ?do
  i declaration
 item% %size +loop ;

: declarations ( -- )
 wordlist dup items ! set-current
 effect-in effect-in-end @ declaration-list
 effect-out effect-out-end @ declaration-list ;

\ offset computation
\ the leftmost (i.e. deepest) item has offset 0
\ the rightmost item has the highest offset

: compute-offset { item xt -- }
    \ xt specifies in/out; update stack-in/out and set item-offset
    item item-type @ type-size @
    item item-stack @ xt execute dup @ >r +!
    r> item item-offset ! ;

: compute-list ( addr1 addr2 xt -- )
    { xt }
    swap u+do
	i xt compute-offset
    item% %size +loop ;

: clear-stack { -- }
    dup stack-in off stack-out off ;


: compute-offsets ( -- )
    data-stack clear-stack  fp-stack clear-stack
    effect-in  effect-in-end  @ ['] stack-in  compute-list
    effect-out effect-out-end @ ['] stack-out compute-list ;

: flush-a-tos { stack -- }
    stack stack-out @ 0<> stack stack-in @ 0= and
    if
	." IF_" stack stack-pointer 2@ 2dup type ." TOS("
	2dup type ." [0] = " type ." TOS);" cr
    endif ;

: flush-tos ( -- )
    data-stack flush-a-tos
    fp-stack   flush-a-tos ;

: fill-a-tos { stack -- }
    stack stack-out @ 0= stack stack-in @ 0<> and
    if
	." IF_" stack stack-pointer 2@ 2dup type ." TOS("
	2dup type ." TOS = " type ." [0]);" cr
    endif ;

: fill-tos ( -- )
    fp-stack   fill-a-tos
    data-stack fill-a-tos ;

: fetch ( addr -- )
 dup item-type @ type-fetch @ execute ;

: fetches ( -- )
 effect-in-end @ effect-in ?do
   i fetch
 item% %size +loop ; 

: stack-pointer-update { stack -- }
    \ stack grow downwards
    stack stack-in @ stack stack-out @ -
    ?dup-if \ this check is not necessary, gcc would do this for us
	stack stack-pointer 2@ type ."  += " 0 .r ." ;" cr
    endif ;

: stack-pointer-updates ( -- )
    data-stack stack-pointer-update
    fp-stack   stack-pointer-update ;

: store ( item -- )
\ f is true if the item should be stored
\ f is false if the store is probably not necessary
 dup item-type @ type-store @ execute ;

: stores ( -- )
 effect-out-end @ effect-out ?do
   i store
 item% %size +loop ; 

: output-c ( -- ) 
 ." I_" c-name 2@ type ." :	/* " forth-name 2@ type ."  ( " stack-string 2@ type ." ) */" cr
 ." /* " doc 2@ type ."  */" cr
 ." NAME(" [char] " emit forth-name 2@ type [char] " emit ." )" cr \ debugging
 ." {" cr
 ." DEF_CA" cr
 declarations
 compute-offsets \ for everything else
 ." NEXT_P0;" cr
 flush-tos
 fetches
 stack-pointer-updates
 ." {" cr
 ." #line " c-line @ . [char] " emit c-filename 2@ type [char] " emit cr
 c-code 2@ type
 ." }" cr
 ." NEXT_P1;" cr
 stores
 fill-tos
 ." NEXT_P2;" cr
 ." }" cr
 cr
;

: stack-used? { stack -- f }
    stack stack-in @ stack stack-out @ or 0<> ;

: output-funclabel ( -- )
  1 function-number +!
  ." &I_" c-name 2@ type ." ," cr ;

: output-forthname ( -- )
  1 function-number +!
  '" emit forth-name 2@ type '" emit ." ," cr ;

: output-c-func ( -- )
\ used for word libraries
    1 function-number +!
    ." Cell * I_" c-name 2@ type ." (Cell *SP, Cell **FP)      /* " forth-name 2@ type
    ."  ( " stack-string 2@ type ."  ) */" cr
    ." /* " doc 2@ type ."  */" cr
    ." NAME(" [char] " emit forth-name 2@ type [char] " emit ." )" cr
    \ debugging
    ." {" cr
    declarations
    compute-offsets \ for everything else
    data-stack stack-used? IF ." Cell *sp=SP;" cr THEN
    fp-stack   stack-used? IF ." Cell *fp=*FP;" cr THEN
    flush-tos
    fetches
    stack-pointer-updates
    fp-stack   stack-used? IF ." *FP=fp;" cr THEN
    ." {" cr
    ." #line " c-line @ . [char] " emit c-filename 2@ type [char] " emit cr
    c-code 2@ type
    ." }" cr
    stores
    fill-tos
    ." return (sp);" cr
    ." }" cr
    cr ;

: output-label ( -- )  
    ." (Label)&&I_" c-name 2@ type ." ," cr
    -1 primitive-number +! ;

: output-alias ( -- ) 
    ( primitive-number @ . ." alias " ) ." Primitive " forth-name 2@ type cr
    -1 primitive-number +! ;

: output-forth ( -- )  
    forth-code @ 0=
    IF    	\ output-alias
	\ this is bad for ec: an alias is compiled if tho word does not exist!
	\ JAW
    ELSE  ." : " forth-name 2@ type ."   ( "
	stack-string 2@ type ." )" cr
	forth-code 2@ type cr
	-1 primitive-number +!
    THEN ;

: output-tag-file ( -- )
    name-filename 2@ last-name-filename 2@ compare if
	name-filename 2@ last-name-filename 2!
	#ff emit cr
	name-filename 2@ type
	." ,0" cr
    endif ;

: output-tag ( -- )
    output-tag-file
    forth-name 2@ 1+ type
    127 emit
    space forth-name 2@ type space
    1 emit
    name-line @ 0 .r
    ." ,0" cr ;

[IFDEF] documentation
: register-doc ( -- )
    get-current documentation set-current
    forth-name 2@ nextname create
    forth-name 2@ 2,
    stack-string 2@ condition-stack-effect 2,
    wordset 2@ 2,
    c-name 2@ condition-pronounciation 2,
    doc 2@ 2,
    set-current ;
[THEN]

: process-file ( addr u xt -- )
    >r
    2dup filename 2!
    0 function-number !
    r/o open-file abort" cannot open file"
    warnings @ if
	." ------------ CUT HERE -------------" cr  endif
    r> primfilter ;

: process      ( xt -- )
    bl word count rot
    process-file ;
