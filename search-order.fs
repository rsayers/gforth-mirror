\ search order wordset                                 14may93py

$10 constant maxvp
Variable vp
  0 A, 0 A,  0 A, 0 A,   0 A, 0 A,   0 A, 0 A, 
  0 A, 0 A,  0 A, 0 A,   0 A, 0 A,   0 A, 0 A, 

: get-current  ( -- wid )  current @ ;
: set-current  ( wid -- )  current ! ;

: context ( -- addr )  vp dup @ cells + ;
: definitions  ( -- )  context @ current ! ;

\ wordlist Vocabulary also previous                    14may93py

AVariable voclink

Defer 'initvoc
' drop IS 'initvoc

Variable slowvoc   slowvoc off

: wordlist  ( -- wid )
  here  0 A, Forth-wordlist cell+ @ A, voclink @ A, slowvoc @ A,
  dup 2 cells + dup voclink ! 'initvoc ;

: Vocabulary ( -- ) Create wordlist drop  DOES> context ! ;

: also  ( -- )
  context @ vp @ 1+ dup maxvp > abort" Vocstack full"
  vp ! context ! ;

: previous ( -- )  vp @ 1- dup 0= abort" Vocstack empty" vp ! ;

\ vocabulary find                                      14may93py

: (vocfind)  ( addr count nfa1 -- nfa2|false )
    \ !! generalize this to be independent of vp
    drop 1 vp @
    DO  2dup vp I cells + @ (search-wordlist) dup
	IF  nip nip
	    UNLOOP EXIT
	THEN  drop
    -1 +LOOP
    2drop false ;

0 value locals-wordlist

: (localsvocfind)  ( addr count nfa1 -- nfa2|false )
    \ !! use generalized (vocfind)
    drop locals-wordlist
    IF 2dup locals-wordlist (search-wordlist) dup
	IF nip nip
	    EXIT
	THEN drop
    THEN
    0 (vocfind) ;

\ In the kernal the dictionary search works on only one wordlist.
\ The following stuff builds a thing that looks to the kernal like one
\ wordlist, but when searched it searches the whole search order
\  (including locals)

\ this is the wordlist-map of the dictionary
Create vocsearch       ' (localsvocfind) A, ' (reveal) A,  ' drop A,

\ Only root                                            14may93py

wordlist \ the wordlist structure
vocsearch over cell+ A! \ patch the map into it

Vocabulary Forth
Vocabulary Root

: Only  vp off  also Root also definitions ;

\ set initial search order                             14may93py

Forth-wordlist @ ' Forth >body A!

Only Forth also definitions

lookup A! \ our dictionary search order becomes the law

\ get-order set-order                                  14may93py

: get-order  ( -- wid1 .. widn n )
  vp @ 0 ?DO  vp cell+ I cells + @  LOOP  vp @ ;

: set-order  ( wid1 .. widn n / -1 -- )
  dup -1 = IF  drop Only exit  THEN  dup vp !
  ?dup IF  1- FOR  vp cell+ I cells + !  NEXT  THEN ;

: seal ( -- )  context @ 1 set-order ;

\ words visible in roots                               14may93py

: .name ( name -- ) name>string type space ;
: words  cr 0 context @
  BEGIN  @ dup  WHILE  2dup cell+ c@ $1F and 2 + dup >r +
         &79 >  IF  cr nip 0 swap  THEN
         dup .name space r> rot + swap  REPEAT 2drop ;

: body> ( data -- cfa )  0 >body - ;

: .voc  body> >name .name ;
: order  1 vp @  DO  vp I cells + @ .voc  -1 +LOOP  2 spaces
  current @ .voc ;
: vocs   voclink  BEGIN  @ dup @  WHILE  dup 2 cells - .voc  REPEAT  drop ;

Root definitions

' words Alias words
' Forth Alias Forth

Forth definitions

include hash.fs
