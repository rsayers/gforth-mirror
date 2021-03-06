/* command line interpretation, image loading etc. for Gforth


  Copyright (C) 1995,1996,1997,1998,2000,2003,2004,2005,2006,2007,2008,2009,2010,2011,2012 Free Software Foundation, Inc.

  This file is part of Gforth.

  Gforth is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation, either version 3
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, see http://www.gnu.org/licenses/.
*/

#include "config.h"
#include "forth.h"
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#ifndef STANDALONE
#include <sys/stat.h>
#endif
#include <fcntl.h>
#include <assert.h>
#include <stdlib.h>
#include <signal.h>

#ifndef STANDALONE
#if HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#endif
#include "io.h"
#include "getopt.h"
#ifndef STANDALONE
#include <locale.h>
#endif

/* output rules etc. for burg with --debug and --print-sequences */
/* #define BURG_FORMAT*/

typedef enum prim_num {
/* definitions of N_execute etc. */
#include PRIM_NUM_I
  N_START_SUPER
} PrimNum;

/* global variables for engine.c 
   We put them here because engine.c is compiled several times in
   different ways for the same engine. */
PER_THREAD Cell *gforth_SP;
PER_THREAD Float *gforth_FP;
PER_THREAD user_area* gforth_UP=NULL;
PER_THREAD Cell *gforth_RP;
PER_THREAD Address gforth_LP;

#ifdef HAS_FFCALL

#include <callback.h>

PER_THREAD va_alist gforth_clist;

void gforth_callback(Xt* fcall, void * alist)
{
  /* save global valiables */
  Cell *rp = gforth_RP;
  Cell *sp = gforth_SP;
  Float *fp = gforth_FP;
  Address lp = gforth_LP;
  va_alist clist = gforth_clist;

  gforth_clist = (va_alist)alist;

  gforth_engine(fcall sr_call);

  /* restore global variables */
  gforth_RP = rp;
  gforth_SP = sp;
  gforth_FP = fp;
  gforth_LP = lp;
  gforth_clist = clist;
}
#endif

#ifdef GFORTH_DEBUGGING
/* define some VM registers as global variables, so they survive exceptions;
   global register variables are not up to the task (according to the 
   GNU C manual) */
#if defined(GLOBALS_NONRELOC)
saved_regs saved_regs_v;
PER_THREAD saved_regs *saved_regs_p = &saved_regs_v;
#else /* !defined(GLOBALS_NONRELOC) */
PER_THREAD Xt *saved_ip;
PER_THREAD Cell *rp;
#endif /* !defined(GLOBALS_NONRELOC) */
#endif /* !defined(GFORTH_DEBUGGING) */

#ifdef NO_IP
Label next_code;
#endif

#ifdef HAS_FILE
char* fileattr[6]={"rb","rb","r+b","r+b","wb","wb"};
char* pfileattr[6]={"r","r","r+","r+","w","w"};

#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_TEXT
#define O_TEXT 0
#endif

int ufileattr[6]= {
  O_RDONLY|O_BINARY, O_RDONLY|O_BINARY,
  O_RDWR  |O_BINARY, O_RDWR  |O_BINARY,
  O_WRONLY|O_BINARY, O_WRONLY|O_BINARY };
#endif
/* end global vars for engine.c */

#define PRIM_VERSION 1
/* increment this whenever the primitives change in an incompatible way */

#ifndef DEFAULTPATH
#  define DEFAULTPATH "."
#endif

#ifdef MSDOS
jmp_buf throw_jmp_handler;
#endif

#if defined(DOUBLY_INDIRECT)
#  define CFA(n)	({Cell _n = (n); ((Cell)(((_n & 0x4000) ? symbols : xts)+(_n&~0x4000UL)));})
#else
#  define CFA(n)	((Cell)(symbols+((n)&~0x4000UL)))
#endif

#define maxaligned(n)	(typeof(n))((((Cell)n)+sizeof(Float)-1)&-sizeof(Float))

static UCell dictsize=0;
static UCell dsize=0;
static UCell rsize=0;
static UCell fsize=0;
static UCell lsize=0;
int offset_image=0;
int die_on_signal=0;
int ignore_async_signals=0;
#ifndef INCLUDE_IMAGE
static int clear_dictionary=0;
UCell pagesize=1;
char *progname;
#else
char *progname = "gforth";
int optind = 1;
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
/* IF you have an old Cygwin, this may help:
#ifdef __CYGWIN__
#define MAP_NORESERVE 0
#endif
*/
static int map_noreserve=MAP_NORESERVE;

#define CODE_BLOCK_SIZE (512*1024) /* !! overflow handling for -native */
Address code_area=0;
Cell code_area_size = CODE_BLOCK_SIZE;
Address code_here; /* does for code-area what HERE does for the dictionary */
Address start_flush=NULL; /* start of unflushed code */
Cell last_jump=0; /* if the last prim was compiled without jump, this
                     is it's number, otherwise this contains 0 */

static int no_super=0;   /* true if compile_prim should not fuse prims */
static int no_dynamic=NO_DYNAMIC_DEFAULT; /* if true, no code is generated
					     dynamically */
static int print_metrics=0; /* if true, print metrics on exit */
static int static_super_number = 10000; /* number of ss used if available */
#define MAX_STATE 9 /* maximum number of states */
static int maxstates = MAX_STATE; /* number of states for stack caching */
static int ss_greedy = 0; /* if true: use greedy, not optimal ss selection */
static int tpa_noequiv = 0;     /* if true: no state equivalence checking */
static int tpa_noautomaton = 0; /* if true: no tree parsing automaton */
static int tpa_trace = 0; /* if true: data for line graph of new states etc. */
static int print_sequences = 0; /* print primitive sequences for optimization */
static int relocs = 0;
static int nonrelocs = 0;

#ifdef HAS_DEBUG
int debug=0;
# define debugp(x...) do { if (debug) fprintf(x); } while (0)
#else
# define perror(x...)
# define fprintf(x...)
# define debugp(x...)
#endif

ImageHeader *gforth_header;
Label *vm_prims;
#ifdef DOUBLY_INDIRECT
Label *xts; /* same content as vm_prims, but should only be used for xts */
#endif

#ifndef NO_DYNAMIC
#ifndef CODE_ALIGNMENT
#define CODE_ALIGNMENT 0
#endif

#define MAX_IMMARGS 2

typedef struct {
  Label start; /* NULL if not relocatable */
  Cell length; /* only includes the jump iff superend is true*/
  Cell restlength; /* length of the rest (i.e., the jump or (on superend) 0) */
  char superend; /* true if primitive ends superinstruction, i.e.,
                     unconditional branch, execute, etc. */
  Cell nimmargs;
  struct immarg {
    Cell offset; /* offset of immarg within prim */
    char rel;    /* true if immarg is relative */
  } immargs[MAX_IMMARGS];
} PrimInfo;

PrimInfo *priminfos;
PrimInfo **decomp_prims;

const char const* const prim_names[]={
#include PRIM_NAMES_I
};

void init_ss_cost(void);

static int is_relocatable(int p)
{
  return !no_dynamic && priminfos[p].start != NULL;
}
#else /* defined(NO_DYNAMIC) */
static int is_relocatable(int p)
{
  return 0;
}
#endif /* defined(NO_DYNAMIC) */

#ifdef MEMCMP_AS_SUBROUTINE
int gforth_memcmp(const char * s1, const char * s2, size_t n)
{
  return memcmp(s1, s2, n);
}

Char *gforth_memmove(Char * dest, const Char* src, Cell n)
{
  return memmove(dest, src, n);
}

Char *gforth_memset(Char * s, Cell c, UCell n)
{
  return memset(s, c, n);
}

Char *gforth_memcpy(Char * dest, const Char* src, Cell n)
{
  return memcpy(dest, src, n);
}
#endif

static Cell max(Cell a, Cell b)
{
  return a>b?a:b;
}

static Cell min(Cell a, Cell b)
{
  return a<b?a:b;
}

#ifndef STANDALONE
/* image file format:
 *  "#! binary-path -i\n" (e.g., "#! /usr/local/bin/gforth-0.4.0 -i\n")
 *   padding to a multiple of 8
 *   magic: "Gforth4x" means format 0.8,
 *              where x is a byte with
 *              bit 7:   reserved = 0
 *              bit 6:5: address unit size 2^n octets
 *              bit 4:3: character size 2^n octets
 *              bit 2:1: cell size 2^n octets
 *              bit 0:   endian, big=0, little=1.
 *  The magic are always 8 octets, no matter what the native AU/character size is
 *  padding to max alignment (no padding necessary on current machines)
 *  ImageHeader structure (see forth.h)
 *  data (size in ImageHeader.image_size)
 *  tags ((if relocatable, 1 bit/data cell)
 *
 * tag==1 means that the corresponding word is an address;
 * If the word is >=0, the address is within the image;
 * addresses within the image are given relative to the start of the image.
 * If the word =-1 (CF_NIL), the address is NIL,
 * If the word is <CF_NIL and >CF(DODOES), it's a CFA (:, Create, ...)
 * If the word =CF(DODOES), it's a DOES> CFA
 * !! ABI-CODE and ;ABI-CODE
 * If the word is <CF(DOER_MAX) and bit 14 is set, it's the xt of a primitive
 * If the word is <CF(DOER_MAX) and bit 14 is clear, 
 *                                        it's the threaded code of a primitive
 * bits 13..9 of a primitive token state which group the primitive belongs to,
 * bits 8..0 of a primitive token index into the group
 */

Cell groups[32] = {
  0,
  0
#undef GROUP
#undef GROUPADD
#define GROUPADD(n) +n
#define GROUP(x, n) , 0
#include PRIM_GRP_I
#undef GROUP
#undef GROUPADD
#define GROUP(x, n)
#define GROUPADD(n)
};

static unsigned char *branch_targets(Cell *image, const unsigned char *bitstring,
			      int size, Cell base)
     /* produce a bitmask marking all the branch targets */
{
  int i=0, j, k, steps=(((size-1)/sizeof(Cell))/RELINFOBITS)+1;
  Cell token;
  unsigned char bits;
  unsigned char *result=malloc(steps);

  memset(result, 0, steps);
  for(k=0; k<steps; k++) {
    for(j=0, bits=bitstring[k]; j<RELINFOBITS; j++, i++, bits<<=1) {
      if(bits & (1U << (RELINFOBITS-1))) {
	assert(i*sizeof(Cell) < size);
        token=image[i];
	if (token>=base) { /* relocatable address */
	  UCell bitnum=(token-base)/sizeof(Cell);
	  if (bitnum/RELINFOBITS < (UCell)steps)
	    result[bitnum/RELINFOBITS] |= 1U << ((~bitnum)&(RELINFOBITS-1));
	}
      }
    }
  }
  return result;
}

