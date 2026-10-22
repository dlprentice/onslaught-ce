#ifndef BEA_MATHS_INTEGER_H
#define BEA_MATHS_INTEGER_H
#include "maths.h"

inline void FMatrix::MakeRotationCore(SINT yaw, SINT pitch, SINT roll)
{
	float	r = roll * 0.0015339808f;
	Row[0] = FVector(cosf(r), 0.f, sinf(r));
	Row[1] = FVector(0.f, 1.f, 0.f);
	Row[2] = FVector(-sinf(r), 0.f, cosf(r));

	float	pt = pitch * 0.0015339808f;
	FMatrix	p;
	p.Row[0] = FVector(1.f, 0.f, 0.f);
	p.Row[1] = FVector(0.f, cosf(pt), -sinf(pt));
	p.Row[2] = FVector(0.f, sinf(pt), cosf(pt));
	*this = p * *this;

	float	yw = yaw * 0.0015339808f;
	p.Row[0] = FVector((float)cos(yw), -(float)sin(yw), 0.f);
	p.Row[1] = FVector((float)sin(yw), (float)cos(yw), 0.f);
	p.Row[2] = FVector(0.f, 0.f, 1.f);
	*this = p * *this;
}
#endif
