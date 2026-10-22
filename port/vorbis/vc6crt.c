// SPDX-License-Identifier: GPL-3.0-only
// The C runtime entry points that the VC6-compiled libogg/libvorbis 1.0 objects (build.py) call, under the vc6_ names
// build.py gives them, for both the i686 Linux and the Windows build. VC6 code keeps the stack 4-byte aligned, so
// every wrapper realigns before it enters the program's runtime. Of these only floor, ldexp, the allocator, memmove,
// memchr, qsort and the helpers in assembly are on the decoding path; pow and acos (floor 0 and the encoder) use the
// host's libm, which need not round as MSVCRT's x87 code does.
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W __attribute__((force_align_arg_pointer))
#ifdef _WIN32
#define SYM(x) "_" #x
#else
#define SYM(x) #x
#endif

int vc6_fltused = 0x9875;

static W void *w_malloc(size_t n) { return malloc(n); }
static W void *w_calloc(size_t a, size_t b) { return calloc(a, b); }
static W void *w_realloc(void *p, size_t n) { return realloc(p, n); }
static W void w_free(void *p) { free(p); }
static W void *w_memmove(void *d, const void *s, size_t n) { return memmove(d, s, n); }
static W void w_qsort(void *b, size_t n, size_t s, int (*c)(const void *, const void *)) { qsort(b, n, s, c); }
static W double w_floor(double x) { return floor(x); }
static W double w_ceil(double x) { return ceil(x); }
static W double w_ldexp(double x, int e) { return ldexp(x, e); }
static W double w_frexp(double x, int *e) { return frexp(x, e); }
static W int w_toupper(int c) { return toupper(c); }
static W void w_perror(const char *s) { perror(s); }
static W void *w_fopen(const char *n, const char *m) { return fopen(n, m); }
static W int w_fclose(void *f) { return fclose((FILE *)f); }
static W int w_sprintf(char *b, const char *f, ...)
{
	va_list a;
	va_start(a, f);
	int r = vsprintf(b, f, a);
	va_end(a);
	return r;
}
// fprintf's stream is &_iob[2] (stderr) on the library's only use, a diagnostic
static W int w_fprintf(void *s, const char *f, ...)
{
	(void)s;
	va_list a;
	va_start(a, f);
	int r = vfprintf(stderr, f, a);
	va_end(a);
	return r;
}
static char fake_iob[3 * 32];

// the import-table slots: VC6 /MD code calls through them (call [__imp__malloc])
void *vc6_imp_malloc = (void *)w_malloc, *vc6_imp_calloc = (void *)w_calloc, *vc6_imp_realloc = (void *)w_realloc,
	 *vc6_imp_free = (void *)w_free, *vc6_imp_memmove = (void *)w_memmove, *vc6_imp_qsort = (void *)w_qsort,
	 *vc6_imp_floor = (void *)w_floor, *vc6_imp_ceil = (void *)w_ceil, *vc6_imp_ldexp = (void *)w_ldexp,
	 *vc6_imp_frexp = (void *)w_frexp, *vc6_imp_toupper = (void *)w_toupper, *vc6_imp_sprintf = (void *)w_sprintf,
	 *vc6_imp_fprintf = (void *)w_fprintf, *vc6_imp_perror = (void *)w_perror, *vc6_imp_fopen = (void *)w_fopen,
	 *vc6_imp_fclose = (void *)w_fclose, *vc6_imp_iob = (void *)fake_iob;

// direct calls from libogg (static runtime, /MT)
W void *vc6_malloc(size_t n) { return malloc(n); }
W void *vc6_realloc(void *p, size_t n) { return realloc(p, n); }
W void vc6_free(void *p) { free(p); }
W void *vc6_memmove(void *d, const void *s, size_t n) { return memmove(d, s, n); }
W void *vc6_memchr(const void *s, int c, size_t n) { return memchr(s, c, n); }

__attribute__((used)) W double vc6_c_pow(double x, double y) { return pow(x, y); }
__attribute__((used)) W double vc6_c_acos(double x) { return acos(x); }

__asm__(".text\n"
		// _CIpow/_CIacos take their operands on the x87 stack (pow: x in st1, y in st0) and return in st0
		".globl " SYM(vc6_CIpow) "\n" SYM(vc6_CIpow) ":\n\tsub $16,%esp\n\tfstpl 8(%esp)\n\tfstpl (%esp)\n"
		"\tcall " SYM(vc6_c_pow) "\n\tadd $16,%esp\n\tret\n"
		".globl " SYM(vc6_CIacos) "\n" SYM(vc6_CIacos) ":\n\tsub $8,%esp\n\tfstpl (%esp)\n\tcall " SYM(vc6_c_acos) "\n"
		"\tadd $8,%esp\n\tret\n"
		// _ftol: st0 truncated to a 64-bit integer in edx:eax, popped
		".globl " SYM(vc6_ftol) "\n" SYM(vc6_ftol) ":\n\tpush %ebp\n\tmov %esp,%ebp\n\tsub $12,%esp\n\tfnstcw -2(%ebp)\n"
		"\tmovw -2(%ebp),%ax\n\torb $0x0c,%ah\n\tmovw %ax,-4(%ebp)\n\tfldcw -4(%ebp)\n\tfistpll -12(%ebp)\n"
		"\tfldcw -2(%ebp)\n\tmov -12(%ebp),%eax\n\tmov -8(%ebp),%edx\n\tleave\n\tret\n"
		// _alloca_probe/_chkstk: allocate eax bytes of stack, touching each page in order
		".globl " SYM(vc6_alloca_probe) "\n.globl " SYM(vc6_chkstk) "\n" SYM(vc6_alloca_probe) ":\n" SYM(vc6_chkstk) ":\n"
		"\tpush %ecx\n\tlea 8(%esp),%ecx\n1:\tcmp $0x1000,%eax\n\tjb 2f\n\tsub $0x1000,%ecx\n\tsub $0x1000,%eax\n"
		"\ttest %eax,(%ecx)\n\tjmp 1b\n2:\tsub %eax,%ecx\n\tmov %esp,%eax\n\ttest %eax,(%ecx)\n\tmov %ecx,%esp\n"
		"\tmov (%eax),%ecx\n\tmov 4(%eax),%eax\n\tpush %eax\n\tret\n"
		// _allshl/_allshr: edx:eax shifted by cl
		".globl " SYM(vc6_allshl) "\n" SYM(vc6_allshl) ":\n\tcmp $64,%cl\n\tjae 3f\n\tcmp $32,%cl\n\tjae 4f\n"
		"\tshld %cl,%eax,%edx\n\tshl %cl,%eax\n\tret\n4:\tmov %eax,%edx\n\txor %eax,%eax\n\tand $31,%cl\n"
		"\tshl %cl,%edx\n\tret\n3:\txor %eax,%eax\n\txor %edx,%edx\n\tret\n"
		".globl " SYM(vc6_allshr) "\n" SYM(vc6_allshr) ":\n\tcmp $64,%cl\n\tjae 5f\n\tcmp $32,%cl\n\tjae 6f\n"
		"\tshrd %cl,%edx,%eax\n\tsar %cl,%edx\n\tret\n6:\tmov %edx,%eax\n\tsar $31,%edx\n\tand $31,%cl\n"
		"\tsar %cl,%eax\n\tret\n5:\tsar $31,%edx\n\tmov %edx,%eax\n\tret\n");