void gforth_relocate(Cell *image, const Char *bitstring, 
		     UCell size, Cell base, Label symbols[])
{
  int i=0, j, k, steps=(((size-1)/sizeof(Cell))/RELINFOBITS)+1;
  Cell token;
  char bits;
  Cell max_symbols;
  /* 
   * A virtual start address that's the real start address minus 
   * the one in the image 
   */
  Cell *start = (Cell * ) (((void *) image) - ((void *) base));
  unsigned char *targets = branch_targets(image, bitstring, size, base);

  /* group index into table */
  if(groups[31]==0) {
    int groupsum=0;
    for(i=0; i<32; i++) {
      groupsum += groups[i];
      groups[i] = groupsum;
      /* printf("group[%d]=%d\n",i,groupsum); */
    }
    i=0;
  }
  
/* printf("relocating to %x[%x] start=%x base=%x\n", image, size, start, base); */
  
  for (max_symbols=0; symbols[max_symbols]!=0; max_symbols++)
    ;
  max_symbols--;

  for(k=0; k<steps; k++) {
    for(j=0, bits=bitstring[k]; j<RELINFOBITS; j++, i++, bits<<=1) {
      /*      fprintf(stderr,"relocate: image[%d]\n", i);*/
      if(bits & (1U << (RELINFOBITS-1))) {
	assert(i*sizeof(Cell) < size);
	/* fprintf(stderr,"relocate: image[%d]=%d of %d\n", i, image[i], size/sizeof(Cell)); */
        token=image[i];
	if(token<0) {
	  int group = (-token & 0x3E00) >> 9;
	  if(group == 0) {
	    switch(token|0x4000) {
	    case CF_NIL      : image[i]=0; break;
#if !defined(DOUBLY_INDIRECT)
	    case CF(DOCOL)   :
	    case CF(DOVAR)   :
	    case CF(DOCON)   :
	    case CF(DOVAL)   :
	    case CF(DOUSER)  : 
	    case CF(DODEFER) : 
	    case CF(DOFIELD) : 
	    case CF(DODOES)  :
	    case CF(DOABICODE) :
	    case CF(DOSEMIABICODE): 
	      MAKE_CF(image+i,symbols[CF(token)]); break;
#endif /* !defined(DOUBLY_INDIRECT) */
	    default          : /* backward compatibility */
/*	      printf("Code field generation image[%x]:=CFA(%x)\n",
		     i, CF(image[i])); */
	      if (CF((token | 0x4000))<max_symbols) {
		image[i]=(Cell)CFA(CF(token));
#ifdef DIRECT_THREADED
		if ((token & 0x4000) == 0) { /* threade code, no CFA */
		  if (targets[k] & (1U<<(RELINFOBITS-1-j)))
		    compile_prim1(0);
		  compile_prim1(&image[i]);
		}
#endif
	      } else
		fprintf(stderr,"Primitive %ld used in this image at %p (offset $%x) is not implemented by this\n engine (%s); executing this code will crash.\n",(long)CF(token), &image[i], i, PACKAGE_VERSION);
	    }
	  } else {
	    int tok = -token & 0x1FF;
	    if (tok < (groups[group+1]-groups[group])) {
#if defined(DOUBLY_INDIRECT)
	      image[i]=(Cell)CFA(((groups[group]+tok) | (CF(token) & 0x4000)));
#else
	      image[i]=(Cell)CFA((groups[group]+tok));
#endif
#ifdef DIRECT_THREADED
	      if ((token & 0x4000) == 0) { /* threade code, no CFA */
		if (targets[k] & (1U<<(RELINFOBITS-1-j)))
		  compile_prim1(0);
		compile_prim1(&image[i]);
	      }
#endif
	    } else
	      fprintf(stderr,"Primitive %lx, %d of group %d used in this image at %p (offset $%x) is not implemented by this\n engine (%s); executing this code will crash.\n", (long)-token, tok, group, &image[i],i,PACKAGE_VERSION);
	  }
	} else {
          /* if base is > 0: 0 is a null reference so don't adjust*/
          if (token>=base) {
            image[i]+=(Cell)start;
          }
        }
      }
    }
  }
  free(targets);
  finish_code();
  ((ImageHeader*)(image))->base = (Address) image;
}

#ifndef DOUBLY_INDIRECT
static UCell checksum(Label symbols[])
{
  UCell r=PRIM_VERSION;
  Cell i;

  for (i=DOCOL; i<=DOER_MAX; i++) {
    r ^= (UCell)(symbols[i]);
    r = (r << 5) | (r >> (8*sizeof(Cell)-5));
  }
#ifdef DIRECT_THREADED
  /* we have to consider all the primitives */
  for (; symbols[i]!=(Label)0; i++) {
    r ^= (UCell)(symbols[i]);
    r = (r << 5) | (r >> (8*sizeof(Cell)-5));
  }
#else
  /* in indirect threaded code all primitives are accessed through the
     symbols table, so we just have to put the base address of symbols
     in the checksum */
  r ^= (UCell)symbols;
#endif
  return r;
}
#endif

static Address verbose_malloc(Cell size)
{
  Address r;
  /* leave a little room (64B) for stack underflows */
  if ((r = malloc(size+64))==NULL) {
    perror(progname);
    exit(1);
  }
  r = (Address)((((Cell)r)+(sizeof(Float)-1))&(-sizeof(Float)));
  debugp(stderr, "malloc succeeds, address=%p\n", r);
  return r;
}

static void *next_address=0;
static void after_alloc(Address r, Cell size)
{
  if (r != (Address)-1) {
    debugp(stderr, "success, address=%p\n", r);
#if 0
    /* not needed now that we protect the stacks with mprotect */
    if (pagesize != 1)
      next_address = (Address)(((((Cell)r)+size-1)&-pagesize)+2*pagesize); /* leave one page unmapped */
#endif
  } else {
    debugp(stderr, "failed: %s\n", strerror(errno));
  }
}

#ifndef MAP_FAILED
#define MAP_FAILED ((Address) -1)
#endif
#ifndef MAP_FILE
# define MAP_FILE 0
#endif
#ifndef MAP_PRIVATE
# define MAP_PRIVATE 0
#endif
#ifndef PROT_NONE
# define PROT_NONE 0
#endif
#if !defined(MAP_ANON) && defined(MAP_ANONYMOUS)
# define MAP_ANON MAP_ANONYMOUS
#endif

#if defined(HAVE_MMAP)
static Address alloc_mmap(Cell size)
{
  void *r;

#if defined(MAP_ANON)
  debugp(stderr,"try mmap(%p, $%lx, ..., MAP_ANON, ...); ", next_address, size);
  r = mmap(next_address, size, PROT_EXEC|PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE|map_noreserve, -1, 0);
#else /* !defined(MAP_ANON) */
  /* Ultrix (at least) does not define MAP_FILE and MAP_PRIVATE (both are
     apparently defaults) */
  static int dev_zero=-1;

  if (dev_zero == -1)
    dev_zero = open("/dev/zero", O_RDONLY);
  if (dev_zero == -1) {
    r = MAP_FAILED;
    debugp(stderr, "open(\"/dev/zero\"...) failed (%s), no mmap; ", 
	      strerror(errno));
  } else {
    debugp(stderr,"try mmap(%p, $%lx, ..., MAP_FILE, dev_zero, ...); ", next_address, size);
    r=mmap(next_address, size, PROT_EXEC|PROT_READ|PROT_WRITE, MAP_FILE|MAP_PRIVATE|map_noreserve, dev_zero, 0);
  }
#endif /* !defined(MAP_ANON) */
  after_alloc(r, size);
  return r;  
}

static void page_noaccess(void *a)
{
  /* try mprotect first; with munmap the page might be allocated later */
  debugp(stderr, "try mprotect(%p,$%lx,PROT_NONE); ", a, (long)pagesize);
  if (mprotect(a, pagesize, PROT_NONE)==0) {
    debugp(stderr, "ok\n");
    return;
  }
  debugp(stderr, "failed: %s\n", strerror(errno));
  debugp(stderr, "try munmap(%p,$%lx); ", a, (long)pagesize);
  if (munmap(a,pagesize)==0) {
    debugp(stderr, "ok\n");
    return;
  }
  debugp(stderr, "failed: %s\n", strerror(errno));
}  

static size_t wholepage(size_t n)
{
  return (n+pagesize-1)&~(pagesize-1);
}
#endif

Address gforth_alloc(Cell size)
{
#if HAVE_MMAP
  Address r;

  r=alloc_mmap(size);
  if (r!=(Address)MAP_FAILED)
    return r;
#endif /* HAVE_MMAP */
  /* use malloc as fallback */
  return verbose_malloc(size);
}

static void *dict_alloc_read(FILE *file, Cell imagesize, Cell dictsize, Cell offset)
{
  void *image = MAP_FAILED;

#if defined(HAVE_MMAP)
  if (offset==0) {
    image=alloc_mmap(dictsize);
    if (image != (void *)MAP_FAILED) {
      void *image1;
      debugp(stderr,"try mmap(%p, $%lx, ..., MAP_FIXED|MAP_FILE, imagefile, 0); ", image, imagesize);
      image1 = mmap(image, imagesize, PROT_EXEC|PROT_READ|PROT_WRITE, MAP_FIXED|MAP_FILE|MAP_PRIVATE|map_noreserve, fileno(file), 0);
      after_alloc(image1,dictsize);
      if (image1 == (void *)MAP_FAILED)
	goto read_image;
    }
  }
#endif /* defined(HAVE_MMAP) */
  if (image == (void *)MAP_FAILED) {
    image = gforth_alloc(dictsize+offset)+offset;
  read_image:
    rewind(file);  /* fseek(imagefile,0L,SEEK_SET); */
    fread(image, 1, imagesize, file);
  }
  return image;
}
#endif

void gforth_free()
{
  Cell image = (-pagesize) & (Cell)gforth_header;
#ifdef HAVE_MMAP
  debugp(stderr,"try unmmap(%p, $%lx); ", (void*)image, dictsize);
  if(!munmap((void*)image, dictsize)) {
    debugp(stderr,"ok\n");
  }
#else
  free((void*)image);
#endif
}

void set_stack_sizes(ImageHeader * header)
{
  if (dictsize==0)
    dictsize = header->dict_size;
  if (dsize==0)
    dsize = header->data_stack_size;
  if (rsize==0)
    rsize = header->return_stack_size;
  if (fsize==0)
    fsize = header->fp_stack_size;
  if (lsize==0)
    lsize = header->locals_stack_size;
  dictsize=maxaligned(dictsize);
  dsize=maxaligned(dsize);
  rsize=maxaligned(rsize);
  lsize=maxaligned(lsize);
  fsize=maxaligned(fsize);

  header->dict_size=dictsize;
  header->data_stack_size=dsize;
  header->fp_stack_size=fsize;
  header->return_stack_size=rsize;
  header->locals_stack_size=lsize;
}

#warning You can ignore the warnings about clobbered variables in gforth_go

#define NEXTPAGE(addr) ((Address)((((UCell)(addr)-1)&-pagesize)+pagesize))
#define NEXTPAGE2(addr) ((Address)((((UCell)(addr)-1)&-pagesize)+2*pagesize))

Cell gforth_go(Xt* ip0)
{
#ifdef SYSSIGNALS
  int throw_code;
  jmp_buf throw_jmp_buf;
  jmp_buf* old_handler;
#endif
  Cell signal_data_stack[24];
  Cell signal_return_stack[16];
  Float signal_fp_stack[1];
  Cell result;

#if defined(SYSSIGNALS) && !defined(STANDALONE)
  old_handler = throw_jmp_handler;
  throw_jmp_handler = &throw_jmp_buf;

  debugp(stderr, "setjmp(%p)\n", *throw_jmp_handler);
  while((throw_code=setjmp(*throw_jmp_handler))) {
    signal_data_stack[15]=throw_code;

#ifdef GFORTH_DEBUGGING
    debugp(stderr,"\ncaught signal, throwing exception %d, ip=%p rp=%p\n",
	      throw_code, saved_ip, rp);
    if ((rp > NEXTPAGE2(gforth_UP->sp0)) &&
	(rp < NEXTPAGE(gforth_UP->rp0))) {
      /* no rstack overflow or underflow */
      gforth_RP = rp;
      *--gforth_RP = (Cell)saved_ip;
    }
    else /* I love non-syntactic ifdefs :-) */
      gforth_RP = signal_return_stack+16;
#else  /* !defined(GFORTH_DEBUGGING) */
    debugp(stderr,"\ncaught signal, throwing exception %d\n", throw_code);
    gforth_RP = signal_return_stack+16;
#endif /* !defined(GFORTH_DEBUGGING) */
    /* fprintf(stderr, "rp=$%x\n",rp0);*/
    
    ip0=gforth_header->throw_entry;
    gforth_SP=signal_data_stack+15;
    gforth_FP=signal_fp_stack;
  }
#endif

  result=((Cell)gforth_engine(ip0 sr_call));
  throw_jmp_handler = old_handler;
  return result;
}

