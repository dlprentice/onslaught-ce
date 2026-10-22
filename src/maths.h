// maths.h: the game's vector and matrix types.
//
// FVector is 16 bytes (X, Y, Z and an unset fourth dword): a matrix row copies all four dwords (the identity
// initializer at 004a3030), and GoToPoint(FVector, BOOL) pops 0x14 bytes (00401520). FMatrix is three rows.
// Out-of-line copies in actor.obj: the constructor (00401ec0), operator+ (00401ee0), FMatrix(row, row, row)
// (00401f10).

#ifndef MATHS_H
#define MATHS_H

#include <math.h>

#ifndef PI
#define PI	3.14159265358979f		// as TransitionHelpers.h has it
#endif
#define PI_M2	(PI * 2.0f)		// Stuart's name (BattleEngine.cpp)

// Stuart's three-float vector (DXFrontend.cpp's light colours).
class FVector3
{
public:
	float	X, Y, Z;

	FVector3() {}
	FVector3(float x, float y, float z) : X(x), Y(y), Z(z) {}
	FVector3	&operator*=(float f) { X *= f; Y *= f; Z *= f; return *this; }
#ifdef PDSIMPLESPRITE_CPP
	FVector3	&operator=(const class FVector &v);
#endif
};

class FVector
{
public:
	float	X, Y, Z;
	float	Pad;

	FVector() {}
	FVector(float x, float y, float z) : X(x), Y(y), Z(z) {}	// FUNCTION: 0x00401ec0

	FVector	operator+(const FVector &a) const { return FVector(X + a.X, Y + a.Y, Z + a.Z); }	// FUNCTION: 0x00401ee0
	FVector	operator-(const FVector &a) const { return FVector(X - a.X, Y - a.Y, Z - a.Z); }
	FVector	operator-() const { return FVector(-X, -Y, -Z); }	// CDXEngine::DrawDebugCuboid (0053d8d9..0053d8fe)
	FVector	operator*(float f) const { return FVector(X * f, Y * f, Z * f); }
	FVector	operator/(float f) const { return FVector(X / f, Y / f, Z / f); }	// Stuart's callers; not checked yet
	// Each product bracketed, as in the other sums of this header. With CMap::Normal's forwarder in place this matches
	// DeclareOnGround 0040c750; bracketing the tail as a group (`X*a.X + (Y*a.Y + Z*a.Z)`) does not.
	float	operator*(const FVector &a) const { return (X * a.X) + (Y * a.Y) + (Z * a.Z); }
	void	operator+=(const FVector &a) { X += a.X; Y += a.Y; Z += a.Z; }	// FVector::operator+= (void, as retail: out of line in Camera.obj 0041ad10)
	// void, as retail: the out-of-line copy Line.obj contributed first (00490900) returns nothing, and Stuart's CSphere
	// calls it (004e53c4)
	// FUNCTION: 0x00490900
	void	operator-=(const FVector &a) { X -= a.X; Y -= a.Y; Z -= a.Z; }
	FVector	&operator*=(float f) { X *= f; Y *= f; Z *= f; return *this; }		// Stuart's callers (BattleEngineJetPart.cpp)
	void	operator/=(float f) { X /= f; Y /= f; Z /= f; }		// (BattleEngineWalkerPart.cpp); void, as retail: out of line in MCBuggy.obj (004944b0)
	// The cross product (Stuart's operator, BattleEngine.cpp); out of line in BattleEngineJetPart.obj at 00411a60.
	// Unbracketed. Brackets are IL tuples that move scheduling regions (see MakeRotationF): on the first two terms they
	// matched MakeFromDirection 0049e4b0 until Normalise's terms were bracketed, after which they cost CFallingTree's
	// constructor 004f5f60; a bracket on the third term changes the out-of-line copy 00411a60.
// STAND-IN for DropshipGuide 00448930 only: its actual forward receiver is const,
// while its side receiver is mutable. These copied cross operations and scalar-field
// groups reproduce the different retail operand load orders without reassociation,
// added emitted operations or layout changes. Original overload ownership and spelling
// are unknown. With the other documented CPP controls, the plain cross leaves 16 bytes;
// four consumed-field groups leave 4; the two const-only Z groups give the whole match.
// Complete-product groups were neutral; explicit named-member syntax changed inlining.
// Replace this guard/overload fallback only with a supported natural form preserving the
// complete section, all references and existing sibling/corpus matches. Both inline;
// other translation units retain the existing operator below.
#ifdef DROPSHIPGUIDE_CPP
	FVector	operator^(const FVector &a) const { return FVector((Y) * a.Z - (Z) * a.Y, (Z) * a.X - (a.Z) * X, (a.Y) * X - (Y) * a.X); }
	FVector	operator^(const FVector &a) { return FVector((Y) * a.Z - Z * a.Y, Z * a.X - (a.Z) * X, (a.Y) * X - (Y) * a.X); }
#else
	FVector	operator^(const FVector &a) const { return FVector(Y * a.Z - Z * a.Y, Z * a.X - a.Z * X, X * a.Y - Y * a.X); }
#endif

