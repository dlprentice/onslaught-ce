// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/x87math.cpp: the maths functions as retail computes them. VC6 at /O2 (retail's flags,
// config/flags.txt) makes sin, cos, tan, atan, atan2, sqrt, exp, log and log10 intrinsics: bare x87 instructions on
// the caller's stack, which run at the game's x87 precision (24-bit after CreateDevice) and return the unrounded st(0).
// The float forms (sinf, ...) are the same code on a float argument. The host's libm computes differently (glibc's
// i386 routines in double arithmetic under the game's control word; UCRT on SSE2), which moved the battle engine by
// a few float ulps at frame 3544 of a level-100 replay tape. port/build.py renames these calls in game objects to the
// functions below (GAME_MATH) and builds game files with -fno-builtin for them, so clang keeps the calls.
// The sequences are VC6's own, read from a VC6 SP5 /O2 /QIfist compile of each call (llvm-objdump).
#if defined(_WIN32)
#define SYM(name) "_" #name
#else
#define SYM(name) #name
#endif

#define FN1D(name, body) ".globl " SYM(name) "\n" SYM(name) ":\n\tfldl 4(%esp)\n" body "\tret\n"
#define FN1F(name, body) ".globl " SYM(name) "\n" SYM(name) ":\n\tflds 4(%esp)\n" body "\tret\n"
// atan2(y, x): VC6 loads y, then x, then fpatan (st1 = y, st0 = x).
#define FN2D(name, body) ".globl " SYM(name) "\n" SYM(name) ":\n\tfldl 4(%esp)\n\tfldl 12(%esp)\n" body "\tret\n"
#define FN2F(name, body) ".globl " SYM(name) "\n" SYM(name) ":\n\tflds 4(%esp)\n\tflds 8(%esp)\n" body "\tret\n"

// exp: x*log2(e) split into integer and fraction, 2^fraction by f2xm1, scaled by the integer part.
#define EXP_BODY "\tfldl2e\n\tfmulp %st, %st(1)\n\tfld %st(0)\n\tfrndint\n\tfxch %st(1)\n\tfsub %st(1), %st\n" \
	"\tf2xm1\n\tfld1\n\tfaddp %st, %st(1)\n\tfscale\n\tfstp %st(1)\n"

__asm__(".text\n"
		FN1D(bea_x87_sin, "\tfsin\n")
		FN1F(bea_x87_sinf, "\tfsin\n")
		FN1F(bea_x87_fsinl, "\tfsin\n")	/* the same, returned as an 80-bit long double (MakeRotationF, patch 0007) */
		FN1F(bea_x87_fcosl, "\tfcos\n")	/* likewise (patch 0007) */
		/* fsin and fcos of an 80-bit angle that retail keeps on the x87 stack (CPanCamera::Update, patch 0036) */
		".globl " SYM(bea_x87_fsin_x) "\n" SYM(bea_x87_fsin_x) ":\n\tfldt 4(%esp)\n\tfsin\n\tret\n"
		".globl " SYM(bea_x87_fcos_x) "\n" SYM(bea_x87_fcos_x) ":\n\tfldt 4(%esp)\n\tfcos\n\tret\n"
		FN2F(bea_x87_atan2f_l, "\tfpatan\n")	/* atan2f, returned as an 80-bit long double (patch 0036) */
		FN1D(bea_x87_cos, "\tfcos\n")
		FN1F(bea_x87_cosf, "\tfcos\n")
		FN1D(bea_x87_tan, "\tfptan\n\tfstp %st(0)\n")
		FN1F(bea_x87_tanf, "\tfptan\n\tfstp %st(0)\n")
		FN1D(bea_x87_atan, "\tfld1\n\tfpatan\n")
		FN1F(bea_x87_atanf, "\tfld1\n\tfpatan\n")
		FN2D(bea_x87_atan2, "\tfpatan\n")
		FN2F(bea_x87_atan2f, "\tfpatan\n")
		FN1D(bea_x87_sqrt, "\tfsqrt\n")
		FN1F(bea_x87_sqrtf, "\tfsqrt\n")
		FN1D(bea_x87_exp, EXP_BODY)
		FN1F(bea_x87_expf, EXP_BODY)
		// log and log10: the constant first, then x, then fyl2x (st1 * log2(st0)).
		".globl " SYM(bea_x87_log) "\n" SYM(bea_x87_log) ":\n\tfldln2\n\tfldl 4(%esp)\n\tfyl2x\n\tret\n"
		".globl " SYM(bea_x87_logf) "\n" SYM(bea_x87_logf) ":\n\tfldln2\n\tflds 4(%esp)\n\tfyl2x\n\tret\n"
		".globl " SYM(bea_x87_log10) "\n" SYM(bea_x87_log10) ":\n\tfldlg2\n\tfldl 4(%esp)\n\tfyl2x\n\tret\n"
		".globl " SYM(bea_x87_log10f) "\n" SYM(bea_x87_log10f) ":\n\tfldlg2\n\tflds 4(%esp)\n\tfyl2x\n\tret\n");