#if !defined(INCLUDE_IMAGE) && !defined(STANDALONE)
static void print_sizes(Cell sizebyte)
     /* print size information */
{
  static char* endianstring[]= { "   big","little" };
  
  fprintf(stderr,"%s endian, cell=%d bytes, char=%d bytes, au=%d bytes\n",
	  endianstring[sizebyte & 1],
	  1 << ((sizebyte >> 1) & 3),
	  1 << ((sizebyte >> 3) & 3),
	  1 << ((sizebyte >> 5) & 3));
}

/* static superinstruction stuff */

struct cost { /* super_info might be a more accurate name */
  char loads;       /* number of stack loads */
  char stores;      /* number of stack stores */
  char updates;     /* number of stack pointer updates */
  char branch;	    /* is it a branch (SET_IP) */
  unsigned char state_in;    /* state on entry */
  unsigned char state_out;   /* state on exit */
  unsigned char imm_ops;     /* number of immediate operands */
  short offset;     /* offset into super2 table */
  unsigned char length;      /* number of components */
};

PrimNum super2[] = {
#include SUPER2_I
};

struct cost super_costs[] = {
#include COSTS_I
};

struct super_state {
  struct super_state *next;
  PrimNum super;
};

#define HASH_SIZE 256

struct super_table_entry {
  struct super_table_entry *next;
  PrimNum *start;
  short length;
  struct super_state *ss_list; /* list of supers */
} *super_table[HASH_SIZE];
int max_super=2;

struct super_state *state_transitions=NULL;

static int hash_super(PrimNum *start, int length)
{
  int i, r;
  
  for (i=0, r=0; i<length; i++) {
    r <<= 1;
    r += start[i];
  }
  return r & (HASH_SIZE-1);
}

static struct super_state **lookup_super(PrimNum *start, int length)
{
  int hash=hash_super(start,length);
  struct super_table_entry *p = super_table[hash];

  /* assert(length >= 2); */
  for (; p!=NULL; p = p->next) {
    if (length == p->length &&
	memcmp((char *)p->start, (char *)start, length*sizeof(PrimNum))==0)
      return &(p->ss_list);
  }
  return NULL;
}

static void prepare_super_table()
{
  int i;
  int nsupers = 0;

  for (i=0; i<sizeof(super_costs)/sizeof(super_costs[0]); i++) {
    struct cost *c = &super_costs[i];
    if ((c->length < 2 || nsupers < static_super_number) &&
	c->state_in < maxstates && c->state_out < maxstates) {
      struct super_state **ss_listp= lookup_super(super2+c->offset, c->length);
      struct super_state *ss = malloc(sizeof(struct super_state));
      ss->super= i;
      if (c->offset==N_noop && i != N_noop) {
	if (is_relocatable(i)) {
	  ss->next = state_transitions;
	  state_transitions = ss;
	}
      } else if (ss_listp != NULL) {
	ss->next = *ss_listp;
	*ss_listp = ss;
      } else {
	int hash = hash_super(super2+c->offset, c->length);
	struct super_table_entry **p = &super_table[hash];
	struct super_table_entry *e = malloc(sizeof(struct super_table_entry));
	ss->next = NULL;
	e->next = *p;
	e->start = super2 + c->offset;
	e->length = c->length;
	e->ss_list = ss;
	*p = e;
      }
      if (c->length > max_super)
	max_super = c->length;
      if (c->length >= 2)
	nsupers++;
    }
  }
  debugp(stderr, "Using %d static superinsts\n", nsupers);
  if (nsupers>0 && !tpa_noautomaton && !tpa_noequiv) {
    /* Currently these two things don't work together; see Section 3.2
       of <http://www.complang.tuwien.ac.at/papers/ertl+06pldi.ps.gz>,
       in particular Footnote 6 for the reason; hmm, we should be able
       to use an automaton without state equivalence, but that costs
       significant space so we only do it if the user explicitly
       disables state equivalence. */
    debugp(stderr, "Disabling tpa-automaton, because nsupers>0 and state equivalence is enabled.\n");
    tpa_noautomaton = 1;
  }
}

/* dynamic replication/superinstruction stuff */

#ifndef NO_DYNAMIC
static int compare_priminfo_length(const void *_a, const void *_b)
{
  PrimInfo **a = (PrimInfo **)_a;
  PrimInfo **b = (PrimInfo **)_b;
  Cell diff = (*a)->length - (*b)->length;
  if (diff)
    return diff;
  else /* break ties by start address; thus the decompiler produces
          the earliest primitive with the same code (e.g. noop instead
          of (char) and @ instead of >code-address */
    return (*b)->start - (*a)->start;
}
#endif /* !defined(NO_DYNAMIC) */

static char MAYBE_UNUSED superend[]={
#include PRIM_SUPEREND_I
};

Cell npriminfos=0;

Label goto_start;
Cell goto_len;

#ifndef NO_DYNAMIC
static int compare_labels(const void *pa, const void *pb)
{
  Label a = *(Label *)pa;
  Label b = *(Label *)pb;
  return a-b;
}
#endif

static Label bsearch_next(Label key, Label *a, UCell n)
     /* a is sorted; return the label >=key that is the closest in a;
        return NULL if there is no label in a >=key */
{
  int mid = (n-1)/2;
  if (n<1)
    return NULL;
  if (n == 1) {
    if (a[0] < key)
      return NULL;
    else
      return a[0];
  }
  if (a[mid] < key)
    return bsearch_next(key, a+mid+1, n-mid-1);
  else
    return bsearch_next(key, a, mid+1);
}

static void check_prims(Label symbols1[])
{
  int i;
#ifndef NO_DYNAMIC
  Label *symbols2, *symbols3, *ends1, *ends1j, *ends1jsorted, *goto_p;
  int nends1j;
#endif

  if (debug)
#ifdef __VERSION__
    fprintf(stderr, "Compiled with gcc-" __VERSION__ "\n");
#else
#define xstr(s) str(s)
#define str(s) #s
  fprintf(stderr, "Compiled with gcc-" xstr(__GNUC__) "." xstr(__GNUC_MINOR__) "\n"); 
#endif
  for (i=0; symbols1[i]!=0; i++)
    ;
  npriminfos = i;
  
#ifndef NO_DYNAMIC
  if (no_dynamic)
    return;
  symbols2=gforth_engine2(0 sr_call);
#if NO_IP
  symbols3=gforth_engine3(0 sr_call);
#else
  symbols3=symbols1;
#endif
  ends1 = symbols1+i+1;
  ends1j =   ends1+i;
  goto_p = ends1j+i+1; /* goto_p[0]==before; ...[1]==after;*/
  nends1j = i+1;
  ends1jsorted = (Label *)alloca(nends1j*sizeof(Label));
  memcpy(ends1jsorted,ends1j,nends1j*sizeof(Label));
  qsort(ends1jsorted, nends1j, sizeof(Label), compare_labels);

  /* check whether the "goto *" is relocatable */
  goto_len = goto_p[1]-goto_p[0];
  debugp(stderr, "goto * %p %p len=%ld\n",
	 goto_p[0],symbols2[goto_p-symbols1],(long)goto_len);
  if (memcmp(goto_p[0],symbols2[goto_p-symbols1],goto_len)!=0) { /* unequal */
    no_dynamic=1;
    debugp(stderr,"  not relocatable, disabling dynamic code generation\n");
    init_ss_cost();
    return;
  }
  goto_start = goto_p[0];
  
  priminfos = calloc(i,sizeof(PrimInfo));
  for (i=0; symbols1[i]!=0; i++) {
    int prim_len = ends1[i]-symbols1[i];
    PrimInfo *pi=&priminfos[i];
    struct cost *sc=&super_costs[i];
    int j=0;
    char *s1 = (char *)symbols1[i];
    char *s2 = (char *)symbols2[i];
    char *s3 = (char *)symbols3[i];
    Label endlabel = bsearch_next(symbols1[i]+1,ends1jsorted,nends1j);

    pi->start = s1;
    pi->superend = superend[i]|no_super;
    pi->length = prim_len;
    pi->restlength = endlabel - symbols1[i] - pi->length;
    pi->nimmargs = 0;
    relocs++;
#if defined(BURG_FORMAT)
    { /* output as burg-style rules */
      int p=super_costs[i].offset;
      if (p==N_noop)
	debugp(stderr, "S%d: S%d = %d (%d);", sc->state_in, sc->state_out, i+1, pi->length);
      else
	debugp(stderr, "S%d: op%d(S%d) = %d (%d);", sc->state_in, p, sc->state_out, i+1, pi->length);
    }
#else
    debugp(stderr, "%-15s %d-%d %4d %p %p %p len=%3ld rest=%2ld send=%1d",
	   prim_names[i], sc->state_in, sc->state_out,
	   i, s1, s2, s3, (long)(pi->length), (long)(pi->restlength),
	   pi->superend);
#endif
    if (endlabel == NULL) {
      pi->start = NULL; /* not relocatable */
      if (pi->length<0) pi->length=100;
#ifndef BURG_FORMAT
      debugp(stderr,"\n   non_reloc: no J label > start found\n");
#endif
      relocs--;
      nonrelocs++;
      continue;
    }
    if (ends1[i] > endlabel && !pi->superend) {
      pi->start = NULL; /* not relocatable */
      pi->length = endlabel-symbols1[i];
#ifndef BURG_FORMAT
      debugp(stderr,"\n   non_reloc: there is a J label before the K label (restlength<0)\n");
#endif
      relocs--;
      nonrelocs++;
      continue;
    }
    if (ends1[i] < pi->start && !pi->superend) {
      pi->start = NULL; /* not relocatable */
      pi->length = endlabel-symbols1[i];
#ifndef BURG_FORMAT
      debugp(stderr,"\n   non_reloc: K label before I label (length<0)\n");
#endif
      relocs--;
      nonrelocs++;
      continue;
    }
    if (CHECK_PRIM(s1, prim_len)) {
#ifndef BURG_FORMAT
      debugp(stderr,"\n   non_reloc: architecture specific check failed\n");
#endif
      pi->start = NULL; /* not relocatable */
      relocs--;
      nonrelocs++;
      continue;
    }
    assert(pi->length>=0);
    assert(pi->restlength >=0);
    while (j<(pi->length+pi->restlength)) {
      if (s1[j]==s3[j]) {
	if (s1[j] != s2[j]) {
	  pi->start = NULL; /* not relocatable */
#ifndef BURG_FORMAT
	  debugp(stderr,"\n   non_reloc: engine1!=engine2 offset %3d",j);
#endif
	  /* assert(j<prim_len); */
	  relocs--;
	  nonrelocs++;
	  break;
	}
	j++;
      } else {
	struct immarg *ia=&pi->immargs[pi->nimmargs];

	pi->nimmargs++;
	ia->offset=j;
	if ((~*(Cell *)&(s1[j]))==*(Cell *)&(s3[j])) {
	  ia->rel=0;
	  debugp(stderr,"\n   absolute immarg: offset %3d",j);
	} else if ((&(s1[j]))+(*(Cell *)&(s1[j]))+4 ==
		   symbols1[DOER_MAX+1]) {
	  ia->rel=1;
	  debugp(stderr,"\n   relative immarg: offset %3d",j);
	} else {
	  pi->start = NULL; /* not relocatable */
#ifndef BURG_FORMAT
	  debugp(stderr,"\n   non_reloc: engine1!=engine3 offset %3d",j);
#endif
	  /* assert(j<prim_len);*/
	  relocs--;
	  nonrelocs++;
	  break;
	}
	j+=4;
      }
    }
    debugp(stderr,"\n");
  }
  decomp_prims = calloc(i,sizeof(PrimInfo *));
  for (i=DOER_MAX+1; i<npriminfos; i++)
    decomp_prims[i] = &(priminfos[i]);
  qsort(decomp_prims+DOER_MAX+1, npriminfos-DOER_MAX-1, sizeof(PrimInfo *),
	compare_priminfo_length);
#endif
}

