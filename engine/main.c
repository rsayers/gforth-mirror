/* command line interpretation, image loading etc. for Gforth


  Copyright (C) 1995,1996,1997,1998,2000,2003 Free Software Foundation, Inc.

  This file is part of Gforth.

  Gforth is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
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
#ifdef STANDALONE
#include <systypes.h>
#endif

enum {
/* definitions of N_execute etc. */
#include "prim_num.i"
  N_START_SUPER
};

/* global variables for engine.c 
   We put them here because engine.c is compiled several times in
   different ways for the same engine. */
Cell *SP;
Float *FP;
Address UP=NULL;

#ifdef HAS_FFCALL
Cell *RP;
Address LP;

#include <callback.h>

va_alist clist;

void engine_callback(Xt* fcall, void * alist)
{
  engine(fcall, SP, RP, FP, LP);
  clist = (va_alist)alist;
}
#endif

#ifdef GFORTH_DEBUGGING
/* define some VM registers as global variables, so they survive exceptions;
   global register variables are not up to the task (according to the 
   GNU C manual) */
Xt *saved_ip;
Cell *rp;
#endif

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
jmp_buf throw_jmp_buf;
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
#ifndef INCLUDE_IMAGE
static int clear_dictionary=0;
UCell pagesize=1;
char *progname;
#else
char *progname = "gforth";
int optind = 1;
#endif

#define CODE_BLOCK_SIZE (256*1024)
Address code_area=0;
Cell code_area_size = CODE_BLOCK_SIZE;
Address code_here=NULL+CODE_BLOCK_SIZE; /* does for code-area what HERE
					   does for the dictionary */
Address start_flush=NULL; /* start of unflushed code */
Cell last_jump=0; /* if the last prim was compiled without jump, this
                     is it's number, otherwise this contains 0 */

static int no_super=0;   /* true if compile_prim should not fuse prims */
static int no_dynamic=NO_DYNAMIC_DEFAULT; /* if true, no code is generated
					     dynamically */
static int print_metrics=0; /* if true, print metrics on exit */
static int static_super_number = 10000000; /* number of ss used if available */
static int ss_greedy = 0; /* if true: use greedy, not optimal ss selection */

#ifdef HAS_DEBUG
int debug=0;
#else
# define perror(x...)
# define fprintf(x...)
#endif

ImageHeader *gforth_header;
Label *vm_prims;
#ifdef DOUBLY_INDIRECT
Label *xts; /* same content as vm_prims, but should only be used for xts */
#endif

#ifdef MEMCMP_AS_SUBROUTINE
int gforth_memcmp(const char * s1, const char * s2, size_t n)
{
  return memcmp(s1, s2, n);
}
#endif

/* image file format:
 *  "#! binary-path -i\n" (e.g., "#! /usr/local/bin/gforth-0.4.0 -i\n")
 *   padding to a multiple of 8
 *   magic: "Gforth3x" means format 0.6,
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
 * If the word =CF(DOESJUMP), it's a DOES JUMP (2 Cells after DOES>,
 *					possibly containing a jump to dodoes)
 * If the word is <CF(DOESJUMP) and bit 14 is set, it's the xt of a primitive
 * If the word is <CF(DOESJUMP) and bit 14 is clear, 
 *                                        it's the threaded code of a primitive
 * bits 13..9 of a primitive token state which group the primitive belongs to,
 * bits 8..0 of a primitive token index into the group
 */

Cell groups[32] = {
  0,
DOESJUMP+1
#undef GROUP
#undef GROUPADD
#define GROUPADD(n) +n
#define GROUP(x, n) , 0
#include "prim_grp.i"
#undef GROUP
#undef GROUPADD
#define GROUP(x, n)
#define GROUPADD(n)
};