	BOOL	operator==(const FVector &a) const { return (X == a.X) && (Y == a.Y) && (Z == a.Z); }	// CUnit::GetLaunchPosition (004fc465..004fc492)
	BOOL	operator!=(const FVector &a) const { return (X != a.X) || (Y != a.Y) || (Z != a.Z); }	// CBattleEngine::Init (0040526a..00405290)

	// Stuart's name (CFEPGoodies: blacklight.mDir.Set(1.0f,0.0f,0.0f)); its out-of-line copy is engine.obj's
	// (0044a5f0).
	void	Set(float x, float y, float z) { X = x; Y = y; Z = z; }
	// Three floats from a mesh file (out of line at 0044c1c0; CMesh::Load and the old part readers call it).
	void	Read(class CDXMemBuffer *file);

	// The bracketed terms (as in MagnitudeXY, MagnitudeSq and Normalise) are what retail's scheduling needs: CRound::Init
	// 004d8410, CWarspiteDomeAI 00504e50, IScript::GetDistToObj 00536070 (a sweep).
	float	Magnitude() const { return sqrtf((X * X) + (Y * Y) + (Z * Z)); }
#ifdef MCMECH_CPP	// CMCMech::UpdateBone only; the other objects keep their declaration counts
	// Private Mech real norm operations; original declaring ownership unknown.
	float	MagnitudeZY() const { float z; memcpy(&z, &Z, sizeof(z)); return sqrtf(((z * z) + Y * Y) + X * X); }
	float	MagnitudeYZ() const { return sqrtf(((Y * Y) + (Z * Z)) + (X * X)); }
#endif
	float	MagnitudeXY() const { return sqrtf((X * X) + (Y * Y)); }	// the bracketed terms are an FPU value barrier: the faddp stays before a following integer test
#ifdef MCMECH_CPP
	// PRIVATE STAND-IN (CMCMech::UpdateBone 00499e30, leg step distance): MagnitudeXY with only Y*Y bracketed, so the
	// X square's faddp has no no-code node before it (0049aa4c). Same operations; a separate member because MagnitudeXY's
	// own body is what fourteen other matched functions need. Original spelling unknown.
	float	MagnitudeXYLate() const { return sqrtf(X * X + (Y * Y)); }
#endif
	float	MagnitudeXYSq() const { return (X * X) + (Y * Y); }	// CDropship::IsAnchorClear (00447dd8..00447df1)
	float	MagnitudeSq() const { return (X * X) + (Y * Y) + (Z * Z); }	// Stuart's name (BattleEngineJetPart.cpp)
	// Stuart's name (DXEngine.cpp); CUnit::Hit (004fcd30..004fcd79), CFEPBEConfig::Render (00450929..0045097e).
	// The reciprocal reuses m (CComponent::StartDieProcess 00428800, CTree::Damage 004f68e0): with a separate `inv`
	// local the X *= step takes its operands in the other order in some callers. `if (m)`, not `m != 0`: the bracketed
	// terms cost inline budget, and the cheaper test keeps CDXFrontEnd::RenderParticles (00540c30) inlining
	// SetLightEnable(2) as retail does.
	void	Normalise() { float m = sqrtf((X * X) + (Y * Y) + (Z * Z)); if (m) { m = 1.0f / m; X *= m; Y *= m; Z *= m; } }
	// Stuart's names; decoded from the inline copies in CGame::ToggleFreeCameraOn (0047046d..004704d8).
	// VC6's float wrapper preserves the operand setup in CInfantryUnit::Init (00488c3d..00488c43) and
	// the scheduling boundary before CPanCamera::Update's final matrix copy (00419ce5..00419d03).
	float	Azimuth() const { return -atan2f(X, Y); }
	// asinf preserves the float argument rounding in the inline copy at 004704a5..004704af.
	float	Elevation() const { float m = Magnitude(); if (m > 0.f) return asinf(Z / m); return 0.f; }
#ifdef PDSIMPLESPRITE_CPP	// the sprite drawing's (out of line in PDSimpleSprite.obj, 004c78b0..004c7900); names ours
	void	Scale(float f) { X *= f; Y *= f; Z *= f; }
	float	InvMagnitude() const { return 1.f / sqrtf(X * X + Y * Y + Z * Z); }
	void	NormaliseIfLong() { float m = X * X + Y * Y + Z * Z; if (m > 0) { float inv = 1.f / (float)sqrt(m); X *= inv; Y *= inv; Z *= inv; } }
#endif
};

