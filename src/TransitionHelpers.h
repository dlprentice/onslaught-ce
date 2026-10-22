// TransitionHelpers.h: the page-transition helpers (reconstructed from their inline copies).

#ifndef TRANSITIONHELPERS_H
#define TRANSITIONHELPERS_H

// The transition's progress through [start, end], clamped to [0, 1] (CFEPLoadGame::Render 00461dbf: (t - 0.75) * 4).
// It works on its parameter: with a local instead, VC6 keeps the (transition - start) for a caller's own later
// (transition - start) across calls, where retail recomputes it (CFEPDirectory::RenderSaveFileList 0051aea4, 0051af0f).
inline float RangeTransition(float transition, float start, float end)
{
	transition = (transition - start) / (end - start);
	if (transition < 0.f)
		transition = 0.f;
	else if (transition > 1.f)
		transition = 1.f;
	return transition;
}

#ifndef PI
#define PI	3.14159265358979f
#endif

// v limited to lo..hi (CFEPCommon::Render 00452bb2).
inline float Clamped(float v, float lo, float hi)
{
	float r = v;
	if (v < lo)
		r = lo;
	else if (v > hi)
		r = hi;
	return r;
}

// Stuart's Clamp and SmoothResize (FEPGoodies.cpp; their header is not in his release), decoded from
// CFEPGoodies::ButtonPressed (0045d219..0045d2b8) and Process (0045d835..0045d860).
inline void Clamp(float &v, float lo, float hi)
{
	if (v < lo)
		v = lo;
	if (v > hi)
		v = hi;
}

inline void SmoothResize(float &v, float target, float factor, float epsilon)
{
	v += (target - v) * factor;
	if (fabsf(target - v) < epsilon)
		v = target;
}

// Between a and b as t goes 0..1.
inline float Range(float t, float a, float b)
{
	return a + ((b - a) * t);
}

// A cosine ease between a (t = 0) and b (t = 1) (CFrontEnd::DrawSlidingTextBordersAndMask 0046748f).
inline float SafeSqrt(float x)
{
	if (x != 0.f)
		x = sqrtf(x);
	return x;
}

inline float CosResize(float t, float a, float b)
{
	float c = cosf(t * PI);
	float f = ((((c < 0.f) ? -1.f : 1.f) * SafeSqrt(fabsf(c))) / 2.f) + 0.5f;
	return ((1.f - f) * b) + (f * a);
}

// A 0..1 fraction as an alpha byte (CFrontEnd::DrawStandardVideoBackground 00467a1d: * 255, fistp, clamped).
inline SINT MakeAlpha(float f)
{
	SINT a = SINT(f * 255.f);
	if (a < 0)
		a = 0;
	else if (a > 0xff)
		a = 0xff;
	return a;
}

// The colour with its alpha scaled by alpha / 256 (00467a94..00467ac8).
inline DWORD BlendAlpha2(DWORD col, SINT alpha)
{
	return (col & 0x00ffffff) | ((((col >> 24) * alpha) << 16) & 0xff000000);
}

// The colour's own alpha scaled by alpha / 256, rounded down (FEMessBox::Render 0044d707..0044d73b: masked, then
// shifted 16, and or-ed with the colour; a mask after the shift turns into a bit merge instead).
inline DWORD ScaleAlpha(DWORD col, SINT alpha)
{
	return ((((col >> 24) * alpha) & 0xffffff00) * 0x10000) | (col & 0x00ffffff);
}

inline void BlendAlpha(DWORD &col, SINT alpha)
{
	col = BlendAlpha2(col, alpha);
}

#endif