static void flush_to_here(void)
{
#ifndef NO_DYNAMIC
  if (start_flush)
    FLUSH_ICACHE((caddr_t)start_flush, code_here-start_flush);
  start_flush=code_here;
#endif
}

static void MAYBE_UNUSED align_code(void)
     /* align code_here on some platforms */
{
#ifndef NO_DYNAMIC
#if defined(CODE_PADDING)
  Cell alignment = CODE_ALIGNMENT;
  static char nops[] = CODE_PADDING;
  UCell maxpadding=MAX_PADDING;
  UCell offset = ((UCell)code_here)&(alignment-1);
  UCell length = alignment-offset;
  if (length <= maxpadding) {
    memcpy(code_here,nops+offset,length);
    code_here += length;
  }
#endif /* defined(CODE_PADDING) */
#endif /* defined(NO_DYNAMIC */
}  

#ifndef NO_DYNAMIC
static void append_jump(void)
{
  if (last_jump) {
    PrimInfo *pi = &priminfos[last_jump];
    
    memcpy(code_here, pi->start+pi->length, pi->restlength);
    code_here += pi->restlength;
    memcpy(code_here, goto_start, goto_len);
    code_here += goto_len;
    align_code();
    last_jump=0;
  }
}

/* Gforth remembers all code blocks in this list.  On forgetting (by
executing a marker) the code blocks are not freed (because Gforth does
not remember how they were allocated; hmm, remembering that might be
easier and cleaner).  Instead, code_here etc. are reset to the old
value, and the "forgotten" code blocks are reused when they are
needed. */

struct code_block_list {
  struct code_block_list *next;
  Address block;
  Cell size;
} *code_block_list=NULL, **next_code_blockp=&code_block_list;

static void reserve_code_space(UCell size)
{
  if (code_area+code_area_size < code_here+size) {
    struct code_block_list *p;
    append_jump();
    debugp(stderr,"Did not use %ld bytes in code block\n",
           (long)(code_area+code_area_size-code_here));
    flush_to_here();
    if (*next_code_blockp == NULL) {
      code_here = start_flush = code_area = gforth_alloc(code_area_size);
      p = (struct code_block_list *)malloc(sizeof(struct code_block_list));
      *next_code_blockp = p;
      p->next = NULL;
      p->block = code_here;
      p->size = code_area_size;
    } else {
      p = *next_code_blockp;
      code_here = start_flush = code_area = p->block;
    }
    next_code_blockp = &(p->next);
  }
}

static Address append_prim(Cell p)
{
  PrimInfo *pi = &priminfos[p];
  Address old_code_here;
  reserve_code_space(pi->length+pi->restlength+goto_len+CODE_ALIGNMENT-1);
  memcpy(code_here, pi->start, pi->length);
  old_code_here = code_here;
  code_here += pi->length;
  return old_code_here;
}

static void reserve_code_super(PrimNum origs[], int ninsts)
{
  int i;
  UCell size = CODE_ALIGNMENT-1; /* alignment may happen first */
  if (no_dynamic)
    return;
  /* use size of the original primitives as an upper bound for the
     size of the superinstruction.  !! This is only safe if we
     optimize for code size (the default) */
  for (i=0; i<ninsts; i++) {
    PrimNum p = origs[i];
    PrimInfo *pi = &priminfos[p];
    if (is_relocatable(p))
      size += pi->length;
    else
      if (i>0)
        size += priminfos[origs[i-1]].restlength+goto_len+CODE_ALIGNMENT-1;
  }
  size += priminfos[origs[i-1]].restlength+goto_len;
  reserve_code_space(size);
}
#endif

int forget_dyncode(Address code)
{
#ifdef NO_DYNAMIC
  return -1;
#else
  struct code_block_list *p, **pp;

  for (pp=&code_block_list, p=*pp; p!=NULL; pp=&(p->next), p=*pp) {
    if (code >= p->block && code < p->block+p->size) {
      next_code_blockp = &(p->next);
      code_here = start_flush = code;
      code_area = p->block;
      last_jump = 0;
      return -1;
    }
  }
  return -no_dynamic;
#endif /* !defined(NO_DYNAMIC) */
}

static long dyncodesize(void)
{
#ifndef NO_DYNAMIC
  struct code_block_list *p;
  long size=0;
  for (p=code_block_list; p!=NULL; p=p->next) {
    if (code_here >= p->block && code_here < p->block+p->size)
      return size + (code_here - p->block);
    else
      size += p->size;
  }
#endif /* !defined(NO_DYNAMIC) */
  return 0;
}

Label decompile_code(Label _code)
{
#ifdef NO_DYNAMIC
  return _code;
#else /* !defined(NO_DYNAMIC) */
  Cell i;
  struct code_block_list *p;
  Address code=_code;

  /* first, check if we are in code at all */
  for (p = code_block_list;; p = p->next) {
    if (p == NULL)
      return code;
    if (code >= p->block && code < p->block+p->size)
      break;
  }
  /* reverse order because NOOP might match other prims */
  for (i=npriminfos-1; i>DOER_MAX; i--) {
    PrimInfo *pi=decomp_prims[i];
    if (pi->start==code || (pi->start && memcmp(code,pi->start,pi->length)==0))
      return vm_prims[super2[super_costs[pi-priminfos].offset]];
    /* return pi->start;*/
  }
  return code;
#endif /* !defined(NO_DYNAMIC) */
}

#ifdef NO_IP
int nbranchinfos=0;

struct branchinfo {
  Label **targetpp; /* **(bi->targetpp) is the target */
  Cell *addressptr; /* store the target here */
} branchinfos[100000];

int ndoesexecinfos=0;
struct doesexecinfo {
  int branchinfo; /* fix the targetptr of branchinfos[...->branchinfo] */
  Label *targetp; /*target for branch (because this is not in threaded code)*/
  Cell *xt; /* cfa of word whose does-code needs calling */
} doesexecinfos[10000];

static void set_rel_target(Cell *source, Label target)
{
  *source = ((Cell)target)-(((Cell)source)+4);
}

static void register_branchinfo(Label source, Cell *targetpp)
{
  struct branchinfo *bi = &(branchinfos[nbranchinfos]);
  bi->targetpp = (Label **)targetpp;
  bi->addressptr = (Cell *)source;
  nbranchinfos++;
}

static Address compile_prim1arg(PrimNum p, Cell **argp)
{
  Address old_code_here=append_prim(p);

  assert(vm_prims[p]==priminfos[p].start);
  *argp = (Cell*)(old_code_here+priminfos[p].immargs[0].offset);
  return old_code_here;
}

static Address compile_call2(Cell *targetpp, Cell **next_code_targetp)
{
  PrimInfo *pi = &priminfos[N_call2];
  Address old_code_here = append_prim(N_call2);

  *next_code_targetp = (Cell *)(old_code_here + pi->immargs[0].offset);
  register_branchinfo(old_code_here + pi->immargs[1].offset, targetpp);
  return old_code_here;
}
#endif

void finish_code(void)
{
#ifdef NO_IP
  Cell i;

  compile_prim1(NULL);
  for (i=0; i<ndoesexecinfos; i++) {
    struct doesexecinfo *dei = &doesexecinfos[i];
    dei->targetp = (Label *)DOES_CODE1((dei->xt));
    branchinfos[dei->branchinfo].targetpp = &(dei->targetp);
  }
  ndoesexecinfos = 0;
  for (i=0; i<nbranchinfos; i++) {
    struct branchinfo *bi=&branchinfos[i];
    set_rel_target(bi->addressptr, **(bi->targetpp));
  }
  nbranchinfos = 0;
#else
  compile_prim1(NULL);
#endif
  flush_to_here();
}

#if !(defined(DOUBLY_INDIRECT) || defined(INDIRECT_THREADED))
#ifdef NO_IP
static Cell compile_prim_dyn(PrimNum p, Cell *tcp)
     /* compile prim #p dynamically (mod flags etc.) and return start
	address of generated code for putting it into the threaded
	code. This function is only called if all the associated
	inline arguments of p are already in place (at tcp[1] etc.) */
{
  PrimInfo *pi=&priminfos[p];
  Cell *next_code_target=NULL;
  Address codeaddr;
  Address primstart;
  
  assert(p<npriminfos);
  if (p==N_execute || p==N_perform || p==N_lit_perform) {
    codeaddr = compile_prim1arg(N_set_next_code, &next_code_target);
    primstart = append_prim(p);
    goto other_prim;
  } else if (p==N_call) {
    codeaddr = compile_call2(tcp+1, &next_code_target);
  } else if (p==N_does_exec) {
    struct doesexecinfo *dei = &doesexecinfos[ndoesexecinfos++];
    Cell *arg;
    codeaddr = compile_prim1arg(N_lit,&arg);
    *arg = (Cell)PFA(tcp[1]);
    /* we cannot determine the callee now (last_start[1] may be a
       forward reference), so just register an arbitrary target, and
       register in dei that we need to fix this before resolving
       branches */
    dei->branchinfo = nbranchinfos;
    dei->xt = (Cell *)(tcp[1]);
    compile_call2(0, &next_code_target);
  } else if (!is_relocatable(p)) {
    Cell *branch_target;
    codeaddr = compile_prim1arg(N_set_next_code, &next_code_target);
    compile_prim1arg(N_branch,&branch_target);
    set_rel_target(branch_target,vm_prims[p]);
  } else {
    unsigned j;

    codeaddr = primstart = append_prim(p);
  other_prim:
    for (j=0; j<pi->nimmargs; j++) {
      struct immarg *ia = &(pi->immargs[j]);
      Cell *argp = tcp + pi->nimmargs - j;
      Cell argval = *argp; /* !! specific to prims */
      if (ia->rel) { /* !! assumption: relative refs are branches */
	register_branchinfo(primstart + ia->offset, argp);
      } else /* plain argument */
	*(Cell *)(primstart + ia->offset) = argval;
    }
  }
  if (next_code_target!=NULL)
    *next_code_target = (Cell)code_here;
  return (Cell)codeaddr;
}
#else /* !defined(NO_IP) */
static Cell compile_prim_dyn(PrimNum p, Cell *tcp)
     /* compile prim #p dynamically (mod flags etc.) and return start
        address of generated code for putting it into the threaded code */
{
  Cell static_prim = (Cell)vm_prims[p];
#if defined(NO_DYNAMIC)
  return static_prim;
#else /* !defined(NO_DYNAMIC) */
  Address old_code_here;

  if (no_dynamic)
    return static_prim;
  if (p>=npriminfos || !is_relocatable(p)) {
    append_jump();
    return static_prim;
  }
  old_code_here = append_prim(p);
  last_jump = p;
  if (priminfos[p].superend)
    append_jump();
  return (Cell)old_code_here;
#endif  /* !defined(NO_DYNAMIC) */
}
#endif /* !defined(NO_IP) */
#endif