#ifdef PDSIMPLESPRITE_CPP
// a vertex position from a vector (out of line in PDSimpleSprite.obj, 004c7d90)
inline FVector3 &FVector3::operator=(const FVector &v) { X = v.X; Y = v.Y; Z = v.Z; return *this; }
#endif

// Stuart's (BattleEngine.cpp): a - b, with b taken round by 2PI when the two sit either side of +-PI/2. Out of line
// in BattleEngine.obj (0040b660, cdecl).
inline float AngleDifference(float a, float b)
{
	if ((a < -PI / 2) && (b > PI / 2))		// b adjusted, one subtraction: inlined copies share the tail
		b -= PI_M2;							// (CBattleEngine::GetInterpolatedEulerOrientation 0040d6b5)
	else if ((a > PI / 2) && (b < -PI / 2))
		b += PI_M2;
	return a - b;
}

class FMatrix
{
public:
	FVector	Row[3];

	FMatrix() {}
	FMatrix(const FVector &a, const FVector &b, const FVector &c) { Row[0] = a; Row[1] = b; Row[2] = c; }	// FUNCTION: 0x00401f10
	// Decoded from the inline copy in CActor::Init (00401255..0040131a).
	FMatrix(float yaw, float pitch, float roll) { MakeRotationF(yaw, pitch, roll); }
	// Row by row, each through its own FVector (CMCBuggy's identity, 00494961..004949e1).
	FMatrix(float xx, float xy, float xz, float yx, float yy, float yz, float zx, float zy, float zz)
	{
		Row[0] = FVector(xx, xy, xz);
		Row[1] = FVector(yx, yy, yz);
		Row[2] = FVector(zx, zy, zz);
	}

	// The constructor's rotation, in place (CFEPGoodies::Process 0045d973..0045da3b). The float trig and the bracketed
	// values emit the same code as (float)cos() and bare expressions, but VC6 keeps each bracket, and each inline cosf,
	// as an extra IL tuple (about 22 here). VC6 schedules a long basic block in regions of about 82 tuples and moves
	// nothing across a region edge, so the tuples decide where the code after an inlined rotation lands: with them it
	// stays after the rotation as in retail (CActor::Init 004011e0, CComplexThing::Init 004f3fd0, CFEPGoodies::
	// ButtonPressed 0045cde0, the segments' GetPartTransform 00494ce0, the script's TeleportOrientation 00536a60).
	// cosf is also cheaper for the inline budget than (float)cos, which keeps CBattleEngine::GetRenderOrientation
	// (004060b0) inlining its first and third copies and calling the second. The brackets sit on the rows, not on the
	// trig values: when two angles are the same constant (CFEPGoodies::Render's MakeRotationF(0, 0, 0) and
	// (0, 0.5f, 0), 0045ee05 and 0045ef36) the optimizer merges the equal cosf/sinf values and drops their brackets
	// with them, while the rows' brackets stay. The spelling is a reproduction of retail's tuple count, not retail's
	// text. Stand-in by a ruling of October 2: Stuart's GPL release has no maths.h,
	// so this body's text is lost, and bracket fitting stays confined to it. Without the brackets 18 matches go, and with
	// every triple or double bracket reduced to one level 17 (all but 004060b0): the exact tuple count is load-bearing
	// (natural-form review 2026-10-15: 004011e0 00404210 004060b0 00410c50 00413360 00419b00 004250f0 004505b0 0045cde0
	// 0045e0d0 00494ce0 004c4dc0 004f0c50 004f3fd0 004fa4b0 0051e1b0 00536a60 00543300; README, "MakeRotationF's
	// tuple-count stand-in").
	void	MakeRotationF(float yaw, float pitch, float roll)
	{
		float cy = cosf(yaw), sy = sinf(yaw);
		float cr = cosf(roll), sr = sinf(roll);
		float cp = cosf(pitch), sp = sinf(pitch);
		Row[0].X = (((cr * cy - sp * sr * sy)));	Row[0].Y = (((-cp * sy)));	Row[0].Z = ((sp * cr * sy + sr * cy));
		Row[1].X = ((sp * sr * cy + cr * sy));	Row[1].Y = (((cp * cy)));		Row[1].Z = (sr * sy - sp * cr * cy);
		Row[2].X = (-cp * sr);				Row[2].Y = sp;			Row[2].Z = (cp * cr);
	}

