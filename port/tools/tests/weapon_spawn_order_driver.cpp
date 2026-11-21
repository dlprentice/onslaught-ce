// SPDX-License-Identifier: GPL-3.0-only
// The 18 cells of CWeapon::SpawnRound's two orientation products (src/weapon.cpp, patch 0110), run as compiled.
//
// The test (port/tools/weapon_spawn_order_tests.py) writes three files beside this one: maths.h (the build's text of it),
// spawn_products.inc (FSpawnOri and FSpawnTurn exactly as the patched weapon.cpp holds them) and spawn_case.h (the matrices
// and the pinned order of every cell). This driver compiles them with the game's own flags, sets the x87 control word the
// game runs with (007f: single-precision mantissa, round to nearest), multiplies, and compares each cell's bits with the
// value an explicit x87 evaluation gives: one load and multiply per term, the first two terms added, then the third.
// For every cell it also evaluates the other two orders and reports them; the test's matrices make all three differ.
//
// Output, one line per cell: CELL product row column expected actual alternative-1 alternative-2 verdict
// Exit: 0 every cell as pinned and every cell's three orders told apart, 1 a cell differs from its pinned order,
// 3 the matrices do not tell a cell's three orders apart (the data is too weak to pin anything).
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// the two types maths.h takes from the game's common.h and windows.h (the same underlying types)
typedef int BOOL;
typedef signed int SINT;
#include "maths.h"
#include "spawn_products.inc"
#include "spawn_case.h"

static uint32_t bits(float f)
{
	uint32_t u;
	memcpy(&u, &f, sizeof u);
	return u;
}

// ((t[a] + t[b]) + t[c]) where t[k] = left[k] * right[k]: x87 loads and multiplies in the order written, one add per line.
static float by_x87(const float *left, const float *right, int a, int b, int c)
{
	float out;
	__asm__ __volatile__(
		"flds %1\n\tfmuls %2\n\t"
		"flds %3\n\tfmuls %4\n\t"
		"faddp\n\t"
		"flds %5\n\tfmuls %6\n\t"
		"faddp\n\t"
		"fstps %0"
		: "=m"(out)
		: "m"(left[a]), "m"(right[a]), "m"(left[b]), "m"(right[b]), "m"(left[c]), "m"(right[c])
		: "st", "st(1)", "cc", "memory");
	return out;
}

template <typename Product>
static int check(const char *name, int product, const float (&left_values)[3][3], const float (&right_values)[3][3])
{
	// Read through volatile pointers so that the compiler cannot see the numbers: the products are formed at run time, from memory.
	Product left;
	FMatrix right;
	for (int i = 0; i < 3; i++)
	{
		const volatile float *l = left_values[i], *r = right_values[i];
		left.Row[i] = FVector(l[0], l[1], l[2]);
		right.Row[i] = FVector(r[0], r[1], r[2]);
	}
	FMatrix result = left * right;
	int status = 0;
	for (int row = 0; row < 3; row++)
		for (int col = 0; col < 3; col++)
		{
			float column[3] = {right_values[0][col], right_values[1][col], right_values[2][col]};
			const int *order = ORDER[product][row][col];		// first two (either way round), then last
			int a = order[0], b = order[1], c = order[2];
			// the other two orders of the same three terms: the last term is each of the two other terms in turn
			int other[2][3] = {{a, c, b}, {b, c, a}};
			uint32_t expected = bits(by_x87(left_values[row], column, a, b, c));
			uint32_t alternative[2];
			for (int k = 0; k < 2; k++)
				alternative[k] = bits(by_x87(left_values[row], column, other[k][0], other[k][1], other[k][2]));
			const FVector &built = result.Row[row];
			const float cell[3] = {built.X, built.Y, built.Z};		// each member by its name
			uint32_t actual = bits(cell[col]);
			const char *verdict = "ok";
			if (expected == alternative[0] || expected == alternative[1] || alternative[0] == alternative[1])
			{
				verdict = "UNSEPARATED";
				status = status == 1 ? 1 : 3;
			}
			else if (actual != expected)
			{
				verdict = "WRONG";
				status = 1;
			}
			printf("CELL %s %d %c %08x %08x %08x %08x %s\n", name, row, "XYZ"[col], expected, actual, alternative[0], alternative[1], verdict);
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
	int status = check<FSpawnOri>("FSpawnOri", 0, LEFT[0], RIGHT[0]);
	int turn = check<FSpawnTurn>("FSpawnTurn", 1, LEFT[1], RIGHT[1]);
	if (turn == 1 || status == 1)
		return 1;
	return status ? status : turn;
}