#ifndef NO_DYNAMIC
static int cost_codesize(int prim)
{
  return priminfos[prim].length;
}
#endif

static int cost_ls(int prim)
{
  struct cost *c = super_costs+prim;

  return c->loads + c->stores;
}

static int cost_lsu(int prim)
{
  struct cost *c = super_costs+prim;

  return c->loads + c->stores + c->updates;
}

static int cost_nexts(int prim)
{
  return 1;
}

typedef int Costfunc(int);
Costfunc *ss_cost =  /* cost function for optimize_bb */
#ifdef NO_DYNAMIC
cost_lsu;
#else
cost_codesize;
#endif

struct {
  Costfunc *costfunc;
  char *metricname;
  long sum;
} cost_sums[] = {
#ifndef NO_DYNAMIC
  { cost_codesize, "codesize", 0 },
#endif
  { cost_ls,       "ls",       0 },
  { cost_lsu,      "lsu",      0 },
  { cost_nexts,    "nexts",    0 }
};

#ifndef NO_DYNAMIC
void init_ss_cost(void) {
  if (no_dynamic && ss_cost == cost_codesize) {
    ss_cost = cost_nexts;
    cost_sums[0] = cost_sums[1]; /* don't use cost_codesize for print-metrics */
    debugp(stderr, "--no-dynamic conflicts with --ss-min-codesize, reverting to --ss-min-nexts\n");
  }
}
#endif

#define MAX_BB 128 /* maximum number of instructions in BB */
#define INF_COST 1000000 /* infinite cost */
#define CANONICAL_STATE 0

struct waypoint {
  int cost;     /* the cost from here to the end */
  PrimNum inst; /* the inst used from here to the next waypoint */
  char relocatable; /* the last non-transition was relocatable */
  char no_transition; /* don't use the next transition (relocatability)
		       * or this transition (does not change state) */
};

struct tpa_state { /* tree parsing automaton (like) state */
  /* labeling is back-to-front */
  struct waypoint *inst;  /* in front of instruction */
  struct waypoint *trans; /* in front of instruction and transition */
}; 

struct tpa_state *termstate = NULL; /* initialized in loader() */

/* statistics about tree parsing (lazyburg) stuff */
long lb_basic_blocks = 0;
long lb_labeler_steps = 0;
long lb_labeler_automaton = 0;
long lb_labeler_dynprog = 0;
long lb_newstate_equiv = 0;
long lb_newstate_new = 0;
long lb_applicable_base_rules = 0;
long lb_applicable_chain_rules = 0;

#if !(defined(DOUBLY_INDIRECT) || defined(INDIRECT_THREADED))
static void init_waypoints(struct waypoint ws[])
{
  int k;

  for (k=0; k<maxstates; k++)
    ws[k].cost=INF_COST;
}

static struct tpa_state *empty_tpa_state()
{
  struct tpa_state *s = malloc(sizeof(struct tpa_state));

  s->inst  = calloc(maxstates,sizeof(struct waypoint));
  init_waypoints(s->inst);
  s->trans = calloc(maxstates,sizeof(struct waypoint));
  /* init_waypoints(s->trans);*/
  return s;
}

static void transitions(struct tpa_state *t)
{
  int k;
  struct super_state *l;
  
  for (k=0; k<maxstates; k++) {
    t->trans[k] = t->inst[k];
    t->trans[k].no_transition = 1;
  }
  for (l = state_transitions; l != NULL; l = l->next) {
    PrimNum s = l->super;
    int jcost;
    struct cost *c=super_costs+s;
    struct waypoint *wi=&(t->trans[c->state_in]);
    struct waypoint *wo=&(t->inst[c->state_out]);
    lb_applicable_chain_rules++;
    if (wo->cost == INF_COST)
      continue;
    jcost = wo->cost + ss_cost(s);
    if (jcost <= wi->cost) {
      wi->cost = jcost;
      wi->inst = s;
      wi->relocatable = wo->relocatable;
      wi->no_transition = 0;
      /* if (ss_greedy) wi->cost = wo->cost ? */
    }
  }
}

static struct tpa_state *make_termstate()
{
  struct tpa_state *s = empty_tpa_state();

  s->inst[CANONICAL_STATE].cost = 0;
  transitions(s);
  return s;
}
#endif

#define TPA_SIZE 16384

struct tpa_entry {
  struct tpa_entry *next;
  PrimNum inst;
  struct tpa_state *state_behind;  /* note: brack-to-front labeling */
  struct tpa_state *state_infront; /* note: brack-to-front labeling */
} *tpa_table[TPA_SIZE];

#if !(defined(DOUBLY_INDIRECT) || defined(INDIRECT_THREADED))
static Cell hash_tpa(PrimNum p, struct tpa_state *t)
{
  UCell it = (UCell )t;
  return (p+it+(it>>14))&(TPA_SIZE-1);
}

static struct tpa_state **lookup_tpa(PrimNum p, struct tpa_state *t2)
{
  int hash=hash_tpa(p, t2);
  struct tpa_entry *te = tpa_table[hash];

  if (tpa_noautomaton) {
    static struct tpa_state *t;
    t = NULL;
    return &t;
  }
  for (; te!=NULL; te = te->next) {
    if (p == te->inst && t2 == te->state_behind)
      return &(te->state_infront);
  }
  te = (struct tpa_entry *)malloc(sizeof(struct tpa_entry));
  te->next = tpa_table[hash];
  te->inst = p;
  te->state_behind = t2;
  te->state_infront = NULL;
  tpa_table[hash] = te;
  return &(te->state_infront);
}

static void tpa_state_normalize(struct tpa_state *t)
{
  /* normalize so cost of canonical state=0; this may result in
     negative costs for some states */
  int d = t->inst[CANONICAL_STATE].cost;
  int i;

  for (i=0; i<maxstates; i++) {
    if (t->inst[i].cost != INF_COST)
      t->inst[i].cost -= d;
    if (t->trans[i].cost != INF_COST)
      t->trans[i].cost -= d;
  }
}

static int tpa_state_equivalent(struct tpa_state *t1, struct tpa_state *t2)
{
  return (memcmp(t1->inst, t2->inst, maxstates*sizeof(struct waypoint)) == 0 &&
	  memcmp(t1->trans,t2->trans,maxstates*sizeof(struct waypoint)) == 0);
}
#endif

struct tpa_state_entry {
  struct tpa_state_entry *next;
  struct tpa_state *state;
} *tpa_state_table[TPA_SIZE];

#if !(defined(DOUBLY_INDIRECT) || defined(INDIRECT_THREADED))
static Cell hash_tpa_state(struct tpa_state *t)
{
  int *ti = (int *)(t->inst);
  int *tt = (int *)(t->trans);
  int r=0;
  int i;

  for (i=0; ti+i < (int *)(t->inst+maxstates); i++)
    r += ti[i]+tt[i];
  return (r+(r>>14)+(r>>22)) & (TPA_SIZE-1);
}

static struct tpa_state *lookup_tpa_state(struct tpa_state *t)
{
  Cell hash = hash_tpa_state(t);
  struct tpa_state_entry *te = tpa_state_table[hash];
  struct tpa_state_entry *tn;

  if (!tpa_noequiv) {
    for (; te!=NULL; te = te->next) {
      if (tpa_state_equivalent(t, te->state)) {
	lb_newstate_equiv++;
	free(t->inst);
	free(t->trans);
	free(t);
	return te->state;
      }
    }
    tn = (struct tpa_state_entry *)malloc(sizeof(struct tpa_state_entry));
    tn->next = te;
    tn->state = t;
    tpa_state_table[hash] = tn;
  }
  lb_newstate_new++;
  if (tpa_trace)
    fprintf(stderr, "%ld %ld lb_states\n", lb_labeler_steps, lb_newstate_new);
  return t;
}

/* use dynamic programming to find the shortest paths within the basic
   block origs[0..ninsts-1] and rewrite the instructions pointed to by
   instps to use it */
static void optimize_rewrite(Cell *instps[], PrimNum origs[], int ninsts)
{
  int i,j;
  struct tpa_state *ts[ninsts+1];
  int nextdyn, nextstate, no_transition;
  Address old_code_area;
  
  lb_basic_blocks++;
  ts[ninsts] = termstate;
#ifndef NO_DYNAMIC
  if (print_sequences) {
    for (i=0; i<ninsts; i++)
#if defined(BURG_FORMAT)
      fprintf(stderr, "op%d ", super_costs[origs[i]].offset);
#else
      fprintf(stderr, "%s ", prim_names[origs[i]]);
#endif
    fprintf(stderr, "\n");
  }
#endif
  for (i=ninsts-1; i>=0; i--) {
    struct tpa_state **tp = lookup_tpa(origs[i],ts[i+1]);
    struct tpa_state *t = *tp;
    lb_labeler_steps++;
    if (t) {
      ts[i] = t;
      lb_labeler_automaton++;
    }
    else {
      lb_labeler_dynprog++;
      ts[i] = empty_tpa_state();
      for (j=1; j<=max_super && i+j<=ninsts; j++) {
	struct super_state **superp = lookup_super(origs+i, j);
	if (superp!=NULL) {
	  struct super_state *supers = *superp;
	  for (; supers!=NULL; supers = supers->next) {
	    PrimNum s = supers->super;
	    int jcost;
	    struct cost *c=super_costs+s;
	    struct waypoint *wi=&(ts[i]->inst[c->state_in]);
	    struct waypoint *wo=&(ts[i+j]->trans[c->state_out]);
	    int no_transition = wo->no_transition;
	    lb_applicable_base_rules++;
	    if (!(is_relocatable(s)) && !wo->relocatable) {
	      wo=&(ts[i+j]->inst[c->state_out]);
	      no_transition=1;
	    }
	    if (wo->cost == INF_COST) 
	      continue;
	    jcost = wo->cost + ss_cost(s);
	    if (jcost <= wi->cost) {
	      wi->cost = jcost;
	      wi->inst = s;
	      wi->relocatable = is_relocatable(s);
	      wi->no_transition = no_transition;
	      /* if (ss_greedy) wi->cost = wo->cost ? */
	    }
	  }
	}
      }
      transitions(ts[i]);
      tpa_state_normalize(ts[i]);
      *tp = ts[i] = lookup_tpa_state(ts[i]);
      if (tpa_trace)
	fprintf(stderr, "%ld %ld lb_table_entries\n", lb_labeler_steps, lb_labeler_dynprog);
    }
  }
  /* now rewrite the instructions */
  reserve_code_super(origs,ninsts);
  old_code_area = code_area;
  nextdyn=0;
  nextstate=CANONICAL_STATE;
  no_transition = ((!ts[0]->trans[nextstate].relocatable) 
		   ||ts[0]->trans[nextstate].no_transition);
  for (i=0; i<ninsts; i++) {
    Cell tc=0, tc2;
    if (i==nextdyn) {
      if (!no_transition) {
	/* process trans */
	PrimNum p = ts[i]->trans[nextstate].inst;
	struct cost *c = super_costs+p;
	assert(ts[i]->trans[nextstate].cost != INF_COST);
	assert(c->state_in==nextstate);
	tc = compile_prim_dyn(p,NULL);
	nextstate = c->state_out;
      }
      {
	/* process inst */
	PrimNum p = ts[i]->inst[nextstate].inst;
	struct cost *c=super_costs+p;
	assert(c->state_in==nextstate);
	assert(ts[i]->inst[nextstate].cost != INF_COST);
#if defined(GFORTH_DEBUGGING)
	assert(p == origs[i]);
#endif
	tc2 = compile_prim_dyn(p,instps[i]);
	if (no_transition || !is_relocatable(p))
	  /* !! actually what we care about is if and where
	   * compile_prim_dyn() puts NEXTs */
	  tc=tc2;
	no_transition = ts[i]->inst[nextstate].no_transition;
	nextstate = c->state_out;
	nextdyn += c->length;
      }
    } else {
#if defined(GFORTH_DEBUGGING)
      assert(0);
#endif
      tc=0;
      /* tc= (Cell)vm_prims[ts[i]->inst[CANONICAL_STATE].inst]; */
    }
    *(instps[i]) = tc;
  }      
  if (!no_transition) {
    PrimNum p = ts[i]->trans[nextstate].inst;
    struct cost *c = super_costs+p;
    assert(c->state_in==nextstate);
    assert(ts[i]->trans[nextstate].cost != INF_COST);
    assert(i==nextdyn);
    (void)compile_prim_dyn(p,NULL);
    nextstate = c->state_out;
  }
  assert(nextstate==CANONICAL_STATE);
  assert(code_area==old_code_area); /* does reserve_code_super() work? */
}
#endif

