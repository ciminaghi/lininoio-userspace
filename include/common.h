
#ifndef __COMMON_H__
#define __COMMON_H__

/* Ever-needed definitions */
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

#endif /* __COMMON_H__ */