	// A rotation about Z only (Stuart's callers: ori.MakeRotationYawF(yaw); CFrontEndThing::Process 0046baf0).
	// The float trig carries retail's extra tuples and inline cost (see MakeRotationF): CTree::Init 004f6080.
	void	MakeRotationYawF(float yaw)
	{
		Row[0] = FVector(cosf(yaw), -sinf(yaw), 0.f);
		Row[1] = FVector(sinf(yaw), cosf(yaw), 0.f);
		Row[2] = FVector(0.f, 0.f, 1.f);
	}

	// Stuart's caller: pitch.MakeRotationPitchF(1.570796f) (his FEPGoodies.cpp). About X.
	void	MakeRotationPitchF(float pitch)
	{
		Row[0] = FVector(1.f, 0.f, 0.f);
		Row[1] = FVector(0.f, (float)cos(pitch), -(float)sin(pitch));
		Row[2] = FVector(0.f, (float)sin(pitch), (float)cos(pitch));
	}

	// About Y, the forward axis.
	void	MakeRotationRollF(float roll)
	{
		Row[0] = FVector((float)cos(roll), 0.f, (float)sin(roll));
		Row[1] = FVector(0.f, 1.f, 0.f);
		Row[2] = FVector(-(float)sin(roll), 0.f, (float)cos(roll));
	}

	// Angles in 4096ths of a turn, rolled, then pitched, then yawed; it does not return this, so it is not a
	// constructor.
	void	MakeRotation(SINT yaw, SINT pitch, SINT roll);	// 004f8140, defined in Unit.cpp (CWeapon's constructor calls it too)
	inline void	MakeRotationCore(SINT yaw, SINT pitch, SINT roll);

	// Stuart's callers: se->mPos=rcori.Transpose() * se->mPos (CSoundManager::UpdateSoundPosition)
	// Its columns (names ours). Standalone matrix receivers at 004e16b2 and 004b08cf establish first-column use;
	// a folded CMeshPart alias is possible, not proven. Float locals preserve CAirGuide's constructor arguments; declared
	// z, y, x, retail's load order (its out-of-line copy 004aa3f0 loads Row[2].X first).
	FVector	GetColumnX() const { float z = Row[2].X, y = Row[1].X, x = Row[0].X; return FVector(x, y, z); }	// FUNCTION: 0x004aa3f0
	FVector	GetColumnY() const { return FVector(Row[0].Y, Row[1].Y, Row[2].Y); }
	FVector	GetColumnZ() const { return FVector(Row[0].Z, Row[1].Z, Row[2].Z); }
	// FUNCTION: 0x004c5280
	FMatrix	Transpose() const { return FMatrix(GetColumnX(), GetColumnY(), GetColumnZ()); }

	void	operator/=(float f) { Row[0] /= (f); Row[1] /= (f); Row[2] /= (f); }
	// Stuart's callers: ori.TransposeInPlace() (CMeshCollisionVolume::CachePartTransform 004ad757..004ad793).
	void	TransposeInPlace()
	{
		float	t;
		t = Row[0].Y;	Row[0].Y = Row[1].X;	Row[1].X = t;
		t = Row[0].Z;	Row[0].Z = Row[2].X;	Row[2].X = t;
		t = Row[1].Z;	Row[1].Z = Row[2].Y;	Row[2].Y = t;
	}

