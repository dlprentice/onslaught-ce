// SPDX-License-Identifier: GPL-3.0-only
// The goodies mesh viewer's turn (GoodiesMeshTurn in src/FEPGoodies.cpp, patch 0130), run as compiled.
//
// The test (port/tools/goodies_mesh_turn_tests.py) writes three files beside this one: maths.h (the build's text of it),
// mesh_turn.inc (GoodiesMeshTurn exactly as the patched FEPGoodies.cpp holds it) and mesh_turn_case.h (the angles and
// matrices of every case, the pinned order of every cell, and what each case's numbers must tell apart). This driver compiles
// them with the game's own flags, sets the x87 control word the game runs with (007f: single-precision mantissa, round to
// nearest), calls the function, and compares each cell's bits with an explicit x87 evaluation written below: one instruction
// per operation, every value carried between operations as an 80-bit number, so nothing is rounded except where an instruction
// rounds it. Under that control word a multiply, add or subtract rounds its result to 24 bits; fsin and fcos do not.
//
// The pinned evaluation: the three cosines, sin(yaw) and sin(roll) are stored as floats before use; sin(pitch) is used as the
// instruction left it, in sp * sr, in sp * cr and as the middle value of the rotation's row 2. Each cell of the product adds
// the two terms ORDER names first, then the third.
//
// Output: CONTROL word; per case a TRIG line (the six values fcos and fsin returned, 80-bit, most significant byte first:
// cos yaw, sin yaw, cos roll, sin roll, cos pitch, sin pitch) and nine CELL lines:
//   CELL case row column expected actual other-order-1 other-order-2 float-sine verdict
// (float-sine: the pinned order with sin(pitch) rounded to a float first; row 2 only, "--------" elsewhere).
// Exit: 0 every cell as pinned and everything a case requires told apart, 1 a cell differs from the pinned evaluation,
// 3 a case's numbers do not tell apart what it requires (the data is too weak to pin anything), 4 the driver's own
// subtraction is the wrong way round.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// the two types maths.h takes from the game's common.h and windows.h (the same underlying types)
typedef int BOOL;
typedef signed int SINT;
#include <math.h>
#include "maths.h"
#include "mesh_turn.inc"
#include "mesh_turn_case.h"

static uint32_t bits(float f)
{
	uint32_t u;
	memcpy(&u, &f, sizeof u);
	return u;
}

// One x87 instruction each. Operands and results travel as 80-bit values (fldt, fstpt), which hold any x87 register exactly.
static long double x_cos(float a)
{
	long double r;
	__asm__ __volatile__("flds %1\n\tfcos\n\tfstpt %0" : "=m"(r) : "m"(a) : "st", "cc", "memory");
	return r;
}

static long double x_sin(float a)
{
	long double r;
	__asm__ __volatile__("flds %1\n\tfsin\n\tfstpt %0" : "=m"(r) : "m"(a) : "st", "cc", "memory");
	return r;
}

static long double x_mul(long double a, long double b)
{
	long double r;
	__asm__ __volatile__("fldt %1\n\tfldt %2\n\tfmulp\n\tfstpt %0" : "=m"(r) : "m"(a), "m"(b) : "st", "st(1)", "cc", "memory");
	return r;
}

static long double x_add(long double a, long double b)
{
	long double r;
	__asm__ __volatile__("fldt %1\n\tfldt %2\n\tfaddp\n\tfstpt %0" : "=m"(r) : "m"(a), "m"(b) : "st", "st(1)", "cc", "memory");
	return r;
}

// a - b: st(1) - st(0), popped. In this assembler syntax that operation is spelled fsubrp (the popping subtract's two
// mnemonics are exchanged there); main() checks the direction with 3 - 1 before anything else is trusted.
static long double x_sub(long double a, long double b)
{
	long double r;
	__asm__ __volatile__("fldt %1\n\tfldt %2\n\tfsubrp %%st, %%st(1)\n\tfstpt %0" : "=m"(r) : "m"(a), "m"(b) : "st", "st(1)", "cc", "memory");
	return r;
}

static long double x_neg(long double a)
{
	long double r;
	__asm__ __volatile__("fldt %1\n\tfchs\n\tfstpt %0" : "=m"(r) : "m"(a) : "st", "cc", "memory");
	return r;
}

// A store to a float and the float read back: the rounding a stored value gets.
static long double x_stored(long double a)
{
	volatile float f;
	__asm__ __volatile__("fldt %1\n\tfstps %0" : "=m"(f) : "m"(a) : "st", "cc", "memory");
	return f;
}

// An 80-bit value as an array element (with the game's flags a bare long double cannot be one, nor a member of an
// ordinary structure: this one is laid out the plain way).
struct __attribute__((gcc_struct)) Wide
{
	long double v;
	Wide() : v(0) {}
	Wide(long double value) : v(value) {}
};

