#ifndef _ALPHA_BYTEORDER_H
#define _ALPHA_BYTEORDER_H

#undef ntohl
#undef ntohs
#undef htonl
#undef htons

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN
#endif

#ifndef __LITTLE_ENDIAN_BITFIELD
#define __LITTLE_ENDIAN_BITFIELD
#endif

extern unsigned long int	ntohl(unsigned long int);
extern unsigned short int	ntohs(unsigned short int);
extern unsigned long int	htonl(unsigned long int);
extern unsigned short int	htons(unsigned short int);

extern unsigned long int	__ntohl(unsigned long int);
extern unsigned short int	__ntohs(unsigned short int);
extern unsigned long int	__constant_ntohl(unsigned long int);
extern unsigned short int	__constant_ntohs(unsigned short int);

/*
 * The constant and non-constant versions here are the same.
 * Maybe I'll come up with an alpha-optimized routine for the
 * non-constant ones (the constant ones don't need it: gcc
 * will optimize it to the correct constant)
 */

extern __inline__ unsigned long int
__ntohl(unsigned long int x)
{
	unsigned long int res, t1, t2;

	__asm__
	    ("bis	%3,%3,%0	# %0 is result; %0=aabbccdd
	      extlh	%0,5,%1		# %1 = dd000000
	      zap	%0,0xfd,%2	# %2 = 0000cc00
	      sll	%2,5,%2		# %2 = 00198000
	      s8addl	%2,%1,%1	# %1 = ddcc0000
	      zap	%0,0xfb,%2	# %2 = 00bb0000
	      srl	%2,8,%2		# %2 = 0000bb00
	      extbl	%0,3,%0		# %0 = 000000aa
	      or	%1,%0,%0	# %0 = ddcc00aa
	      or	%2,%0,%0	# %0 = ddccbbaa"
	     : "r="(res), "r="(t1), "r="(t2) : "r"(x));
	return res;
}

#define __constant_ntohl(x) \
((unsigned int)((((unsigned int)(x) & 0x000000ffU) << 24) | \
		(((unsigned int)(x) & 0x0000ff00U) <<  8) | \
		(((unsigned int)(x) & 0x00ff0000U) >>  8) | \
		(((unsigned int)(x) & 0xff000000U) >> 24)))

extern __inline__ unsigned short int
__ntohs(unsigned short int x)
{
	unsigned long int res, t1;
	
	__asm__
	    ("bis	%2,%2,%0	# v0 is result; swap in-place.  v0=aabb
	      extwh	%0,7,%1		# t1 = bb00
	      extbl	%0,1,%0		# v0 = 00aa
	      bis	%0,%1,%0	# v0 = bbaa"
	     : "r="(res), "r="(t1) : "r"(x));
	return res;
}

#define __constant_ntohs(x) \
((unsigned short int)((((unsigned short int)(x) & 0x00ff) << 8) | \
		      (((unsigned short int)(x) & 0xff00) >> 8)))

#define __htonl(x) __ntohl(x)
#define __htons(x) __ntohs(x)
#define __constant_htonl(x) __constant_ntohl(x)
#define __constant_htons(x) __constant_ntohs(x)

#ifdef  __OPTIMIZE__
#  define ntohl(x) \
(__builtin_constant_p((long)(x)) ? \
 __constant_ntohl((x)) : \
 __ntohl((x)))
#  define ntohs(x) \
(__builtin_constant_p((short)(x)) ? \
 __constant_ntohs((x)) : \
 __ntohs((x)))
#  define htonl(x) \
(__builtin_constant_p((long)(x)) ? \
 __constant_htonl((x)) : \
 __htonl((x)))
#  define htons(x) \
(__builtin_constant_p((short)(x)) ? \
 __constant_htons((x)) : \
 __htons((x)))
#endif

#endif