	// The determinant accumulated row by row in a local, as the FMatrix of Stuart's earlier MoHo/Ball Breakers codebase
	// writes it (DCMoHo fcoords.h, FMatrix::Determinant); out of line in MCMech.obj (0049bc40). Retail's inlined
	// inverses recompute the cofactor minors and keep the determinant on the x87 stack (CMCMech::Init 00498c40, Mat34
	// InvertBasisToOut 00494350).
	float	Determinant() const
	{
		float	result;
		result =  Row[0].X * ((Row[1].Y * Row[2].Z) - (Row[1].Z * Row[2].Y));
		result -= Row[0].Y * ((Row[1].X * Row[2].Z) - (Row[1].Z * Row[2].X));
		result += Row[0].Z * ((Row[1].X * Row[2].Y) - (Row[1].Y * Row[2].X));
		return result;
	}
	// The cofactors, the determinant and the division the inverse is built from; MCMech.obj emits them out of line
	// (0049bc80, 0049bc40, 0049bbb0). Each product bracketed, as in the other helpers (the determinant's copy matches so),
	// and each cofactor whole, sign times bracketed minor. A bracket is an IL tuple that takes a temporary in C2's
	// common-subexpression pass (0x107130cb), which numbers the row addresses' temporaries in tuple order, and the operand
	// rank pass (0x1070fc45) orders each product by those ids: the brackets on the unsigned cofactors give retail's order
	// in five products (0049bc80 matches). The inverse (00494350) keeps one difference: retail schedules the lea
	// of the first FVector::operator/= call after the determinant's last product, ours before its subtraction.
	FMatrix	Cofactors() const
	{
		FMatrix	c;
		c.Row[0].X =  ((Row[1].Y * Row[2].Z) - (Row[1].Z * Row[2].Y));
		c.Row[0].Y = -((Row[1].X * Row[2].Z) - (Row[2].X * Row[1].Z));
		c.Row[0].Z =  ((Row[1].X * Row[2].Y) - (Row[2].X * Row[1].Y));
		c.Row[1].X = -((Row[0].Y * Row[2].Z) - (Row[0].Z * Row[2].Y));
		c.Row[1].Y =  ((Row[0].X * Row[2].Z) - (Row[2].X * Row[0].Z));
		c.Row[1].Z = -((Row[0].X * Row[2].Y) - (Row[2].X * Row[0].Y));
		c.Row[2].X =  ((Row[1].Z * Row[0].Y) - (Row[0].Z * Row[1].Y));
		c.Row[2].Y = -((Row[1].Z * Row[0].X) - (Row[1].X * Row[0].Z));
		c.Row[2].Z =  ((Row[1].Y * Row[0].X) - (Row[1].X * Row[0].Y));
		return c;
	}
	// The inverse: the cofactors, transposed and divided by the determinant (out of line in MCBuggy.obj, 00494350).
	FMatrix	Inverse() const
	{
		FMatrix	r = Cofactors();
		r.TransposeInPlace();
		r /= Determinant();
		return r;
	}

