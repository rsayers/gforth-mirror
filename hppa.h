/*
  $Id$
  Copyright 1992 by the ANSI figForth Development Group

  This is the machine-specific part for a HPPA running HP-UX
*/
/* cache flush stuff */

#ifdef DIRECT_THREADED

extern void * cacheflush(void *, int, int);
#ifdef DEBUG
#  define CACHE_FLUSH(addr,size) \
	({	fprintf(stderr,"Flushing Cache at %08x:%08x\n",(int) addr, size); \
		fflush(stderr); \
		fprintf(stderr,"Cache flushed, final address: %08x\n", \
		        (int)cacheflush((void *)(addr), (int)(size), 32)); })
#  else
/*
#  define CACHE_FLUSH(addr,size) \
	({	fprintf(stderr,"Flushing Cache at %08x:%08x\n",(int) addr, size); \
		fflush(stderr); \
		fprintf(stderr,"Cache flushed, final address: %08x\n", \
		        (int)cacheflush((void *)(addr), (int)(size), 32)); })
*/
#  define CACHE_FLUSH(addr,size) \
		({ (void)cacheflush((void *)(addr), (int)(size), 32); })

#  endif
#endif


/* Cell and UCell must be the same size as a pointer */
typedef long Cell;
typedef unsigned long UCell;

/* DCell and UDCell must be twice as large as Cell */
typedef long long DCell;
typedef unsigned long long UDCell;

/* define this if IEEE singles and doubles are available as C data types */
#define IEEE_FP

/* the IEEE types are used only for loading and storing */
/* the IEEE double precision type */
typedef double DFloat;
/* the IEEE single precision type */
typedef float SFloat;

/* define this if the least-significant byte is at the largets address */
#define BIG_ENDIAN

#ifdef DIRECT_THREADED
	/* PFA gives the parameter field address corresponding to a cfa */
#	define PFA(cfa)	(((Cell *)cfa)+2)
	/* PFA1 is a special version for use just after a NEXT1 */
	/* the improvement here is that we may destroy cfa before using PFA1 */
#	define PFA1(cfa)       PFA(cfa)
	/* HPPA uses register 2 for branch and link */

	/* CODE_ADDRESS is the address of the code jumped to through the code field */

	/* MAKE_CF creates an appropriate code field at the cfa; ca is the code address */
	/* we use ble and a register, since 'bl' only has 21 bits displacement */
#endif

#ifdef undefined
#define MAKE_CFA(cfa,ca)	({long *_cfa        = (long *)(cfa); \
			  unsigned _ca = (unsigned)(ca); \
				  _cfa[0] = 0xE4A02000 | (((int)_ca+4-(int)symbols[0]) & 0x7FC)<<1 ; \
				  _cfa[1] = *(long *)(_ca); \
				  /* printf("%08x:%08x,%08x\n",_cfa,_cfa[0],_cfa[1]); */ \
			  })
#endif

#ifdef DIRECT_THREADED

#ifdef DEBUG
#	define DOUT(a,b,c,d)  fprintf(stderr,a,b,c,d)
#else
#	define DOUT(a,b,c,d)
#endif

#  define ASS17(n)(((((n) >> 13) & 0x1F) << 16)| /* first 5 bits */ \
				       ((((n) >>  2) & 0x3FF) << 3)| /* second 11 bits */ \
				       ((((n) >> 12) & 0x1) << 2)  | /* lo sign (arg!) */ \
				       (((n) < 0) << 0)) /* sign bit */

#  define DIS17(n)(((((n) >> 16) & 0x1F) << 13)| /* first 5 bits */ \
				       ((((n) >>  3) & 0x3FF) << 2)| /* second 11 bits */ \
				       ((((n) >>  2) & 0x1) << 12) | /* lo sign (arg!) */ \
				       (-((n) & 1) << 18)) /* sign bit */

#	define CODE_ADDRESS(cfa) ((Label)({ \
		unsigned int *_cfa=(unsigned int *)(cfa); unsigned _ca; \
		if((_cfa[0] & 0xFFE0E002) == 0xE8000000) /* relative branch */ \
			{ \
				_ca = _cfa[0]; \
				_ca = DIS17(_ca); \
				_ca += (int) (_cfa + 2); \
			} \
		else if((_cfa[0] & 0xFFE0E002) == 0xE0000000) /* absolute branch */ \
			{ \
				_ca = _cfa[0]; \
				_ca = DIS17(_ca); \
			} \
		else \
			{ \
				_ca = _cfa[0]; \
				_ca = (_ca<<31) | \
				      ((_ca>>1 ) & 0x00001800) | \
				      ((_ca>>3 ) & 0x0003E000) | \
				      ((_ca<<4 ) & 0x000C0000) | \
				      ((_ca<<19) & 0x7FF00000) |  \
				      ((_cfa[1]>>1) & 0xFFC); \
			} \
		/* printf("code-address at %08x: %08x\n",_ca,_cfa); */ \
		_ca; \
	}))