/* compile *start, possibly rewriting it into a static and/or dynamic
   superinstruction */
void compile_prim1(Cell *start)
{
#if defined(DOUBLY_INDIRECT)
  Label prim;

  if (start==NULL)
    return;
  prim = (Label)*start;
  if (prim<((Label)(xts+DOER_MAX)) || prim>((Label)(xts+npriminfos))) {
    fprintf(stderr,"compile_prim encountered xt %p\n", prim);
    *start=(Cell)prim;
    return;
  } else {
    *start = (Cell)(prim-((Label)xts)+((Label)vm_prims));
    return;
  }
#elif defined(INDIRECT_THREADED)
  return;
#else /* !(defined(DOUBLY_INDIRECT) || defined(INDIRECT_THREADED)) */
  static Cell *instps[MAX_BB];
  static PrimNum origs[MAX_BB];
  static int ninsts=0;
  PrimNum prim_num;

  if (start==NULL || ninsts >= MAX_BB ||
      (ninsts>0 && superend[origs[ninsts-1]])) {
    /* after bb, or at the start of the next bb */
    optimize_rewrite(instps,origs,ninsts);
    /* fprintf(stderr,"optimize_rewrite(...,%d)\n",ninsts); */
    ninsts=0;
    if (start==NULL) {
      align_code();
      return;
    }
  }
  prim_num = ((Xt)*start)-vm_prims;
  if(prim_num >= npriminfos) {
    /* code word */
    optimize_rewrite(instps,origs,ninsts);
    /* fprintf(stderr,"optimize_rewrite(...,%d)\n",ninsts);*/
    ninsts=0;
    append_jump();
    *start = *(Cell *)*start;
    return;
  }    
  assert(ninsts<MAX_BB);
  instps[ninsts] = start;
  origs[ninsts] = prim_num;
  ninsts++;
#endif /* !(defined(DOUBLY_INDIRECT) || defined(INDIRECT_THREADED)) */
}

void gforth_init()
{
#if 0 && defined(__i386)
  /* disabled because the drawbacks may be worse than the benefits */
  /* set 387 precision control to use 53-bit mantissae to avoid most
     cases of double rounding */
  short fpu_control = 0x027f ;
  asm("fldcw %0" : : "m"(fpu_control));
#endif /* defined(__i386) */

#ifdef MACOSX_DEPLOYMENT_TARGET
  setenv("MACOSX_DEPLOYMENT_TARGET", MACOSX_DEPLOYMENT_TARGET, 0);
#endif
#ifdef LTDL_LIBRARY_PATH
  setenv("LTDL_LIBRARY_PATH", LTDL_LIBRARY_PATH, 0);
#endif
#ifndef STANDALONE
  /* buffering of the user output device */
#ifdef _IONBF
  if (isatty(fileno(stdout))) {
    fflush(stdout);
    setvbuf(stdout,NULL,_IONBF,0);
  }
#endif
  setlocale(LC_ALL, "");
  setlocale(LC_NUMERIC, "C");
#else
  prep_terminal();
#endif

#ifndef STANDALONE
#ifdef HAVE_LIBLTDL
  if (lt_dlinit()!=0) {
    fprintf(stderr,"%s: lt_dlinit failed", progname);
    exit(1);
  }
#endif
#ifdef HAS_OS
#ifndef NO_DYNAMIC
  init_ss_cost();
#endif /* !defined(NO_DYNAMIC) */
#endif /* defined(HAS_OS) */
#endif
  code_here = ((void *)0)+code_area_size;

  get_winsize();
   
  install_signal_handlers(); /* right place? */
}

/* pointer to last '/' or '\' in file, 0 if there is none. */
static char *onlypath(char *filename)
{
  return strrchr(filename, DIRSEP);
}

static FILE *openimage(char *fullfilename)
{
  FILE *image_file;
  char * expfilename = tilde_cstr((Char *)fullfilename, strlen(fullfilename));

  image_file=fopen(expfilename,"rb");
  if (image_file!=NULL && debug)
    fprintf(stderr, "Opened image file: %s\n", expfilename);
  free(expfilename);
  return image_file;
}

/* try to open image file concat(path[0:len],imagename) */
static FILE *checkimage(char *path, int len, char *imagename)
{
  int dirlen=len;
  char fullfilename[dirlen+strlen((char *)imagename)+2];

  memcpy(fullfilename, path, dirlen);
  if (fullfilename[dirlen-1]!=DIRSEP)
    fullfilename[dirlen++]=DIRSEP;
  strcpy(fullfilename+dirlen,imagename);
  return openimage(fullfilename);
}

static FILE * open_image_file(char * imagename, char * path)
{
  FILE * image_file=NULL;
  char *origpath=path;
  
  if(strchr(imagename, DIRSEP)==NULL) {
    /* first check the directory where the exe file is in !! 01may97jaw */
    if (onlypath(progname))
      image_file=checkimage(progname, onlypath(progname)-progname, imagename);
    if (!image_file)
      do {
	char *pend=strchr(path, PATHSEP);
	if (pend==NULL)
	  pend=path+strlen(path);
	if (strlen(path)==0) break;
	image_file=checkimage(path, pend-path, imagename);
	path=pend+(*pend==PATHSEP);
      } while (image_file==NULL);
  } else {
    image_file=openimage(imagename);
  }

  if (!image_file) {
    fprintf(stderr,"%s: cannot open image file %s in path %s for reading\n",
	    progname, imagename, origpath);
    exit(1);
  }

  return image_file;
}

#ifdef STANDALONE
Address gforth_loader(char* imagename, char* path)
{
  gforth_init();
  return gforth_engine(0 sr_call);
}
#else
Address gforth_loader(char* imagename, char* path)
/* returns the address of the image proper (after the preamble) */
{
  ImageHeader header;
  Address image;
  Address imp; /* image+preamble */
  Char magic[8];
  char magic7; /* size byte of magic number */
  Cell preamblesize=0;
  Cell data_offset = offset_image ? 56*sizeof(Cell) : 0;
  UCell check_sum;
  Cell ausize = ((RELINFOBITS ==  8) ? 0 :
		 (RELINFOBITS == 16) ? 1 :
		 (RELINFOBITS == 32) ? 2 : 3);
  Cell charsize = ((sizeof(Char) == 1) ? 0 :
		   (sizeof(Char) == 2) ? 1 :
		   (sizeof(Char) == 4) ? 2 : 3) + ausize;
  Cell cellsize = ((sizeof(Cell) == 1) ? 0 :
		   (sizeof(Cell) == 2) ? 1 :
		   (sizeof(Cell) == 4) ? 2 : 3) + ausize;
  Cell sizebyte = (ausize << 5) + (charsize << 3) + (cellsize << 1) +
#ifdef WORDS_BIGENDIAN
       0
#else
       1
#endif
    ;
  FILE* imagefile=open_image_file(imagename, path);

  gforth_init();

  vm_prims = gforth_engine(0 sr_call);
  check_prims(vm_prims);
  prepare_super_table();
#ifndef DOUBLY_INDIRECT
#ifdef PRINT_SUPER_LENGTHS
  print_super_lengths();
#endif
  check_sum = checksum(vm_prims);
#else /* defined(DOUBLY_INDIRECT) */
  check_sum = (UCell)vm_prims;
#endif /* defined(DOUBLY_INDIRECT) */
#if !(defined(DOUBLY_INDIRECT) || defined(INDIRECT_THREADED))
  termstate = make_termstate();
#endif /* !(defined(DOUBLY_INDIRECT) || defined(INDIRECT_THREADED)) */
  
  do {
    if(fread(magic,sizeof(Char),8,imagefile) < 8) {
      fprintf(stderr,"%s: image %s doesn't seem to be a Gforth (>=0.8) image.\n",
	      progname, imagename);
      exit(1);
    }
    preamblesize+=8;
  } while(memcmp(magic,"Gforth4",7));
  magic7 = magic[7];
  if (debug) {
    magic[7]='\0';
    fprintf(stderr,"Magic found: %s ", magic);
    print_sizes(magic7);
  }

  if (magic7 != sizebyte)
    {
      fprintf(stderr,"This image is:         ");
      print_sizes(magic7);
      fprintf(stderr,"whereas the machine is ");
      print_sizes(sizebyte);
      exit(-2);
    };

  fread((void *)&header,sizeof(ImageHeader),1,imagefile);

  set_stack_sizes(&header);
  
#if HAVE_GETPAGESIZE
  pagesize=getpagesize(); /* Linux/GNU libc offers this */
#elif HAVE_SYSCONF && defined(_SC_PAGESIZE)
  pagesize=sysconf(_SC_PAGESIZE); /* POSIX.4 */
#elif PAGESIZE
  pagesize=PAGESIZE; /* in limits.h according to Gallmeister's POSIX.4 book */
#endif
  debugp(stderr,"pagesize=%ld\n",(unsigned long) pagesize);

  image = dict_alloc_read(imagefile, preamblesize+header.image_size,
			  dictsize, data_offset);
  imp=image+preamblesize;

  if (clear_dictionary)
    memset(imp+header.image_size, 0, dictsize-header.image_size-preamblesize);
  if(header.base==0 || header.base  == (Address)0x100) {
    Cell reloc_size=((header.image_size-1)/sizeof(Cell))/8+1;
    Char reloc_bits[reloc_size];
    fseek(imagefile, preamblesize+header.image_size, SEEK_SET);
    fread(reloc_bits, 1, reloc_size, imagefile);
    gforth_relocate((Cell *)imp, reloc_bits, header.image_size, (Cell)header.base, vm_prims);
#if 0
    { /* let's see what the relocator did */
      FILE *snapshot=fopen("snapshot.fi","wb");
      fwrite(image,1,imagesize,snapshot);
      fclose(snapshot);
    }
#endif
  }
  else if(header.base!=imp) {
    fprintf(stderr,"%s: Cannot load nonrelocatable image (compiled for address %p) at address %p\n",
	    progname, header.base, imp);
    exit(1);
  }
  if (header.checksum==0)
    ((ImageHeader *)imp)->checksum=check_sum;
  else if (header.checksum != check_sum) {
    fprintf(stderr,"%s: Checksum of image ($%lx) does not match the executable ($%lx)\n",
	    progname, header.checksum, check_sum);
    exit(1);
  }
#ifdef DOUBLY_INDIRECT
  ((ImageHeader *)imp)->xt_base = xts;
#endif
  fclose(imagefile);

  /* unnecessary, except maybe for CODE words */
  /* FLUSH_ICACHE(imp, header.image_size);*/

  return imp;
}
#endif
#endif