	FMatrix	operator+(const FMatrix &m) const { return FMatrix(Row[0] + m.Row[0], Row[1] + m.Row[1], Row[2] + m.Row[2]); }
	FMatrix	operator-(const FMatrix &m) const { return FMatrix(Row[0] - m.Row[0], Row[1] - m.Row[1], Row[2] - m.Row[2]); }
	// Before the matrix product, as in Stuart's ancestor (DCMoHo fcoords.h; retail emits it first, 0040d2c0). Written out in full: built from FVector's dot product, the constructor is not inlined (CThing::GetCentrePos 004f3ac0).
	// Each product bracketed (from CSquadNormal::BuildColumnFormation 004e8730; fourteen functions match so).
	FVector	operator*(const FVector &v) const { return FVector((Row[0].X * v.X) + (Row[0].Y * v.Y) + (Row[0].Z * v.Z), (Row[1].X * v.X) + (Row[1].Y * v.Y) + (Row[1].Z * v.Z), (Row[2].X * v.X) + (Row[2].Y * v.Y) + (Row[2].Z * v.Z)); }
	FMatrix	operator*(const FMatrix &m) const	// CFEPGoodies::ButtonPressed (0045d44a..0045d5ad)
	{
		return FMatrix(
					   FVector(Row[0].X * m.Row[0].X + Row[0].Y * m.Row[1].X + Row[0].Z * m.Row[2].X,
							   Row[0].X * m.Row[0].Y + Row[0].Y * m.Row[1].Y + Row[0].Z * m.Row[2].Y,
							   Row[0].X * m.Row[0].Z + Row[0].Y * m.Row[1].Z + Row[0].Z * m.Row[2].Z),
					   FVector(Row[1].X * m.Row[0].X + Row[1].Y * m.Row[1].X + Row[1].Z * m.Row[2].X,
							   Row[1].X * m.Row[0].Y + Row[1].Y * m.Row[1].Y + Row[1].Z * m.Row[2].Y,
							   Row[1].X * m.Row[0].Z + Row[1].Y * m.Row[1].Z + Row[1].Z * m.Row[2].Z),
					   FVector(Row[2].X * m.Row[0].X + Row[2].Y * m.Row[1].X + Row[2].Z * m.Row[2].X,
							   Row[2].X * m.Row[0].Y + Row[2].Y * m.Row[1].Y + Row[2].Z * m.Row[2].Y,
							   Row[2].X * m.Row[0].Z + Row[2].Y * m.Row[1].Z + Row[2].Z * m.Row[2].Z));
	}
	FMatrix	operator*(float f) const { return FMatrix(Row[0] * f, Row[1] * f, Row[2] * f); }
#if defined(EXPLOSION_CPP) || defined(MESH_CPP)	// defined in explosion.obj (0044c210); CMesh::Load reads with it. The other objects keep their declaration counts
	void	Read(class CDXMemBuffer *file);
#endif
	// re-orthonormalise (Stuart's; CInterpolatedCamera's constructor 0041af95..0041afdb)
	// FUNCTION: 0x00425760 ?Normalise@FMatrix@@QAEXXZ
	void	Normalise() { Row[0].Normalise(); Row[1].Normalise(); Row[2] = Row[0] ^ Row[1]; Row[2].Normalise(); Row[1] = Row[2] ^ Row[0]; }
	// Columns from a direction and an up vector: side, up and back (names ours). MCTentacle.obj emits it out of line
	// (0049e4b0), for each bone along the curve.
	void	MakeFromDirection(FVector dir, FVector up)
	{
		FVector	back = -dir;
		FVector	side = back ^ up;
		FVector	top = back ^ side;
		side.Normalise();
		top.Normalise();
		back.Normalise();
		Row[0].X = side.X;	Row[0].Y = top.X;	Row[0].Z = back.X;
		Row[1].X = side.Y;	Row[1].Y = top.Y;	Row[1].Z = back.Y;
		Row[2].X = side.Z;	Row[2].Y = top.Z;	Row[2].Z = back.Z;
	}
#ifdef MCMECH_CPP
	// PRIVATE EXPERIMENT: native BODY input-up remains live across back construction.
	// Keep the existing by-value direction and actual FMatrix member owner; copy
	// all four up DWORDs after back. Original overload/ownership is unknown.
	void	MakeBodyDirectionWithLateUp(FVector dir, const FVector &inputUp)
	{
		FVector	back = -dir;
		FVector up = inputUp;
		FVector	side = back ^ up;
		FVector	top = back ^ side;
		side.Normalise();
		top.Normalise();
		back.Normalise();
		Row[0].X = side.X;	Row[0].Y = top.X;	Row[0].Z = back.X;
		Row[1].X = side.Y;	Row[1].Y = top.Y;	Row[1].Z = back.Y;
		Row[2].X = side.Z;	Row[2].Y = top.Z;	Row[2].Z = back.Z;
	}
#endif
	BOOL	operator!=(const FMatrix &m) const { return (Row[0] != m.Row[0]) || (Row[1] != m.Row[1]) || (Row[2] != m.Row[2]); }	// Stuart's callers; not placed yet
};

// Euler angles (12 bytes): CUnit keeps two (+0x114, +0x12c). The constructor from a matrix is out of line
// (0044adb0): the pitch is the arcsine of Row[2].Y, as FMatrix(yaw, pitch, roll) builds it.
class CEulerAngles		// Stuart's name and members (his game.cpp and BattleEngine.h)
{
public:
	CEulerAngles() {}
	// By reference: CInfantryUnit::Init reads the yaw through its address and makes temporaries for the zeros
	// (00488cbb..00488cf6); CMine::Init and CDropshipGuide call the out-of-line copy with three addresses; CAirUnit::Init uses it at 00402b0c..00402b2f.
	CEulerAngles(const float &yaw, const float &pitch, const float &roll) : mYaw(yaw), mPitch(pitch), mRoll(roll) {}	// FUNCTION: 0x00449560
	CEulerAngles(const FMatrix &m);											// 0044adb0

	// CUnit::Move compares its target orientation with its current one (004fa92f..004fa966)
	BOOL	operator!=(const CEulerAngles &a) const { return (mYaw != a.mYaw) || (mPitch != a.mPitch) || (mRoll != a.mRoll); }

	float	mYaw;															// +0x00
	float	mPitch;															// +0x04
	float	mRoll;															// +0x08
};

#endif
