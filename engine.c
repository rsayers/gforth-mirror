/*
  $Id$
  Copyright 1992 by the ANSI figForth Development Group
*/

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "forth.h"
#include "io.h"

typedef union {
  struct {
#ifdef BIG_ENDIAN
    Cell high;
    Cell low;
#else
    Cell low;
    Cell high;
#endif;
  } cells;
  DCell dcell;
} Double_Store;

typedef struct F83Name {
  struct F83Name	*next;  /* the link field for old hands */
  char			countetc;
  Char			name[0];
} F83Name;

/* are macros for setting necessary? */
#define F83NAME_COUNT(np)	((np)->countetc & 0x1f)
#define F83NAME_SMUDGE(np)	(((np)->countetc & 0x40) != 0)
#define F83NAME_IMMEDIATE(np)	(((np)->countetc & 0x20) != 0)

/* NEXT and NEXT1 are split into several parts to help scheduling */
#ifdef DIRECT_THREADED
#	define NEXT1_P1 
#	define NEXT1_P2 ({goto *cfa;})
#	define DEF_CA
#else
#	define NEXT1_P1 ({ca = *cfa;})
#	define NEXT1_P2 ({goto *ca;})
#	define DEF_CA	Label ca;
#endif
#define NEXT_P1 ({cfa = *ip++; NEXT1_P1;})

#define NEXT1 ({DEF_CA NEXT1_P1; NEXT1_P2;})
#define NEXT ({DEF_CA NEXT_P1; NEXT1_P2;})

#ifdef USE_TOS
#define IF_TOS(x) x
#else
#define IF_TOS(x)
#define TOS (sp[0])
#endif

#ifdef USE_FTOS
#define IF_FTOS(x) x
#else
#define IF_FTOS(x)
#define FTOS (fp[0])
#endif

int emitcounter;
#define NULLC '\0'

#define cstr(to,from,size)\
	{	memcpy(to,from,size);\
		to[size]=NULLC;}
#define NEWLINE	'\n'

static char* fileattr[6]={"r","rb","r+","r+b","w+","w+b"};

Label *engine(Xt *ip, Cell *sp, Cell *rp, Float *fp, Address lp)
/* executes code at ip, if ip!=NULL
   returns array of machine code labels (for use in a loader), if ip==NULL
*/
{
  Xt cfa;
  Address up=NULL;
  static Label symbols[]= {
    &&docol,
    &&docon,
    &&dovar,
    &&douser,
    &&dodoes,
    &&dodoes,  /* dummy for does handler address */
#include "prim_labels.i"
  };
  int throw_code;
  IF_TOS(register Cell TOS;)
  IF_FTOS(Float FTOS;)
#ifdef CPU_DEP
  CPU_DEP;
#endif

  if (ip == NULL)
    return symbols;
  
  if ((throw_code=setjmp(throw_jmp_buf))) {
    static Cell signal_data_stack[8];

     /* AFAIK, it's not guarateed that the registers have the right value
	after a longjump, so we avoid using the current values.
	If it were guaranteed that the registers keep their values, we could
	call a signal handler in Forth instead of doing the throw from C */
    sp = &signal_data_stack[7];
    TOS = throw_code;
    ip = throw_ip;
    NEXT;
  }

  IF_TOS(TOS = sp[0]);
  IF_FTOS(FTOS = fp[0]);
  prep_terminal();
  NEXT;
  
 docol:
#ifdef DEBUG
  printf("%08x: col: %08x\n",(Cell)ip,(Cell)PFA1(cfa));
#endif
#ifdef undefined
  /* this is the simple version */
  *--rp = (Cell)ip;
  ip = (Xt *)PFA1(cfa);
  NEXT;
#endif
  /* this one is important, so we help the compiler optimizing
     The following version may be better (for scheduling), but probably has
     problems with code fields employing calls and delay slots
  */
  {
    DEF_CA
    Xt *current_ip = (Xt *)PFA1(cfa);
    cfa = *current_ip;
    NEXT1_P1;
    *--rp = (Cell)ip;
    ip = current_ip+1;
    NEXT1_P2;
  }
  
 docon:
#ifdef DEBUG
  printf("%08x: con: %08x\n",(Cell)ip,*(Cell*)PFA1(cfa));
#endif
#ifdef USE_TOS
  *sp-- = TOS;
  TOS = *(Cell *)PFA1(cfa);
#else
  *--sp = *(Cell *)PFA1(cfa);
#endif
  NEXT;
  
 dovar:
#ifdef DEBUG
  printf("%08x: var: %08x\n",(Cell)ip,(Cell)PFA1(cfa));
#endif
#ifdef USE_TOS
  *sp-- = TOS;
  TOS = (Cell)PFA1(cfa);
#else
  *--sp = (Cell)PFA1(cfa);
#endif
  NEXT;
  
  /* !! user? */
  
 douser:
#ifdef DEBUG
  printf("%08x: user: %08x\n",(Cell)ip,(Cell)PFA1(cfa));
#endif
#ifdef USE_TOS
  *sp-- = TOS;
  TOS = (Cell)(up+*(Cell*)PFA1(cfa));
#else
  *--sp = (Cell)(up+*(Cell*)PFA1(cfa));
#endif
  NEXT;
  
 dodoes:
  /* this assumes the following structure:
     defining-word:
     
     ...
     DOES>
     (possible padding)
     possibly handler: jmp dodoes
     (possible branch delay slot(s))
     Forth code after DOES>
     
     defined word:
     
     cfa: address of or jump to handler OR
          address of or jump to dodoes, address of DOES-code
     pfa:
     
     */
#ifdef DEBUG
  printf("%08x/%08x: does: %08x\n",(Cell)ip,(Cell)cfa,*(Cell)PFA(cfa));
  fflush(stdout);
#endif
  *--rp = (Cell)ip;
  /* PFA1 might collide with DOES_CODE1 here, so we use PFA */
#ifdef USE_TOS
  *sp-- = TOS;
  TOS = (Cell)PFA(cfa);
#else
  *--sp = (Cell)PFA(cfa);
#endif
  ip = DOES_CODE1(cfa);
  NEXT;
  
#include "primitives.i"
}