#ifdef STANDALONE_ALLOC
Address gforth_alloc(Cell size)
{
  Address r;
  /* leave a little room (64B) for stack underflows */
  if ((r = malloc(size+64))==NULL) {
    perror(progname);
    exit(1);
  }
  r = (Address)((((Cell)r)+(sizeof(Float)-1))&(-sizeof(Float)));
  debugp(stderr, "malloc succeeds, address=%p\n", r);
  return r;
}
#endif

#ifdef HAS_OS
static UCell convsize(char *s, UCell elemsize)
/* converts s of the format [0-9]+[bekMGT]? (e.g. 25k) into the number
   of bytes.  the letter at the end indicates the unit, where e stands
   for the element size. default is e */
{
  char *endp;
  UCell n,m;

  m = elemsize;
  n = strtoul(s,&endp,0);
  if (endp!=NULL) {
    if (strcmp(endp,"b")==0)
      m=1;
    else if (strcmp(endp,"k")==0)
      m=1024;
    else if (strcmp(endp,"M")==0)
      m=1024*1024;
    else if (strcmp(endp,"G")==0)
      m=1024*1024*1024;
    else if (strcmp(endp,"T")==0) {
#if (SIZEOF_CHAR_P > 4)
      m=1024L*1024*1024*1024;
#else
      fprintf(stderr,"%s: size specification \"%s\" too large for this machine\n", progname, endp);
      exit(1);
#endif
    } else if (strcmp(endp,"e")!=0 && strcmp(endp,"")!=0) {
      fprintf(stderr,"%s: cannot grok size specification %s: invalid unit \"%s\"\n", progname, s, endp);
      exit(1);
    }
  }
  return n*m;
}

enum {
  ss_number = 256,
  ss_states,
  ss_min_codesize,
  ss_min_ls,
  ss_min_lsu,
  ss_min_nexts,
  opt_code_block_size,
};

static void print_diag()
{

#if !defined(HAVE_GETRUSAGE)
  fprintf(stderr, "*** missing functionality ***\n"
#ifndef HAVE_GETRUSAGE
	  "    no getrusage -> CPUTIME broken\n"
#endif
	  );
#endif
  if((relocs < nonrelocs) ||
#if defined(BUGGY_LL_CMP) || defined(BUGGY_LL_MUL) || defined(BUGGY_LL_DIV) || defined(BUGGY_LL_ADD) || defined(BUGGY_LL_SHIFT) || defined(BUGGY_LL_D2F) || defined(BUGGY_LL_F2D)
     1
#else
     0
#endif
     )
    debugp(stderr, "relocs: %d:%d\n", relocs, nonrelocs);
    fprintf(stderr, "*** %sperformance problems ***\n%s%s",
#if defined(BUGGY_LL_CMP) || defined(BUGGY_LL_MUL) || defined(BUGGY_LL_DIV) || defined(BUGGY_LL_ADD) || defined(BUGGY_LL_SHIFT) || defined(BUGGY_LL_D2F) || defined(BUGGY_LL_F2D) || !(defined(FORCE_REG) || defined(FORCE_REG_UNNECESSARY)) || defined(BUGGY_LONG_LONG)
	    "",
#else
	    "no ",
#endif
#if defined(BUGGY_LL_CMP) || defined(BUGGY_LL_MUL) || defined(BUGGY_LL_DIV) || defined(BUGGY_LL_ADD) || defined(BUGGY_LL_SHIFT) || defined(BUGGY_LL_D2F) || defined(BUGGY_LL_F2D)
	    "    double-cell integer type buggy ->\n        "
#ifdef BUGGY_LL_CMP
	    "double comparisons, "
#endif
#ifdef BUGGY_LL_MUL
	    "*/MOD */ M* UM* "
#endif
#ifdef BUGGY_LL_DIV
	    /* currently nothing is affected */
#endif
#ifdef BUGGY_LL_ADD
	    "M+ D+ D- DNEGATE "
#endif
#ifdef BUGGY_LL_SHIFT
	    "D2/ "
#endif
#ifdef BUGGY_LL_D2F
	    "D>F "
#endif
#ifdef BUGGY_LL_F2D
	    "F>D "
#endif
	    "\b\b slow\n"
#endif
#if !(defined(FORCE_REG) || defined(FORCE_REG_UNNECESSARY))
	    "    automatic register allocation: performance degradation possible\n"
#endif
	    "",
	    (relocs < nonrelocs) ? "no dynamic code generation (--debug for details) -> factor 2 slowdown\n" : "");
}

#ifdef STANDALONE
void gforth_args(int argc, char ** argv, char ** path, char ** imagename)
{
#ifdef HAS_OS
  *path = getenv("GFORTHPATH") ? : DEFAULTPATH;
#else
  *path = DEFAULTPATH;
#endif
  *imagename="gforth.fi";
}
#else
void gforth_args(int argc, char ** argv, char ** path, char ** imagename)
{
  int c;
#ifdef HAS_OS
  *path = getenv("GFORTHPATH") ? : DEFAULTPATH;
#else
  *path = DEFAULTPATH;
#endif
  *imagename="gforth.fi";
  progname = argv[0];

  opterr=0;
  while (1) {
    int option_index=0;
    static struct option opts[] = {
      {"appl-image", required_argument, NULL, 'a'},
      {"image-file", required_argument, NULL, 'i'},
      {"dictionary-size", required_argument, NULL, 'm'},
      {"data-stack-size", required_argument, NULL, 'd'},
      {"return-stack-size", required_argument, NULL, 'r'},
      {"fp-stack-size", required_argument, NULL, 'f'},
      {"locals-stack-size", required_argument, NULL, 'l'},
      {"vm-commit", no_argument, &map_noreserve, 0},
      {"path", required_argument, NULL, 'p'},
      {"version", no_argument, NULL, 'v'},
      {"help", no_argument, NULL, 'h'},
      /* put something != 0 into offset_image */
      {"offset-image", no_argument, &offset_image, 1},
      {"no-offset-im", no_argument, &offset_image, 0},
      {"clear-dictionary", no_argument, &clear_dictionary, 1},
      {"debug", no_argument, &debug, 1},
      {"diag", no_argument, NULL, 'D'},
      {"die-on-signal", no_argument, &die_on_signal, 1},
      {"ignore-async-signals", no_argument, &ignore_async_signals, 1},
      {"no-super", no_argument, &no_super, 1},
      {"no-dynamic", no_argument, &no_dynamic, 1},
      {"dynamic", no_argument, &no_dynamic, 0},
      {"code-block-size", required_argument, NULL, opt_code_block_size},
      {"print-metrics", no_argument, &print_metrics, 1},
      {"print-sequences", no_argument, &print_sequences, 1},
      {"ss-number", required_argument, NULL, ss_number},
      {"ss-states", required_argument, NULL, ss_states},
#ifndef NO_DYNAMIC
      {"ss-min-codesize", no_argument, NULL, ss_min_codesize},
#endif
      {"ss-min-ls",       no_argument, NULL, ss_min_ls},
      {"ss-min-lsu",      no_argument, NULL, ss_min_lsu},
      {"ss-min-nexts",    no_argument, NULL, ss_min_nexts},
      {"ss-greedy",       no_argument, &ss_greedy, 1},
      {"tpa-noequiv",     no_argument, &tpa_noequiv, 1},
      {"tpa-noautomaton", no_argument, &tpa_noautomaton, 1},
      {"tpa-trace",	  no_argument, &tpa_trace, 1},
      {0,0,0,0}
      /* no-init-file, no-rc? */
    };
    
    c = getopt_long(argc, argv, "+i:m:d:r:f:l:p:vhoncsxD", opts, &option_index);
    
    switch (c) {
    case EOF: return;
    case '?': optind--; return;
    case 'a': *imagename = optarg; return;
    case 'i': *imagename = optarg; break;
    case 'm': dictsize = convsize(optarg,sizeof(Cell)); break;
    case 'd': dsize = convsize(optarg,sizeof(Cell)); break;
    case 'r': rsize = convsize(optarg,sizeof(Cell)); break;
    case 'f': fsize = convsize(optarg,sizeof(Float)); break;
    case 'l': lsize = convsize(optarg,sizeof(Cell)); break;
    case 'p': *path = optarg; break;
    case 'o': offset_image = 1; break;
    case 'n': offset_image = 0; break;
    case 'c': clear_dictionary = 1; break;
    case 's': die_on_signal = 1; break;
    case 'x': debug = 1; break;
    case 'D': print_diag(); break;
    case 'v': fputs(PACKAGE_STRING"\n", stderr); exit(0);
    case opt_code_block_size: code_area_size = atoi(optarg); break;
    case ss_number: static_super_number = atoi(optarg); break;
    case ss_states: maxstates = max(min(atoi(optarg),MAX_STATE),1); break;
#ifndef NO_DYNAMIC
    case ss_min_codesize: ss_cost = cost_codesize; break;
#endif
    case ss_min_ls:       ss_cost = cost_ls;       break;
    case ss_min_lsu:      ss_cost = cost_lsu;      break;
    case ss_min_nexts:    ss_cost = cost_nexts;    break;
    case 'h': 
      fprintf(stderr, "Usage: %s [engine options] ['--'] [image arguments]\n\
Engine Options:\n\
  --appl-image FILE		    Equivalent to '--image-file=FILE --'\n\
  --clear-dictionary		    Initialize the dictionary with 0 bytes\n\
  --code-block-size=SIZE            size of native code blocks [512KB]\n\
  -d SIZE, --data-stack-size=SIZE   Specify data stack size\n\
  --debug			    Print debugging information during startup\n\
  -D, --diag			    Print diagnostic information during startup\n\
  --die-on-signal		    Exit instead of THROWing some signals\n\
  --dynamic			    Use dynamic native code\n\
  -f SIZE, --fp-stack-size=SIZE	    Specify floating point stack size\n\
  -h, --help			    Print this message and exit\n\
  --ignore-async-signals	    Ignore instead of THROWing async. signals\n\
  -i FILE, --image-file=FILE	    Use image FILE instead of `gforth.fi'\n\
  -l SIZE, --locals-stack-size=SIZE Specify locals stack size\n\
  -m SIZE, --dictionary-size=SIZE   Specify Forth dictionary size\n\
  --no-dynamic			    Use only statically compiled primitives\n\
  --no-offset-im		    Load image at normal position\n\
  --no-super			    No dynamically formed superinstructions\n\
  --offset-image		    Load image at a different position\n\
  -p PATH, --path=PATH		    Search path for finding image and sources\n\
  --print-metrics		    Print some code generation metrics on exit\n\
  --print-sequences		    Print primitive sequences for optimization\n\
  -r SIZE, --return-stack-size=SIZE Specify return stack size\n\
  --ss-greedy			    Greedy, not optimal superinst selection\n\
  --ss-min-codesize		    Select superinsts for smallest native code\n\
  --ss-min-ls			    Minimize loads and stores\n\
  --ss-min-lsu			    Minimize loads, stores, and pointer updates\n\
  --ss-min-nexts		    Minimize the number of static superinsts\n\
  --ss-number=N			    Use N static superinsts (default max)\n\
  --ss-states=N			    N states for stack caching (default max)\n\
  --tpa-noequiv			    Automaton without state equivalence\n\
  --tpa-noautomaton		    Dynamic programming only\n\
  --tpa-trace			    Report new states etc.\n\
  -v, --version			    Print engine version and exit\n\
  --vm-commit			    Use OS default for memory overcommit\n\
SIZE arguments consist of an integer followed by a unit. The unit can be\n\
  `b' (byte), `e' (element; default), `k' (KB), `M' (MB), `G' (GB) or `T' (TB).\n",
	      argv[0]);
      optind--;
      return;
    }
  }
}
#endif
#endif

