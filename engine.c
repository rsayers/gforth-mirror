/*
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
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include "forth.h"
#include "io.h"
#include "threading.h"

#ifndef SEEK_SET
/* should be defined in stdio.h, but some systems don't have it */
#define SEEK_SET 0
#endif

#define IOR(flag)	((flag)? -512-errno : 0)

typedef union {
  struct {
#ifdef WORDS_BIGENDIAN
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

Cell *SP;
Float *FP;
int emitcounter;
#define NULLC '\0'

char *cstr(Char *from, UCell size, int clear)
/* if clear is true, scratch can be reused, otherwise we want more of
   the same */
{
  static char *scratch=NULL;
  static unsigned scratchsize=0;
  static char *nextscratch;
  char *oldnextscratch;

  if (clear)
    nextscratch=scratch;
  if (scratch==NULL) {
    scratch=malloc(size+1);
    nextscratch=scratch;
    scratchsize=size;
  }
  else if (nextscratch+size>scratch+scratchsize) {
    char *oldscratch=scratch;
    scratch = realloc(scratch, (nextscratch-scratch)+size+1);
    nextscratch=scratch+(nextscratch-oldscratch);
    scratchsize=size;
  }
  memcpy(nextscratch,from,size);
  nextscratch[size]='\0';
  oldnextscratch = nextscratch;
  nextscratch += size+1;
  return oldnextscratch;
}

char *tilde_cstr(Char *from, UCell size, int clear)
/* like cstr(), but perform tilde expansion on the string */
{
  char *s1,*s2;
  int s1_len, s2_len;
  struct passwd *getpwnam (), *user_entry;

  if (size<1 || from[0]!='~')
    return cstr(from, size, clear);
  if (size<2 || from[1]=='/') {
    s1 = (char *)getenv ("HOME");
    s2 = from+1;
    s2_len = size-1;
  } else {
    int i;
    for (i=1; i<size && from[i]!='/'; i++)
      ;
    {
      char user[i];
      memcpy(user,from+1,i-1);
      user[i-1]='\0';
      user_entry=getpwnam(user);
    }
    if (user_entry==NULL)
      return cstr(from, size, clear);
    s1 = user_entry->pw_dir;
    s2 = from+i;
    s2_len = size-i;
  }
  s1_len = strlen(s1);
  if (s1_len>1 && s1[s1_len-1]=='/')
    s1_len--;
  {
    char path[s1_len+s2_len];
    memcpy(path,s1,s1_len);
    memcpy(path+s1_len,s2,s2_len);
    return cstr(path,s1_len+s2_len,clear);
  }
}
   

#define NEWLINE	'\n'

#ifndef HAVE_RINT
#define rint(x)	floor((x)+0.5)
#endif

static char* fileattr[6]={"r","rb","r+","r+b","w","wb"};

static Address up0=NULL;

/* if machine.h has not defined explicit registers, define them as implicit */
#ifndef IPREG
#define IPREG
#endif
#ifndef SPREG
#define SPREG
#endif
#ifndef RPREG
#define RPREG
#endif
#ifndef FPREG
#define FPREG
#endif
#ifndef LPREG
#define LPREG
#endif
#ifndef CFAREG
#define CFAREG
#endif
#ifndef UPREG
#define UPREG
#endif
#ifndef TOSREG
#define TOSREG
#endif
#ifndef FTOSREG
#define FTOSREG
#endif

Label *engine(Xt *ip0, Cell *sp0, Cell *rp0, Float *fp0, Address lp0)
/* executes code at ip, if ip!=NULL
   returns array of machine code labels (for use in a loader), if ip==NULL
*/
{
  register Xt *ip IPREG = ip0;
  register Cell *sp SPREG = sp0;
  register Cell *rp RPREG = rp0;
  register Float *fp FPREG = fp0;
  register Address lp LPREG = lp0;
#ifdef CFA_NEXT
  register Xt cfa CFAREG;
#endif
  register Address up UPREG = up0;
  IF_TOS(register Cell TOS TOSREG;)
  IF_FTOS(register Float FTOS FTOSREG;)
  static Label symbols[]= {
    &&docol,
    &&docon,
    &&dovar,
    &&douser,
    &&dodefer,
    &&dofield,
    &&dodoes,
    &&dodoes,  /* dummy for does handler address */
#include "prim_labels.i"
  };
#ifdef CPU_DEP
  CPU_DEP;
#endif

#ifdef DEBUG
  fprintf(stderr,"ip=%x, sp=%x, rp=%x, fp=%x, lp=%x, up=%x\n",
          (unsigned)ip,(unsigned)sp,(unsigned)rp,
	  (unsigned)fp,(unsigned)lp,(unsigned)up);
#endif

  if (ip == NULL)
    return symbols;

  IF_TOS(TOS = sp[0]);
  IF_FTOS(FTOS = fp[0]);
/*  prep_terminal(); */
  NEXT_P0;
  NEXT;
  
 docol:
#ifndef CFA_NEXT
  {
    Xt cfa; GETCFA(cfa);
#endif
#ifdef DEBUG
  fprintf(stderr,"%08lx: col: %08lx\n",(Cell)ip,(Cell)PFA1(cfa));
#endif
#ifdef CISC_NEXT
  /* this is the simple version */
  *--rp = (Cell)ip;
  ip = (Xt *)PFA1(cfa);
  NEXT_P0;
  NEXT;
#else
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
#endif
#ifndef CFA_NEXT
  }
#endif

 docon:
#ifndef CFA_NEXT
  {
    Xt cfa; GETCFA(cfa);
#endif
#ifdef DEBUG
  fprintf(stderr,"%08lx: con: %08lx\n",(Cell)ip,*(Cell*)PFA1(cfa));
#endif
#ifdef USE_TOS
  *sp-- = TOS;
  TOS = *(Cell *)PFA1(cfa);
#else
  *--sp = *(Cell *)PFA1(cfa);
#endif
#ifndef CFA_NEXT
  }
#endif
  NEXT_P0;
  NEXT;
  
 dovar:
#ifndef CFA_NEXT
  {
    Xt cfa; GETCFA(cfa);
#endif
#ifdef DEBUG
  fprintf(stderr,"%08lx: var: %08lx\n",(Cell)ip,(Cell)PFA1(cfa));
#endif
#ifdef USE_TOS
  *sp-- = TOS;
  TOS = (Cell)PFA1(cfa);
#else
  *--sp = (Cell)PFA1(cfa);
#endif
#ifndef CFA_NEXT
  }
#endif
  NEXT_P0;
  NEXT;
  
 douser:
#ifndef CFA_NEXT
  {
    Xt cfa; GETCFA(cfa);
#endif
#ifdef DEBUG
  fprintf(stderr,"%08lx: user: %08lx\n",(Cell)ip,(Cell)PFA1(cfa));
#endif
#ifdef USE_TOS
  *sp-- = TOS;
  TOS = (Cell)(up+*(Cell*)PFA1(cfa));
#else
  *--sp = (Cell)(up+*(Cell*)PFA1(cfa));
#endif
#ifndef CFA_NEXT
  }
#endif
  NEXT_P0;
  NEXT;
  
 dodefer:
#ifndef CFA_NEXT
  {
    Xt cfa; GETCFA(cfa);
#endif
#ifdef DEBUG
  fprintf(stderr,"%08lx: defer: %08lx\n",(Cell)ip,*(Cell*)PFA1(cfa));
#endif
  EXEC(*(Xt *)PFA1(cfa));
#ifndef CFA_NEXT
  }
#endif

 dofield:
#ifndef CFA_NEXT
  {
    Xt cfa; GETCFA(cfa);
#endif
#ifdef DEBUG
  fprintf(stderr,"%08lx: field: %08lx\n",(Cell)ip,(Cell)PFA1(cfa));
#endif
  TOS += *(Cell*)PFA1(cfa); 
#ifndef CFA_NEXT
  }
#endif
  NEXT_P0;
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
#ifndef CFA_NEXT
  {
    Xt cfa; GETCFA(cfa);

/*    fprintf(stderr, "Got CFA %08lx at doescode %08lx/%08lx: does: %08lx\n",cfa,(Cell)ip,(Cell)PFA(cfa),(Cell)DOES_CODE1(cfa));*/
#endif
#ifdef DEBUG
  fprintf(stderr,"%08lx/%08lx: does: %08lx\n",(Cell)ip,(Cell)PFA(cfa),(Cell)DOES_CODE1(cfa));
  fflush(stderr);
#endif
  *--rp = (Cell)ip;
  /* PFA1 might collide with DOES_CODE1 here, so we use PFA */
  ip = DOES_CODE1(cfa);
#ifdef USE_TOS
  *sp-- = TOS;
  TOS = (Cell)PFA(cfa);
#else
  *--sp = (Cell)PFA(cfa);
#endif
#ifndef CFA_NEXT
/*    fprintf(stderr,"TOS = %08lx, IP=%08lx\n", TOS, IP);*/
  }
#endif
  NEXT_P0;
  NEXT;

#include "primitives.i"
}