// asin and acos: VC6 calls its runtime here (__CIasin 0055dcb0, __CIacos 0055f380 in retail). Each keeps the caller's
// precision (it only forces round-to-nearest with exceptions masked, a no-op for the game) and computes
// asin = atan2(x, sqrt((1 + x) * (1 - x))) and acos = atan2(sqrt((1 + x) * (1 - x)), x) with these instructions.
// |x| = 1 gives the extended-precision pi/2 (asin, sign of x) or 0 / pi (acos); |x| > 1 and NaN or infinity give the
// x87 indefinite NaN, as retail's constant at 00653730 (retail also reports a domain error, which the game never reads).
// Retail's classification looks at the high word: exponent >= 0x3ff means |x| >= 1.
// The exit (00561663 / 0056163b with ___fastflag, 009d08b4, which nothing writes, 0): at control word 0x27F the result
// returns as computed; otherwise, when precision exceptions are masked and the (sticky) inexact flag is set, the
// runtime's exception path (00561547) stores it as a double and reloads it, so the game's main thread (0x07F) gets
// the result rounded to double.
#define ASINCOS(name, core, one_pos, one_neg) \
	".globl " SYM(name) "\n" SYM(name) ":\n" \
	"\tfldl 4(%esp)\n\tmovl 8(%esp), %eax\n\tandl $0x7ff00000, %eax\n\tcmpl $0x3ff00000, %eax\n\tjae 1f\n" \
	"\tfld1\n\tfadd %st(1), %st\n\tfld1\n\tfsub %st(2), %st\n\tfmulp %st, %st(1)\n\tfsqrt\n" core "\tjmp " SYM(bea_x87_ci_exit) "\n" \
	"1:\tja 3f\n\tmovl 8(%esp), %eax\n\tandl $0x000fffff, %eax\n\torl 4(%esp), %eax\n\tjne 3f\n\tfstp %st(0)\n" \
	"\ttestl $0x80000000, 8(%esp)\n\tjne 2f\n" one_pos "\tjmp " SYM(bea_x87_ci_exit) "\n2:\t" one_neg "\tjmp " SYM(bea_x87_ci_exit) "\n" \
	"3:\tfstp %st(0)\n\tfldt " SYM(bea_x87_indefinite) "\n\tjmp " SYM(bea_x87_ci_exit) "\n"
#define ASINCOSF(name, dbl) \
	".globl " SYM(name) "\n" SYM(name) ":\n\tflds 4(%esp)\n\tsubl $8, %esp\n\tfstpl (%esp)\n\tcall " SYM(dbl) "\n" \
	"\taddl $8, %esp\n\tret\n"

__asm__(".section .rodata\n\t.p2align 4\n"
		SYM(bea_x87_half_pi) ":\n\t.byte 0x35,0xc2,0x68,0x21,0xa2,0xda,0x0f,0xc9,0xff,0x3f\n"
		SYM(bea_x87_indefinite) ":\n\t.byte 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xff,0xff\n"
		".text\n"
		SYM(bea_x87_ci_exit) ":\n\tsubl $4, %esp\n\tfnstcw (%esp)\n\tmovw (%esp), %ax\n\tcmpw $0x27f, %ax\n\tje 1f\n"
		"\ttestw $0x20, %ax\n\tje 1f\n\tfnstsw %ax\n\ttestw $0x20, %ax\n\tje 1f\n"
		"\tsubl $8, %esp\n\tfstpl (%esp)\n\tfldl (%esp)\n\taddl $8, %esp\n1:\taddl $4, %esp\n\tret\n"
		ASINCOS(bea_x87_asin, "\tfpatan\n", "\tfldt " SYM(bea_x87_half_pi) "\n",
				"\tfldt " SYM(bea_x87_half_pi) "\n\tfchs\n")
		ASINCOS(bea_x87_acos, "\tfxch %st(1)\n\tfpatan\n", "\tfldz\n", "\tfldpi\n")
		".globl " SYM(bea_x87_asin_l) "\n" SYM(bea_x87_asin_l) ":\n\tjmp " SYM(bea_x87_asin) "\n"	/* asin, typed long double (patch 0036) */
		ASINCOSF(bea_x87_asinf, bea_x87_asin)
		ASINCOSF(bea_x87_acosf, bea_x87_acos));
