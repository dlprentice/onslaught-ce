// bytesprite.h: CByteSprite, a set of run-length 8-bit sprite frames drawn into a byte bitmap (bytesprite.obj,
// 00418470..00418bd6; CDXCompass::PlatformInit allocates one, 0053be5b, and targets the compass's 512x30 strip,
// 0053c1a0). Not in Stuart's release: reconstructed from the retail object.
//
// A frame is four signed bytes (left and top relative to the source frame's centre, width, height) and then, for
// each row, packets: n > 0 copies n pixels, n < 0 skips -n, and 0 ends the row.

#ifndef BYTESPRITE_H
#define BYTESPRITE_H

class CByteSprite
{
public:
	CByteSprite();														// 00418470
	~CByteSprite();														// 00418480
	SINT	Load(char *name, SINT width, SINT height, SINT frames, UBYTE key);	// 004184c0: the frames read
	void	SetTarget(void *pixels, SINT pitch, SINT width, SINT height, UBYTE wrap);	// 00418720
	void	DrawRLE_NoClip(char *s, SINT x, SINT y, SINT h);			// 00418750
	void	DrawRLE_ClipLeft(char *s, SINT x, SINT y, SINT h);			// 004187e0
	void	DrawRLE_ClipRight(char *s, SINT x, SINT y, SINT h);			// 00418880
	void	DrawFrame(SINT frame, SINT x, SINT y);						// 00418920

	UBYTE	*mData;							// +0x00: every frame's packets
	SINT	*mFrameOffsets;					// +0x04: each frame's offset into mData
	SINT	mNumFrames;						// +0x08
	UBYTE	*mTarget;						// +0x0c
	SINT	mPitch;							// +0x10
	SINT	mTargetWidth;					// +0x14
	SINT	mTargetHeight;					// +0x18
	UBYTE	mWrap;							// +0x1c: a frame off one side is drawn again at the other
};

// Load's scratch encoder (0x24 bytes): raw frames in, packets out.
class CByteSpriteEncoder
{
public:
	CByteSpriteEncoder(SINT width, SINT height, SINT frames, UBYTE key);
	~CByteSpriteEncoder();
	BOOL	EncodeFrame();												// 004189f0

	UBYTE	*mFrame;						// +0x00: the raw frame being encoded
	UBYTE	*mOut;							// +0x04
	SINT	*mOffsets;						// +0x08
	UBYTE	*mOutPtr;						// +0x0c
	SINT	*mOffsetPtr;					// +0x10
	SINT	mWidth;							// +0x14
	SINT	mHeight;						// +0x18
	SINT	mFrames;						// +0x1c
	UBYTE	mKey;							// +0x20: values at or above it are transparent
};

#endif