void relocate(Cell *image, const unsigned char *bitstring, 
              int size, Cell base, Label symbols[])
{
  int i=0, j, k, steps=(size/sizeof(Cell))/RELINFOBITS;
  Cell token;
  char bits;
  Cell max_symbols;
  /* 
   * A virtual start address that's the real start address minus 
   * the one in the image 
   */
  Cell *start = (Cell * ) (((void *) image) - ((void *) base));

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
  
  for (max_symbols=DOESJUMP+1; symbols[max_symbols]!=0; max_symbols++)
    ;
  max_symbols--;
  size/=sizeof(Cell);

  for(k=0; k<=steps; k++) {
    for(j=0, bits=bitstring[k]; j<RELINFOBITS; j++, i++, bits<<=1) {
      /*      fprintf(stderr,"relocate: image[%d]\n", i);*/
      if((i < size) && (bits & (1U << (RELINFOBITS-1)))) {
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
	    case CF(DOUSER)  : 
	    case CF(DODEFER) : 
	    case CF(DOFIELD) : MAKE_CF(image+i,symbols[CF(token)]); break;
	    case CF(DOESJUMP): image[i]=0; break;
#endif /* !defined(DOUBLY_INDIRECT) */
	    case CF(DODOES)  :
	      MAKE_DOES_CF(image+i,(Xt *)(image[i+1]+((Cell)start)));
	      break;
	    default          : /* backward compatibility */
/*	      printf("Code field generation image[%x]:=CFA(%x)\n",
		     i, CF(image[i])); */
	      if (CF((token | 0x4000))<max_symbols) {
		image[i]=(Cell)CFA(CF(token));
#ifdef DIRECT_THREADED
		if ((token & 0x4000) == 0) /* threade code, no CFA */
		  compile_prim1(&image[i]);
#endif
	      } else
		fprintf(stderr,"Primitive %ld used in this image at $%lx (offset $%x) is not implemented by this\n engine (%s); executing this code will crash.\n",(long)CF(token),(long)&image[i], i, PACKAGE_VERSION);
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
	      if ((token & 0x4000) == 0) /* threade code, no CFA */
		compile_prim1(&image[i]);
#endif
	    } else
	      fprintf(stderr,"Primitive %lx, %d of group %d used in this image at $%lx (offset $%x) is not implemented by this\n engine (%s); executing this code will crash.\n", (long)-token, tok, group, (long)&image[i],i,PACKAGE_VERSION);
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
  finish_code();
  ((ImageHeader*)(image))->base = (Address) image;
}

UCell checksum(Label symbols[])
{
  UCell r=PRIM_VERSION;
  Cell i;

  for (i=DOCOL; i<=DOESJUMP; i++) {
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

Address verbose_malloc(Cell size)
{
  Address r;
  /* leave a little room (64B) for stack underflows */
  if ((r = malloc(size+64))==NULL) {
    perror(progname);
    exit(1);
  }
  r = (Address)((((Cell)r)+(sizeof(Float)-1))&(-sizeof(Float)));
  if (debug)
    fprintf(stderr, "malloc succeeds, address=$%lx\n", (long)r);
  return r;
}

static Address next_address=0;
void after_alloc(Address r, Cell size)
{
  if (r != (Address)-1) {
    if (debug)
      fprintf(stderr, "success, address=$%lx\n", (long) r);
    if (pagesize != 1)
      next_address = (Address)(((((Cell)r)+size-1)&-pagesize)+2*pagesize); /* leave one page unmapped */
  } else {
    if (debug)
      fprintf(stderr, "failed: %s\n", strerror(errno));
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
#if !defined(MAP_ANON) && defined(MAP_ANONYMOUS)
# define MAP_ANON MAP_ANONYMOUS
#endif

#if defined(HAVE_MMAP)
static Address alloc_mmap(Cell size)
{
  Address r;

#if defined(MAP_ANON)
  if (debug)
    fprintf(stderr,"try mmap($%lx, $%lx, ..., MAP_ANON, ...); ", (long)next_address, (long)size);
  r = mmap(next_address, size, PROT_EXEC|PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
#else /* !defined(MAP_ANON) */
  /* Ultrix (at least) does not define MAP_FILE and MAP_PRIVATE (both are
     apparently defaults) */
  static int dev_zero=-1;

  if (dev_zero == -1)
    dev_zero = open("/dev/zero", O_RDONLY);
  if (dev_zero == -1) {
    r = MAP_FAILED;
    if (debug)
      fprintf(stderr, "open(\"/dev/zero\"...) failed (%s), no mmap; ", 
	      strerror(errno));
  } else {
    if (debug)
      fprintf(stderr,"try mmap($%lx, $%lx, ..., MAP_FILE, dev_zero, ...); ", (long)next_address, (long)size);
    r=mmap(next_address, size, PROT_EXEC|PROT_READ|PROT_WRITE, MAP_FILE|MAP_PRIVATE, dev_zero, 0);
  }
#endif /* !defined(MAP_ANON) */
  after_alloc(r, size);
  return r;  
}
#endif

Address my_alloc(Cell size)
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

Address dict_alloc_read(FILE *file, Cell imagesize, Cell dictsize, Cell offset)
{
  Address image = MAP_FAILED;

#if defined(HAVE_MMAP)
  if (offset==0) {
    image=alloc_mmap(dictsize);
    if (debug)
      fprintf(stderr,"try mmap($%lx, $%lx, ..., MAP_FIXED|MAP_FILE, imagefile, 0); ", (long)image, (long)imagesize);
    image = mmap(image, imagesize, PROT_EXEC|PROT_READ|PROT_WRITE, MAP_FIXED|MAP_FILE|MAP_PRIVATE, fileno(file), 0);
    after_alloc(image,dictsize);
  }
#endif /* defined(HAVE_MMAP) */
  if (image == (Address)MAP_FAILED) {
    image = my_alloc(dictsize+offset)+offset;
    rewind(file);  /* fseek(imagefile,0L,SEEK_SET); */
    fread(image, 1, imagesize, file);
  }
  return image;
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
}

void alloc_stacks(ImageHeader * header)
{
  header->dict_size=dictsize;
  header->data_stack_size=dsize;
  header->fp_stack_size=fsize;
  header->return_stack_size=rsize;
  header->locals_stack_size=lsize;

  header->data_stack_base=my_alloc(dsize);
  header->fp_stack_base=my_alloc(fsize);
  header->return_stack_base=my_alloc(rsize);
  header->locals_stack_base=my_alloc(lsize);
}

#warning You can ignore the warnings about clobbered variables in go_forth
int go_forth(Address image, int stack, Cell *entries)
{
  volatile ImageHeader *image_header = (ImageHeader *)image;
  Cell *sp0=(Cell*)(image_header->data_stack_base + dsize);
  Cell *rp0=(Cell *)(image_header->return_stack_base + rsize);
  Float *fp0=(Float *)(image_header->fp_stack_base + fsize);
#ifdef GFORTH_DEBUGGING
  volatile Cell *orig_rp0=rp0;
#endif
  Address lp0=image_header->locals_stack_base + lsize;
  Xt *ip0=(Xt *)(image_header->boot_entry);
#ifdef SYSSIGNALS
  int throw_code;
#endif

  /* ensure that the cached elements (if any) are accessible */
  IF_spTOS(sp0--);
  IF_fpTOS(fp0--);
  
  for(;stack>0;stack--)
    *--sp0=entries[stack-1];

#ifdef SYSSIGNALS
  get_winsize();
   
  install_signal_handlers(); /* right place? */
  
  if ((throw_code=setjmp(throw_jmp_buf))) {
    static Cell signal_data_stack[8];
    static Cell signal_return_stack[8];
    static Float signal_fp_stack[1];

    signal_data_stack[7]=throw_code;

#ifdef GFORTH_DEBUGGING
    if (debug)
      fprintf(stderr,"\ncaught signal, throwing exception %d, ip=%p rp=%p\n",
	      throw_code, saved_ip, rp);
    if (rp <= orig_rp0 && rp > (Cell *)(image_header->return_stack_base+5)) {
      /* no rstack overflow or underflow */
      rp0 = rp;
      *--rp0 = (Cell)saved_ip;
    }
    else /* I love non-syntactic ifdefs :-) */
      rp0 = signal_return_stack+8;
#else  /* !defined(GFORTH_DEBUGGING) */
    if (debug)
      fprintf(stderr,"\ncaught signal, throwing exception %d\n", throw_code);
      rp0 = signal_return_stack+8;
#endif /* !defined(GFORTH_DEBUGGING) */
    /* fprintf(stderr, "rp=$%x\n",rp0);*/
    
    return((int)(Cell)engine(image_header->throw_entry, signal_data_stack+7,
		       rp0, signal_fp_stack, 0));
  }
#endif

  return((int)(Cell)engine(ip0,sp0,rp0,fp0,lp0));
}

#ifndef INCLUDE_IMAGE
void print_sizes(Cell sizebyte)
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

struct cost {
  char loads;       /* number of stack loads */
  char stores;      /* number of stack stores */
  char updates;     /* number of stack pointer updates */
  short offset;      /* offset into super2 table */
  char length;      /* number of components */
};

short super2[] = {
#include "super2.i"
};

struct cost super_costs[] = {
#include "costs.i"
};

#define HASH_SIZE 256

struct super_table_entry {
  struct super_table_entry *next;
  short *start;
  short length;
  short super;
} *super_table[HASH_SIZE];
int max_super=2;

int hash_super(short *start, int length)
{
  int i, r;
  
  for (i=0, r=0; i<length; i++) {
    r <<= 1;
    r += start[i];
  }
  return r & (HASH_SIZE-1);
}

int lookup_super(short *start, int length)
{
  int hash=hash_super(start,length);
  struct super_table_entry *p = super_table[hash];

  assert(length >= 2);
  for (; p!=NULL; p = p->next) {
    if (length == p->length &&
	memcmp((char *)p->start, (char *)start, length*sizeof(short))==0)
      return p->super;
  }
  return -1;
}

void prepare_super_table()
{
  int i;
  int nsupers = 0;

  for (i=0; i<sizeof(super_costs)/sizeof(super_costs[0]); i++) {
    struct cost *c = &super_costs[i];
    if (c->length > 1 && nsupers < static_super_number) {
      int hash = hash_super(super2+c->offset, c->length);
      struct super_table_entry **p = &super_table[hash];
      struct super_table_entry *e = malloc(sizeof(struct super_table_entry));
      e->next = *p;
      e->start = super2 + c->offset;
      e->length = c->length;
      e->super = i;
      *p = e;
      if (c->length > max_super)
	max_super = c->length;
      nsupers++;
    }
  }
  if (debug)
    fprintf(stderr, "Using %d static superinsts\n", nsupers);
}

/* dynamic replication/superinstruction stuff */

#define MAX_IMMARGS 2

#ifndef NO_DYNAMIC
typedef struct {
  Label start;
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

int compare_priminfo_length(const void *_a, const void *_b)
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

static char superend[]={
#include "prim_superend.i"
};

Cell npriminfos=0;

int compare_labels(const void *pa, const void *pb)
{
  Label a = *(Label *)pa;
  Label b = *(Label *)pb;
  return a-b;
}

Label bsearch_next(Label key, Label *a, UCell n)
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

void check_prims(Label symbols1[])
{
  int i;
#ifndef NO_DYNAMIC
  Label *symbols2, *symbols3, *ends1, *ends1j, *ends1jsorted;
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
  for (i=DOESJUMP+1; symbols1[i+1]!=0; i++)
    ;
  npriminfos = i;
  
#ifndef NO_DYNAMIC
  if (no_dynamic)
    return;
  symbols2=engine2(0,0,0,0,0);
#if NO_IP
  symbols3=engine3(0,0,0,0,0);
#else
  symbols3=symbols1;
#endif
  ends1 = symbols1+i+1-DOESJUMP;
  ends1j =   ends1+i;
  nends1j = i-DOESJUMP;
  ends1jsorted = (Label *)alloca(nends1j*sizeof(Label));
  memcpy(ends1jsorted,ends1j,nends1j*sizeof(Label));
  qsort(ends1jsorted, nends1j, sizeof(Label), compare_labels);
  
  priminfos = calloc(i,sizeof(PrimInfo));
  for (i=DOESJUMP+1; symbols1[i+1]!=0; i++) {
    int prim_len = ends1[i]-symbols1[i];
    PrimInfo *pi=&priminfos[i];
    int j=0;
    char *s1 = (char *)symbols1[i];
    char *s2 = (char *)symbols2[i];
    char *s3 = (char *)symbols3[i];
    Label endlabel = bsearch_next(symbols1[i]+1,ends1jsorted,nends1j);

    pi->start = s1;
    pi->superend = superend[i-DOESJUMP-1]|no_super;
    if (pi->superend)
      pi->length = endlabel-symbols1[i];
    else
      pi->length = prim_len;
    pi->restlength = endlabel - symbols1[i] - pi->length;
    pi->nimmargs = 0;
    if (debug)
      fprintf(stderr, "Prim %3d @ %p %p %p, length=%3ld restlength=%2ld superend=%1d",
	      i, s1, s2, s3, (long)(pi->length), (long)(pi->restlength), pi->superend);
    if (endlabel == NULL) {
      pi->start = NULL; /* not relocatable */
      if (debug)
	fprintf(stderr,"\n   non_reloc: no J label > start found\n");
      continue;
    }
    if (ends1[i] > endlabel && !pi->superend) {
      pi->start = NULL; /* not relocatable */
      if (debug)
	fprintf(stderr,"\n   non_reloc: there is a J label before the J label (restlength<0)\n");
      continue;
    }
    if (ends1[i] < pi->start && !pi->superend) {
      pi->start = NULL; /* not relocatable */
      if (debug)
	fprintf(stderr,"\n   non_reloc: K label before I label (length<0)\n");
      continue;
    }
    assert(prim_len>=0);
    assert(pi->restlength >=0);
    while (j<(pi->length+pi->restlength)) {
      if (s1[j]==s3[j]) {
	if (s1[j] != s2[j]) {
	  pi->start = NULL; /* not relocatable */
	  if (debug)
	    fprintf(stderr,"\n   non_reloc: engine1!=engine2 offset %3d",j);
	  /* assert(j<prim_len); */
	  break;
	}
	j++;
      } else {
	struct immarg *ia=&pi->immargs[pi->nimmargs];

	pi->nimmargs++;
	ia->offset=j;
	if ((~*(Cell *)&(s1[j]))==*(Cell *)&(s3[j])) {
	  ia->rel=0;
	  if (debug)
	    fprintf(stderr,"\n   absolute immarg: offset %3d",j);
	} else if ((&(s1[j]))+(*(Cell *)&(s1[j]))+4 ==
		   symbols1[DOESJUMP+1]) {
	  ia->rel=1;
	  if (debug)
	    fprintf(stderr,"\n   relative immarg: offset %3d",j);
	} else {
	  pi->start = NULL; /* not relocatable */
	  if (debug)
	    fprintf(stderr,"\n   non_reloc: engine1!=engine3 offset %3d",j);
	  /* assert(j<prim_len);*/
	  break;
	}
	j+=4;
      }
    }
    if (debug)
      fprintf(stderr,"\n");
  }
  decomp_prims = calloc(i,sizeof(PrimInfo *));
  for (i=DOESJUMP+1; i<npriminfos; i++)
    decomp_prims[i] = &(priminfos[i]);
  qsort(decomp_prims+DOESJUMP+1, npriminfos-DOESJUMP-1, sizeof(PrimInfo *),
	compare_priminfo_length);
#endif
}

void flush_to_here(void)
{
#ifndef NO_DYNAMIC
  if (start_flush)
    FLUSH_ICACHE(start_flush, code_here-start_flush);
  start_flush=code_here;
#endif
}

#ifndef NO_DYNAMIC
void append_jump(void)
{
  if (last_jump) {
    PrimInfo *pi = &priminfos[last_jump];
    
    memcpy(code_here, pi->start+pi->length, pi->restlength);
    code_here += pi->restlength;
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

Address append_prim(Cell p)
{
  PrimInfo *pi = &priminfos[p];
  Address old_code_here = code_here;

  if (code_area+code_area_size < code_here+pi->length+pi->restlength) {
    struct code_block_list *p;
    append_jump();
    flush_to_here();
    if (*next_code_blockp == NULL) {
      code_here = start_flush = code_area = my_alloc(code_area_size);
      p = (struct code_block_list *)malloc(sizeof(struct code_block_list));
      *next_code_blockp = p;
      p->next = NULL;
      p->block = code_here;
      p->size = code_area_size;
    } else {
      p = *next_code_blockp;
      code_here = start_flush = code_area = p->block;
    }
    old_code_here = code_here;
    next_code_blockp = &(p->next);
  }
  memcpy(code_here, pi->start, pi->length);
  code_here += pi->length;
  return old_code_here;
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

long dyncodesize(void)
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
  for (i=npriminfos-1; i>DOESJUMP; i--) {
    PrimInfo *pi=decomp_prims[i];
    if (pi->start==code || (pi->start && memcmp(code,pi->start,pi->length)==0))
      return vm_prims[super2[super_costs[pi-priminfos-DOESJUMP-1].offset]+DOESJUMP+1];
    /* return pi->start;*/
  }
  return code;
#endif /* !defined(NO_DYNAMIC) */
}

#ifdef NO_IP
int nbranchinfos=0;

struct branchinfo {
  Label *targetptr; /* *(bi->targetptr) is the target */
  Cell *addressptr; /* store the target here */
} branchinfos[100000];

int ndoesexecinfos=0;
struct doesexecinfo {
  int branchinfo; /* fix the targetptr of branchinfos[...->branchinfo] */
  Cell *xt; /* cfa of word whose does-code needs calling */
} doesexecinfos[10000];

void set_rel_target(Cell *source, Label target)
{
  *source = ((Cell)target)-(((Cell)source)+4);
}

void register_branchinfo(Label source, Cell targetptr)
{
  struct branchinfo *bi = &(branchinfos[nbranchinfos]);
  bi->targetptr = (Label *)targetptr;
  bi->addressptr = (Cell *)source;
  nbranchinfos++;
}

Cell *compile_prim1arg(Cell p)
{
  int l = priminfos[p].length;
  Address old_code_here=code_here;

  assert(vm_prims[p]==priminfos[p].start);
  append_prim(p);
  return (Cell*)(old_code_here+priminfos[p].immargs[0].offset);
}

Cell *compile_call2(Cell targetptr)
{
  Cell *next_code_target;
  PrimInfo *pi = &priminfos[N_call2];
  Address old_code_here = append_prim(N_call2);

  next_code_target = (Cell *)(old_code_here + pi->immargs[0].offset);
  register_branchinfo(old_code_here + pi->immargs[1].offset, targetptr);
  return next_code_target;
}
#endif

void finish_code(void)
{
#ifdef NO_IP
  Cell i;

  compile_prim1(NULL);
  for (i=0; i<ndoesexecinfos; i++) {
    struct doesexecinfo *dei = &doesexecinfos[i];
    branchinfos[dei->branchinfo].targetptr = DOES_CODE1((dei->xt));
  }
  ndoesexecinfos = 0;
  for (i=0; i<nbranchinfos; i++) {
    struct branchinfo *bi=&branchinfos[i];
    set_rel_target(bi->addressptr, *(bi->targetptr));
  }
  nbranchinfos = 0;
#endif
  flush_to_here();
}

#if 0
/* compile *start into a dynamic superinstruction, updating *start */
void compile_prim_dyn(Cell *start)
{
#if defined(NO_IP)
  static Cell *last_start=NULL;
  static Xt last_prim=NULL;
  /* delay work by one call in order to get relocated immargs */

  if (last_start) {
    unsigned i = last_prim-vm_prims;
    PrimInfo *pi=&priminfos[i];
    Cell *next_code_target=NULL;

    assert(i<npriminfos);
    if (i==N_execute||i==N_perform||i==N_lit_perform) {
      next_code_target = compile_prim1arg(N_set_next_code);
    }
    if (i==N_call) {
      next_code_target = compile_call2(last_start[1]);
    } else if (i==N_does_exec) {
      struct doesexecinfo *dei = &doesexecinfos[ndoesexecinfos++];
      *compile_prim1arg(N_lit) = (Cell)PFA(last_start[1]);
      /* we cannot determine the callee now (last_start[1] may be a
         forward reference), so just register an arbitrary target, and
         register in dei that we need to fix this before resolving
         branches */
      dei->branchinfo = nbranchinfos;
      dei->xt = (Cell *)(last_start[1]);
      next_code_target = compile_call2(NULL);
    } else if (pi->start == NULL) { /* non-reloc */
      next_code_target = compile_prim1arg(N_set_next_code);
      set_rel_target(compile_prim1arg(N_abranch),*(Xt)last_prim);
    } else {
      unsigned j;
      Address old_code_here = append_prim(i);

      for (j=0; j<pi->nimmargs; j++) {
	struct immarg *ia = &(pi->immargs[j]);
	Cell argval = last_start[pi->nimmargs - j]; /* !! specific to prims */
	if (ia->rel) { /* !! assumption: relative refs are branches */
	  register_branchinfo(old_code_here + ia->offset, argval);
	} else /* plain argument */
	  *(Cell *)(old_code_here + ia->offset) = argval;
      }
    }
    if (next_code_target!=NULL)
      *next_code_target = (Cell)code_here;
  }
  if (start) {
    last_prim = (Xt)*start;
    *start = (Cell)code_here;
  }
  last_start = start;
  return;
#elif !defined(NO_DYNAMIC)
  Label prim=(Label)*start;
  unsigned i;
  Address old_code_here;

  i = ((Xt)prim)-vm_prims;
  prim = *(Xt)prim;
  if (no_dynamic) {
    *start = (Cell)prim;
    return;
  }
  if (i>=npriminfos || priminfos[i].start == 0) { /* not a relocatable prim */
    append_jump();
    *start = (Cell)prim;
    return;
  }
  assert(priminfos[i].start = prim); 
#ifdef ALIGN_CODE
  /*  ALIGN_CODE;*/
#endif
  assert(prim==priminfos[i].start);
  old_code_here = append_prim(i);
  last_jump = (priminfos[i].superend) ? 0 : i;
  *start = (Cell)old_code_here;
  return;
#else /* !defined(DOUBLY_INDIRECT), no code replication */
  Label prim=(Label)*start;
#if !defined(INDIRECT_THREADED)
  prim = *(Xt)prim;
#endif
  *start = (Cell)prim;
  return;
#endif /* !defined(DOUBLY_INDIRECT) */
}
#endif /* 0 */

Cell compile_prim_dyn(unsigned p)
{
  Cell static_prim = (Cell)vm_prims[p+DOESJUMP+1];
#if defined(NO_DYNAMIC)
  return static_prim;
#else /* !defined(NO_DYNAMIC) */
  Address old_code_here;

  if (no_dynamic)
    return static_prim;
  p += DOESJUMP+1;
  if (p>=npriminfos || priminfos[p].start == 0) { /* not a relocatable prim */
    append_jump();
    return static_prim;
  }
  old_code_here = append_prim(p);
  last_jump = (priminfos[p].superend) ? 0 : p;
  return (Cell)old_code_here;
#endif  /* !defined(NO_DYNAMIC) */
}

#ifndef NO_DYNAMIC
int cost_codesize(int prim)
{
  return priminfos[prim+DOESJUMP+1].length;
}
#endif

int cost_ls(int prim)
{
  struct cost *c = super_costs+prim;

  return c->loads + c->stores;
}

int cost_lsu(int prim)
{
  struct cost *c = super_costs+prim;

  return c->loads + c->stores + c->updates;
}

int cost_nexts(int prim)
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

#define MAX_BB 128 /* maximum number of instructions in BB */

/* use dynamic programming to find the shortest paths within the basic
   block origs[0..ninsts-1]; optimals[i] contains the superinstruction
   on the shortest path to the end of the BB */
void optimize_bb(short origs[], short optimals[], int ninsts)
{
  int i,j, mincost;
  static int costs[MAX_BB+1];

  assert(ninsts<MAX_BB);
  costs[ninsts]=0;
  for (i=ninsts-1; i>=0; i--) {
    optimals[i] = origs[i];
    costs[i] = mincost = costs[i+1] + ss_cost(optimals[i]);
    for (j=2; j<=max_super && i+j<=ninsts ; j++) {
      int super, jcost;

      super = lookup_super(origs+i,j);
      if (super >= 0) {
	jcost = costs[i+j] + ss_cost(super);
	if (jcost <= mincost) {
	  optimals[i] = super;
	  mincost = jcost;
	  if (!ss_greedy)
	    costs[i] = jcost;
	}
      }
    }
  }
}

/* rewrite the instructions pointed to by instps to use the
   superinstructions in optimals */
void rewrite_bb(Cell *instps[], short *optimals, int ninsts)
{
  int i,j, nextdyn;
  Cell inst;

  for (i=0, nextdyn=0; i<ninsts; i++) {
    if (i==nextdyn) { /* compile dynamically */
      nextdyn += super_costs[optimals[i]].length;
      inst = compile_prim_dyn(optimals[i]);
      for (j=0; j<sizeof(cost_sums)/sizeof(cost_sums[0]); j++)
	cost_sums[j].sum += cost_sums[j].costfunc(optimals[i]);
    } else { /* compile statically */
      inst = (Cell)vm_prims[optimals[i]+DOESJUMP+1];
    }
    *(instps[i]) = inst;
  }
}

/* compile *start, possibly rewriting it into a static and/or dynamic
   superinstruction */
void compile_prim1(Cell *start)
{
#if defined(DOUBLY_INDIRECT)
  Label prim=(Label)*start;
  if (prim<((Label)(xts+DOESJUMP)) || prim>((Label)(xts+npriminfos))) {
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
  static short origs[MAX_BB];
  static short optimals[MAX_BB];
  static int ninsts=0;
  unsigned prim_num;

  if (start==NULL)
    goto end_bb;
  prim_num = ((Xt)*start)-vm_prims;
  if (prim_num >= npriminfos)
    goto end_bb;
  assert(ninsts<MAX_BB);
  instps[ninsts] = start;
  origs[ninsts] = prim_num-DOESJUMP-1;
  ninsts++;
  if (ninsts >= MAX_BB || superend[prim_num-DOESJUMP-1]) {
  end_bb:
    optimize_bb(origs,optimals,ninsts);
    rewrite_bb(instps,optimals,ninsts);
    ninsts=0;
  }
#endif /* !(defined(DOUBLY_INDIRECT) || defined(INDIRECT_THREADED)) */
}

#if defined(PRINT_SUPER_LENGTHS) && !defined(NO_DYNAMIC)
Cell prim_length(Cell prim)
{
  return priminfos[prim+DOESJUMP+1].length;
}
#endif

Address loader(FILE *imagefile, char* filename)
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

  vm_prims = engine(0,0,0,0,0);
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
  
  do {
    if(fread(magic,sizeof(Char),8,imagefile) < 8) {
      fprintf(stderr,"%s: image %s doesn't seem to be a Gforth (>=0.6) image.\n",
	      progname, filename);
      exit(1);
    }
    preamblesize+=8;
  } while(memcmp(magic,"Gforth3",7));
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
  if (debug)
    fprintf(stderr,"pagesize=%ld\n",(unsigned long) pagesize);

  image = dict_alloc_read(imagefile, preamblesize+header.image_size,
			  preamblesize+dictsize, data_offset);
  imp=image+preamblesize;
  alloc_stacks((ImageHeader *)imp);
  if (clear_dictionary)
    memset(imp+header.image_size, 0, dictsize-header.image_size);
  if(header.base==0 || header.base  == (Address)0x100) {
    Cell reloc_size=((header.image_size-1)/sizeof(Cell))/8+1;
    char reloc_bits[reloc_size];
    fseek(imagefile, preamblesize+header.image_size, SEEK_SET);
    fread(reloc_bits, 1, reloc_size, imagefile);
    relocate((Cell *)imp, reloc_bits, header.image_size, (Cell)header.base, vm_prims);
#if 0
    { /* let's see what the relocator did */
      FILE *snapshot=fopen("snapshot.fi","wb");
      fwrite(image,1,imagesize,snapshot);
      fclose(snapshot);
    }
#endif
  }
  else if(header.base!=imp) {
    fprintf(stderr,"%s: Cannot load nonrelocatable image (compiled for address $%lx) at address $%lx\n",
	    progname, (unsigned long)header.base, (unsigned long)imp);
    exit(1);
  }
  if (header.checksum==0)
    ((ImageHeader *)imp)->checksum=check_sum;
  else if (header.checksum != check_sum) {
    fprintf(stderr,"%s: Checksum of image ($%lx) does not match the executable ($%lx)\n",
	    progname, (unsigned long)(header.checksum),(unsigned long)check_sum);
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

/* pointer to last '/' or '\' in file, 0 if there is none. */
char *onlypath(char *filename)
{
  return strrchr(filename, DIRSEP);
}

FILE *openimage(char *fullfilename)
{
  FILE *image_file;
  char * expfilename = tilde_cstr(fullfilename, strlen(fullfilename), 1);

  image_file=fopen(expfilename,"rb");
  if (image_file!=NULL && debug)
    fprintf(stderr, "Opened image file: %s\n", expfilename);
  return image_file;
}

/* try to open image file concat(path[0:len],imagename) */
FILE *checkimage(char *path, int len, char *imagename)
{
  int dirlen=len;
  char fullfilename[dirlen+strlen(imagename)+2];

  memcpy(fullfilename, path, dirlen);
  if (fullfilename[dirlen-1]!=DIRSEP)
    fullfilename[dirlen++]=DIRSEP;
  strcpy(fullfilename+dirlen,imagename);
  return openimage(fullfilename);
}

FILE * open_image_file(char * imagename, char * path)
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
#endif

#ifdef HAS_OS
UCell convsize(char *s, UCell elemsize)
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
  ss_min_codesize,
  ss_min_ls,
  ss_min_lsu,
  ss_min_nexts,
};

void gforth_args(int argc, char ** argv, char ** path, char ** imagename)
{
  int c;

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
      {"path", required_argument, NULL, 'p'},
      {"version", no_argument, NULL, 'v'},
      {"help", no_argument, NULL, 'h'},
      /* put something != 0 into offset_image */
      {"offset-image", no_argument, &offset_image, 1},
      {"no-offset-im", no_argument, &offset_image, 0},
      {"clear-dictionary", no_argument, &clear_dictionary, 1},
      {"die-on-signal", no_argument, &die_on_signal, 1},
      {"debug", no_argument, &debug, 1},
      {"no-super", no_argument, &no_super, 1},
      {"no-dynamic", no_argument, &no_dynamic, 1},
      {"dynamic", no_argument, &no_dynamic, 0},
      {"print-metrics", no_argument, &print_metrics, 1},
      {"ss-number", required_argument, NULL, ss_number},
#ifndef NO_DYNAMIC
      {"ss-min-codesize", no_argument, NULL, ss_min_codesize},
#endif
      {"ss-min-ls",       no_argument, NULL, ss_min_ls},
      {"ss-min-lsu",      no_argument, NULL, ss_min_lsu},
      {"ss-min-nexts",    no_argument, NULL, ss_min_nexts},
      {"ss-greedy",       no_argument, &ss_greedy, 1},
      {0,0,0,0}
      /* no-init-file, no-rc? */
    };
    
    c = getopt_long(argc, argv, "+i:m:d:r:f:l:p:vhoncsx", opts, &option_index);
    
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
    case 'v': fputs(PACKAGE_STRING"\n", stderr); exit(0);
    case ss_number: static_super_number = atoi(optarg); break;
#ifndef NO_DYNAMIC
    case ss_min_codesize: ss_cost = cost_codesize; break;
#endif
    case ss_min_ls:       ss_cost = cost_ls;       break;
    case ss_min_lsu:      ss_cost = cost_lsu;      break;
    case ss_min_nexts:    ss_cost = cost_nexts;    break;
    case 'h': 
      fprintf(stderr, "Usage: %s [engine options] ['--'] [image arguments]\n\
Engine Options:\n\
  --appl-image FILE		    equivalent to '--image-file=FILE --'\n\
  --clear-dictionary		    Initialize the dictionary with 0 bytes\n\
  -d SIZE, --data-stack-size=SIZE   Specify data stack size\n\
  --debug			    Print debugging information during startup\n\
  --die-on-signal		    exit instead of CATCHing some signals\n\
  --dynamic			    use dynamic native code\n\
  -f SIZE, --fp-stack-size=SIZE	    Specify floating point stack size\n\
  -h, --help			    Print this message and exit\n\
  -i FILE, --image-file=FILE	    Use image FILE instead of `gforth.fi'\n\
  -l SIZE, --locals-stack-size=SIZE Specify locals stack size\n\
  -m SIZE, --dictionary-size=SIZE   Specify Forth dictionary size\n\
  --no-dynamic			    Use only statically compiled primitives\n\
  --no-offset-im		    Load image at normal position\n\
  --no-super                        No dynamically formed superinstructions\n\
  --offset-image		    Load image at a different position\n\
  -p PATH, --path=PATH		    Search path for finding image and sources\n\
  --print-metrics		    Print some code generation metrics on exit\n\
  -r SIZE, --return-stack-size=SIZE Specify return stack size\n\
  --ss-greedy                       greedy, not optimal superinst selection\n\
  --ss-min-codesize                 select superinsts for smallest native code\n\
  --ss-min-ls                       minimize loads and stores\n\
  --ss-min-lsu                      minimize loads, stores, and pointer updates\n\
  --ss-min-nexts                    minimize the number of static superinsts\n\
  --ss-number=N                     use N static superinsts (default max)\n\
  -v, --version			    Print engine version and exit\n\
SIZE arguments consist of an integer followed by a unit. The unit can be\n\
  `b' (byte), `e' (element; default), `k' (KB), `M' (MB), `G' (GB) or `T' (TB).\n",
	      argv[0]);
      optind--;
      return;
    }
  }
}
#endif

#ifdef INCLUDE_IMAGE
extern Cell image[];
extern const char reloc_bits[];
#endif

int main(int argc, char **argv, char **env)
{
#ifdef HAS_OS
  char *path = getenv("GFORTHPATH") ? : DEFAULTPATH;
#else
  char *path = DEFAULTPATH;
#endif
#ifndef INCLUDE_IMAGE
  char *imagename="gforth.fi";
  FILE *image_file;
  Address image;
#endif
  int retvalue;
	  
#if defined(i386) && defined(ALIGNMENT_CHECK)
  /* turn on alignment checks on the 486.
   * on the 386 this should have no effect. */
  __asm__("pushfl; popl %eax; orl $0x40000, %eax; pushl %eax; popfl;");
  /* this is unusable with Linux' libc.4.6.27, because this library is
     not alignment-clean; we would have to replace some library
     functions (e.g., memcpy) to make it work. Also GCC doesn't try to keep
     the stack FP-aligned. */
#endif

  /* buffering of the user output device */
#ifdef _IONBF
  if (isatty(fileno(stdout))) {
    fflush(stdout);
    setvbuf(stdout,NULL,_IONBF,0);
  }
#endif

  progname = argv[0];

#ifdef HAS_OS
  gforth_args(argc, argv, &path, &imagename);
#ifndef NO_DYNAMIC
  if (no_dynamic && ss_cost == cost_codesize) {
    ss_cost = cost_lsu;
    cost_sums[0] = cost_sums[1];
    if (debug)
      fprintf(stderr, "--no-dynamic conflicts with --ss-min-codesize, reverting to --ss-min-lsu\n");
  }
#endif /* !defined(NO_DYNAMIC) */
#endif /* defined(HAS_OS) */

#ifdef INCLUDE_IMAGE
  set_stack_sizes((ImageHeader *)image);
  if(((ImageHeader *)image)->base != image)
    relocate(image, reloc_bits, ((ImageHeader *)image)->image_size,
	     (Label*)engine(0, 0, 0, 0, 0));
  alloc_stacks((ImageHeader *)image);
#else
  image_file = open_image_file(imagename, path);
  image = loader(image_file, imagename);
#endif
  gforth_header=(ImageHeader *)image; /* used in SIGSEGV handler */

  {
    char path2[strlen(path)+1];
    char *p1, *p2;
    Cell environ[]= {
      (Cell)argc-(optind-1),
      (Cell)(argv+(optind-1)),
      (Cell)strlen(path),
      (Cell)path2};
    argv[optind-1] = progname;
    /*
       for (i=0; i<environ[0]; i++)
       printf("%s\n", ((char **)(environ[1]))[i]);
       */
    /* make path OS-independent by replacing path separators with NUL */
    for (p1=path, p2=path2; *p1!='\0'; p1++, p2++)
      if (*p1==PATHSEP)
	*p2 = '\0';
      else
	*p2 = *p1;
    *p2='\0';
    retvalue = go_forth(image, 4, environ);
#ifdef SIGPIPE
    bsd_signal(SIGPIPE, SIG_IGN);
#endif
#ifdef VM_PROFILING
    vm_print_profile(stderr);
#endif
    deprep_terminal();
  }
  if (print_metrics) {
    int i;
    fprintf(stderr, "code size = %8ld\n", dyncodesize());
    for (i=0; i<sizeof(cost_sums)/sizeof(cost_sums[0]); i++)
      fprintf(stderr, "metric %8s: %8ld\n",
	      cost_sums[i].metricname, cost_sums[i].sum);
  }
  return retvalue;
}