// ((t[a] + t[b]) + t[c]) where t[k] = left[k] * right[k]: the left value loaded whole, multiplied by the float, one add per
// line, the result stored as a float.
static float cell_by_x87(const Wide *left, const float *right, int a, int b, int c)
{
	float out;
	__asm__ __volatile__(
		"fldt %1\n\tfmuls %2\n\t"
		"fldt %3\n\tfmuls %4\n\t"
		"faddp\n\t"
		"fldt %5\n\tfmuls %6\n\t"
		"faddp\n\t"
		"fstps %0"
		: "=m"(out)
		: "m"(left[a].v), "m"(right[a]), "m"(left[b].v), "m"(right[b]), "m"(left[c].v), "m"(right[c])
		: "st", "st(1)", "cc", "memory");
	return out;
}

static void print80(long double v)
{
	unsigned char raw[sizeof(long double)];
	memcpy(raw, &v, sizeof raw);
	for (int i = 9; i >= 0; i--)
		printf("%02x", raw[i]);
}

static int run_case(int index)
{
	const TurnCase &given = CASES[index];
	// Read through volatile so that the compiler cannot see the numbers: everything is computed at run time.
	const volatile TurnCase *seen = &given;
	float yaw = seen->yaw, pitch = seen->pitch, roll = seen->roll;
	FMatrix mesh;
	float m[3][3];
	for (int k = 0; k < 3; k++)
	{
		for (int j = 0; j < 3; j++)
			m[k][j] = seen->m[k][j];
		mesh.Row[k] = FVector(m[k][0], m[k][1], m[k][2]);
	}
	FMatrix result = GoodiesMeshTurn(yaw, pitch, roll, mesh);

	// The pinned evaluation.
	long double cyx = x_cos(yaw), syx = x_sin(yaw), crx = x_cos(roll), srx = x_sin(roll), cpx = x_cos(pitch), spx = x_sin(pitch);
	long double cy = x_stored(cyx), sy = x_stored(syx), cr = x_stored(crx), sr = x_stored(srx), cp = x_stored(cpx);
	long double sp = spx;
	long double spsr = x_mul(sp, sr), spcr = x_mul(sp, cr);
	Wide rotation[3][3] = {
		{x_sub(x_mul(cr, cy), x_mul(spsr, sy)), x_neg(x_mul(cp, sy)), x_add(x_mul(sr, cy), x_mul(spcr, sy))},
		{x_add(x_mul(cr, sy), x_mul(spsr, cy)), x_mul(cp, cy), x_sub(x_mul(sr, sy), x_mul(spcr, cy))},
		{x_neg(x_mul(cp, sr)), sp, x_mul(cp, cr)},
	};
	Wide float_sine_row[3] = {rotation[2][0], x_stored(sp), rotation[2][2]};

	printf("TRIG %s ", given.name);
	const Wide trig[6] = {cyx, syx, crx, srx, cpx, spx};
	for (int i = 0; i < 6; i++)
	{
		print80(trig[i].v);
		putchar(i == 5 ? '\n' : ' ');
	}
	int status = 0;
	for (int row = 0; row < 3; row++)
		for (int col = 0; col < 3; col++)
		{
			float column[3] = {m[0][col], m[1][col], m[2][col]};
			const int *order = ORDER[row][col];		// first two (either way round), then last
			int a = order[0], b = order[1], c = order[2];
			// the other two orders of the same three terms: the last term is each of the two other terms in turn
			int other[2][3] = {{a, c, b}, {b, c, a}};
			uint32_t expected = bits(cell_by_x87(rotation[row], column, a, b, c));
			uint32_t alternative[2];
			for (int k = 0; k < 2; k++)
				alternative[k] = bits(cell_by_x87(rotation[row], column, other[k][0], other[k][1], other[k][2]));
			uint32_t float_sine = row == 2 ? bits(cell_by_x87(float_sine_row, column, a, b, c)) : 0;
			const FVector &built = result.Row[row];
			const float cell[3] = {built.X, built.Y, built.Z};
			uint32_t actual = bits(cell[col]);
			const char *verdict = "ok";
			bool orders_apart = expected != alternative[0] && expected != alternative[1] && alternative[0] != alternative[1];
			bool sine_apart = row != 2 || float_sine != expected;
			if (((given.require & (1 << row)) && !orders_apart) || ((given.require & REQUIRE_SINE) && !sine_apart))
			{
				verdict = "UNSEPARATED";
				status = status == 1 ? 1 : 3;
			}
			else if (actual != expected)
			{
				verdict = "WRONG";
				status = 1;
			}
			printf("CELL %s %d %c %08x %08x %08x %08x ", given.name, row, "XYZ"[col], expected, actual, alternative[0], alternative[1]);
			if (row == 2)
				printf("%08x", float_sine);
			else
				printf("--------");
			printf(" %s\n", verdict);
		}
	return status;
}

int main()
{
	uint16_t word = 0x007f;
	__asm__ __volatile__("fldcw %0" : : "m"(word));
	uint16_t seen = 0;
	__asm__ __volatile__("fnstcw %0" : "=m"(seen));
	printf("CONTROL %04x\n", seen);
	volatile long double three = 3.0L, one_ = 1.0L;
	if (x_sub(three, one_) != 2.0L)
	{
		printf("SUBTRACT wrong way round\n");
		return 4;
	}
	int status = 0;
	for (int i = 0; i < CASE_COUNT; i++)
	{
		int one = run_case(i);
		if (one == 1 || status == 1)
			status = 1;
		else if (one)
			status = one;
	}
	return status;
}