#	define MAKE_CF(cfa,ca) \
	({ \
		long *_cfa   = (long *)(cfa); \
		int _ca      = (int)(ca); \
		int _dp      = _ca-(int)(_cfa+2); \
		\
		if(_ca < 0x40000) /* Branch absolute */ \
			{ \
				_cfa[0] =((0x38 << 26) | /* major opcode */ \
				          (   0 << 21) | /* register */ \
				          (   0 << 13) | /* space register */ \
				          (   0 <<  1))| /* if 1, don't execute delay slot */ \
                      ASS17(_ca); \
				_cfa[1] = 0x08000240 /* or %r0,%r0,%r0 */; \
			} \
		else if(_dp < 0x40000 || _dp >= -0x40000) \
			{ \
				_cfa[0] =((0x3A << 26) | /* major opcode */ \
				          (   0 << 21) | /* register */ \
				          (   0 << 13) | /* space register */ \
				          (   0 <<  1))| /* if 1, don't execute delay slot */ \
				          ASS17(_dp); \
				_cfa[1] = 0x08000240 /* or %r0,%r0,%r0 */; \
			} \
		else \
			{ \
				_cfa[0] = (0x08 << 26) | \
				          ((int)_ca<0) | \
				          (_ca & 0x00001800)<<1 | \
				          (_ca & 0x0003E000)<<3 | \
				          (_ca & 0x000C0000)>>4 | \
				          (_ca & 0x7FF00000)>>19; \
            _ca &= 0x3FF; \
				_cfa[1] =((0x38 << 26) | /* major opcode */ \
				          (   1 << 21) | /* register */ \
				          (   0 << 13) | /* space register */ \
				          (   1 <<  1))| /* if 1, don't execute delay slot */ \
				          ASS17(_ca); \
			} \
			DOUT("%08x: %08x,%08x\n",(int)_cfa,_cfa[0],_cfa[1]); \
	})
	/* HP wins the price for the most obfuscated binary opcode */

	/* this is the point where the does code starts if label points to the
	 * jump dodoes */

#	define DOES_CODE(cfa)	((Xt *)(((char *)CODE_ADDRESS(cfa))+8))

	/* this is a special version of DOES_CODE for use in dodoes */
#	define DOES_CODE1(cfa)  DOES_CODE(cfa) \
/*	({register Xt * _ret asm("%r31"); _ret;}) */

	/* HPPA uses register 2 for branch and link */

#	define DOES_HANDLER_SIZE 8
#	define MAKE_DOES_HANDLER(cfa) \
	({ \
		long *_cfa   = (long *)(cfa); \
		int _ca      = (int)symbols[DODOES]; \
		int _dp      = _ca-(int)(_cfa+2); \
		\
		if(_ca < 0x40000) /* Branch absolute */ \
			{ \
				_cfa[0] =((0x38 << 26) | /* major opcode */ \
				          (   0 << 21) | /* register */ \
				          (   0 << 13) | /* space register */ \
				          (   0 <<  1))| /* if 1, don't execute delay slot */ \
				          ASS17(_ca); \
				_cfa[1] = 0x08000240 /* or %r0,%r0,%r0 */; \
			} \
		else if(_dp < 0x40000 || _dp >= -0x40000) \
			{ \
				_cfa[0] =((0x3A << 26) | /* major opcode */ \
				          (   0 << 21) | /* register */ \
				          (   0 << 13) | /* space register */ \
				          (   0 <<  1))| /* if 1, don't execute delay slot */ \
				          ASS17(_dp); \
				_cfa[1] = 0x08000240 /* or %r0,%r0,%r0 */; \
			} \
		else \
			{ \
				fprintf(stderr,"DOESHANDLER assignment failed, use ITC instead of DTC\n"); exit(1); \
				_cfa[0] = (0x08 << 26) | \
				          ((int)_ca<0) | \
				          (_ca & 0x00001800)<<1 | \
				          (_ca & 0x0003E000)<<3 | \
				          (_ca & 0x000C0000)>>4 | \
				          (_ca & 0x7FF00000)>>19; \
            _ca &= 0x3FF; \
				_cfa[1] =((0x38 << 26) | /* major opcode */ \
				          (   1 << 21) | /* register */ \
				          (   0 << 13) | /* space register */ \
				          (   1 <<  1))| /* if 1, don't execute delay slot */ \
				          ASS17(_ca); \
			} \
			DOUT("%08x: %08x,%08x\n",(int)_cfa,_cfa[0],_cfa[1]); \
	})

#	define MAKE_DOES_CF(cfa,ca) \
	({ \
		long *_cfa   = (long *)(cfa); \
		int _ca      = (int)(ca)-DOES_HANDLER_SIZE; \
		int _dp      = _ca-(int)(_cfa+2); \
		\
		if(_dp < 0x40000 || _dp >= -0x40000) \
			{ \
				_cfa[0] = (0x3A << 26) | /* major opcode */ \
				          (   0 << 21) | /* register */ \
				          (   0 << 13) | /* space register */ \
				          (   0 <<  1) | /* if 1, don't execute delay slot */ \
				          ASS17(_dp); \
				_cfa[1] = 0x08000240 /* or %r0,%r0,%r0 */; \
			} \
		else if(_ca < 0x40000) /* Branch absolute */ \
			{ \
				_cfa[0] = (0x38 << 26) | /* major opcode */ \
				          (   0 << 21) | /* register */ \
				          (   0 << 13) | /* space register */ \
				          (   0 <<  1) | /* if 1, don't execute delay slot */ \
				          ASS17(_ca); \
				_cfa[1] = 0x08000240 /* or %r0,%r0,%r0 */; \
			} \
		else \
			{ \
				_cfa[0] = (0x08 << 26) | \
				          ((int)_ca<0) | \
				          (_ca & 0x00001800)<<1 | \
				          (_ca & 0x0003E000)<<3 | \
				          (_ca & 0x000C0000)>>4 | \
				          (_ca & 0x7FF00000)>>19; \
            _ca &= 0x3FF; \
				_cfa[1] = (0x38 << 26) | /* major opcode */ \
				          (   1 << 21) | /* register */ \
				          (   0 << 13) | /* space register */ \
				          (   1 <<  1) | /* if 1, don't execute delay slot */ \
				          ASS17(_ca); \
			} \
			DOUT("%08x: %08x,%08x\n",(int)_cfa,_cfa[0],_cfa[1]); \
	})
	/* this stores a call dodoes at addr */
#endif

/* OS dependences */

#define SEEK_SET 0
#define rint(x)	floor((x)+0.5)