#ifdef STANDALONE
Cell data_abort_pc;

void data_abort_C(void)
{
  while(1) {
  }
}
#endif

void* gforth_pointers(Cell n)
{
  switch(n) {
  case 0: return (void*)&gforth_SP;
  case 1: return (void*)&gforth_FP;
  case 2: return (void*)&gforth_LP;
  case 3: return (void*)&gforth_RP;
  case 4: return (void*)&gforth_UP;
  case 5: return (void*)&gforth_engine;
#ifdef HAS_FILE
  case 6: return (void*)&cstr;
  case 7: return (void*)&tilde_cstr;
#endif
  case 8: return (void*)&throw_jmp_handler;
  case 9: return (void*)&gforth_stacks;
  case 10: return (void*)&gforth_free_stacks;
  default: return NULL;
  }
}

void gforth_printmetrics()
{
  if (print_metrics) {
    int i;
    fprintf(stderr, "code size = %8ld\n", dyncodesize());
#ifndef STANDALONE
    for (i=0; i<sizeof(cost_sums)/sizeof(cost_sums[0]); i++)
      fprintf(stderr, "metric %8s: %8ld\n",
	      cost_sums[i].metricname, cost_sums[i].sum);
#endif
    fprintf(stderr,"lb_basic_blocks = %ld\n", lb_basic_blocks);
    fprintf(stderr,"lb_labeler_steps = %ld\n", lb_labeler_steps);
    fprintf(stderr,"lb_labeler_automaton = %ld\n", lb_labeler_automaton);
    fprintf(stderr,"lb_labeler_dynprog = %ld\n", lb_labeler_dynprog);
    fprintf(stderr,"lb_newstate_equiv = %ld\n", lb_newstate_equiv);
    fprintf(stderr,"lb_newstate_new = %ld\n", lb_newstate_new);
    fprintf(stderr,"lb_applicable_base_rules = %ld\n", lb_applicable_base_rules);
    fprintf(stderr,"lb_applicable_chain_rules = %ld\n", lb_applicable_chain_rules);
  }
  if (tpa_trace) {
    fprintf(stderr, "%ld %ld lb_states\n", lb_labeler_steps, lb_newstate_new);
    fprintf(stderr, "%ld %ld lb_table_entries\n", lb_labeler_steps, lb_labeler_dynprog);
  }
}

void gforth_cleanup()
{
#if defined(SIGPIPE) && !defined(STANDALONE)
  bsd_signal(SIGPIPE, SIG_IGN);
#endif
#ifdef VM_PROFILING
  vm_print_profile(stderr);
#endif
  deprep_terminal();
#ifndef STANDALONE
#ifdef HAVE_LIBLTDL
  if (lt_dlexit()!=0)
    fprintf(stderr,"%s: lt_dlexit failed", progname);
#endif
#endif
}

user_area* gforth_stacks(Cell dsize, Cell rsize, Cell fsize, Cell lsize)
{
#ifdef SIGSTKSZ
  stack_t sigstack;
  int sas_retval=-1;
#endif
  size_t totalsize;
  Cell a;
  user_area * up0;
  Cell dsizep = wholepage(dsize);
  Cell rsizep = wholepage(rsize);
  Cell fsizep = wholepage(fsize);
  Cell lsizep = wholepage(lsize);
  totalsize = dsizep+fsizep+rsizep+lsizep+6*pagesize;
#ifdef SIGSTKSZ
  totalsize += 2*SIGSTKSZ;
#endif
#ifdef HAVE_MMAP
  a = (Cell)alloc_mmap(totalsize);
  if (a != (Cell)MAP_FAILED) {
    up0=(user_area*)a; a+=pagesize;
    page_noaccess((void*)a); a+=pagesize; up0->sp0=a+dsize; a+=dsizep;
    page_noaccess((void*)a); a+=pagesize; up0->rp0=a+rsize; a+=rsizep;
    page_noaccess((void*)a); a+=pagesize; up0->fp0=a+fsize; a+=fsizep;
    page_noaccess((void*)a); a+=pagesize; up0->lp0=a+lsize; a+=lsizep;
    page_noaccess((void*)a); a+=pagesize;
#ifdef SIGSTKSZ
    sigstack.ss_sp=(void*)a+SIGSTKSZ;
    sigstack.ss_size=SIGSTKSZ;
    sas_retval=sigaltstack(&sigstack,(stack_t *)0);
#if defined(HAS_FILE) || !defined(STANDALONE)
    debugp(stderr,"sigaltstack: %s\n",strerror(sas_retval));
#endif
#endif
    return up0;
  }
  return 0;
#else
  a = (Cell)verbose_malloc(totalsize);
  if (a != NULL) {
    up0=(user_area*)a; a+=pagesize;
    a+=pagesize; up0->sp0=a+dsize; a+=dsizep;
    a+=pagesize; up0->rp0=a+rsize; a+=rsizep;
    a+=pagesize; up0->fp0=a+fsize; a+=fsizep;
    a+=pagesize; up0->lp0=a+lsize; a+=lsizep;
    a+=pagesize;
#ifdef SIGSTKSZ
    sigstack.ss_sp=(void*)a+SIGSTKSZ;
    sigstack.ss_size=SIGSTKSZ;
    sas_retval=sigaltstack(&sigstack,(stack_t *)0);
#if defined(HAS_FILE) || !defined(STANDALONE)
    debugp(stderr,"sigaltstack: %s\n",strerror(sas_retval));
#endif
#endif
    return up0;
  }
  return 0;
#endif
}

void gforth_free_stacks(user_area * t)
{
#if HAVE_GETPAGESIZE
  Cell pagesize=getpagesize(); /* Linux/GNU libc offers this */
#elif HAVE_SYSCONF && defined(_SC_PAGESIZE)
  Cell pagesize=sysconf(_SC_PAGESIZE); /* POSIX.4 */
#elif PAGESIZE
  Cell pagesize=PAGESIZE; /* in limits.h according to Gallmeister's POSIX.4 book */
#endif
  Cell size = wholepage((Cell)((t)->lp0)+pagesize-(Cell)t);
#ifdef SIGSTKSZ
  size += 2*SIGSTKSZ;
#endif
#ifdef HAVE_MMAP
  munmap(t, size);
#else
  free(t);
#endif
}

void gforth_setstacks()
{
  gforth_UP->next_task = NULL; /* mark user area as need-to-be-set */

  /* ensure that the cached elements (if any) are accessible */
#if !(defined(GFORTH_DEBUGGING) || defined(INDIRECT_THREADED) || defined(DOUBLY_INDIRECT) || defined(VM_PROFILING))
  gforth_UP->sp0 -= 8; /* make stuff below bottom accessible for stack caching */
  gforth_UP->fp0--;
#endif

  gforth_SP = gforth_UP->sp0;
  gforth_RP = gforth_UP->rp0;
  gforth_FP = gforth_UP->fp0;
  gforth_LP = gforth_UP->lp0;
}

int gforth_boot(int argc, char** argv, char* path)
{
  char *path2=malloc(strlen(path)+1);
  char *p1, *p2;
  
  argv[optind-1] = progname;
  
  /* make path OS-independent by replacing path separators with NUL */
  for (p1=path, p2=path2; *p1!='\0'; p1++, p2++)
    if (*p1==PATHSEP)
      *p2 = '\0';
    else
      *p2 = *p1;
  *p2='\0';
  
  *--gforth_SP=(Cell)path2;
  *--gforth_SP=(Cell)strlen(path);
  *--gforth_SP=(Cell)(argv+(optind-1));
  *--gforth_SP=(Cell)(argc-(optind-1));
  
  debugp(stderr, "Booting Gforth: %p\n", gforth_header->boot_entry);
  return gforth_go(gforth_header->boot_entry);
}

int gforth_quit()
{
  debugp(stderr, "Quit into Gforth: %p\n", gforth_header->quit_entry);
  return gforth_go(gforth_header->quit_entry);
}

int gforth_execute(Xt xt)
{
  debugp(stderr, "Execute Gforth xt %p: %p\n", xt, gforth_header->execute_entry);

  *--gforth_SP = (Cell)xt;

  return gforth_go(gforth_header->execute_entry);
}

Xt gforth_find(Char * name)
{
  Xt xt;
  debugp(stderr, "Find '%s' in Gforth: %p\n", name, gforth_header->find_entry);

  *--gforth_SP = (Cell)name;
  *--gforth_SP = strlen(name);

  xt = (Xt)gforth_go(gforth_header->find_entry);
  debugp(stderr, "Found %p\n", xt);
  return xt;
}

int gforth_start(int argc, char ** argv)
{
  char *path, *imagename;

  gforth_args(argc, argv, &path, &imagename);
  gforth_header = gforth_loader(imagename, path);
  gforth_UP = gforth_stacks(dsize, rsize, fsize, lsize);
  gforth_setstacks();
  return gforth_boot(argc, argv, path);
}

int gforth_main(int argc, char **argv, char **env)
{
  int retvalue=gforth_start(argc, argv);

  if(retvalue > 0) {
    gforth_execute(gforth_find("bootmessage"));
    retvalue = gforth_quit();
  }
  gforth_cleanup();
  gforth_printmetrics();
  gforth_free();

  return retvalue;
}

int gforth_make_image(int debugflag)
{
  char *argv0[] = { "gforth", "--clear-dictionary", "--no-offset-im", "--die-on-signal", "-i", KERNEL, "exboot.fs", "startup.fs", "arch/" ARCH "/asm.fs", "arch/" ARCH "/disasm.fs", "-e", "savesystem temp-file.fi1 bye" };
  char *argv1[] = { "gforth", "--clear-dictionary", "--offset-image", "--die-on-signal", "-i", KERNEL, "exboot.fs", "startup.fs", "arch/" ARCH "/asm.fs", "arch/" ARCH "/disasm.fs", "-e", "savesystem temp-file.fi2 bye" };
  char *argv2[] = { "gforth", "--die-on-signal", "-i", KERNEL, "exboot.fs", "startup.fs", "comp-i.fs", "-e", "comp-image temp-file.fi1 temp-file.fi2 gforth.fi bye" };
  const int argc0 = sizeof(argv0)/sizeof(char*);
  const int argc1 = sizeof(argv1)/sizeof(char*);
  const int argc2 = sizeof(argv2)/sizeof(char*);

  int retvalue;

  debug=debugflag;

  retvalue=gforth_start(argc0, argv0);
  gforth_free_stacks(gforth_UP);
  gforth_free();

  optind=1;

  retvalue=gforth_start(argc1, argv1);
  gforth_free_stacks(gforth_UP);
  gforth_free();

  optind=1;

  retvalue=gforth_start(argc2, argv2);
  gforth_free_stacks(gforth_UP);
  gforth_free();
  
  unlink("temp-file.fi1");
  unlink("temp-file.fi2");

  return retvalue;
}
