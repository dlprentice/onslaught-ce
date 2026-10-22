// TokenArchive.h: the particle system editor's text archive (ParticleSystemEd files: one "token value" per line).
// Reconstructed from TokenArchive.obj (004f51c0..004f5e3f); Stuart's release has no copy. The token names are the
// file's own; the enum names follow them.

#ifndef TOKENARCHIVE_H
#define TOKENARCHIVE_H

class CDXMemBuffer;
class CParticleDescriptor;

enum EParticleSystemToken
{
	PST_NONE = -1,
	PST_FILE_HEADER,                     // 0: "ParticleSystemEd_File_(C)2000_Lost_Toys_Ltd"
	PST_FILE_VERSION,                    // 1: "File_Version"
	PST_NUM_PARTICLE_DESCRIPTORS,        // 2: "Num_Particle_Descriptors"
	PST_PARTICLE_DESCRIPTOR_TYPE,        // 3: "Particle_Descriptor_Type"
	PST_PARTICLE_DESCRIPTOR_NAME,        // 4: "Particle_Descriptor_Name"
	PST_SEPARATOR,                       // 5: "*****************************************************************"
	PST_RADIUS,                          // 6: "Radius"
	PST_FINAL_RADIUS,                    // 7: "Final_Radius"
	PST_GRAVITY,                         // 8: "Gravity"
	PST_BOUNCE,                          // 9: "Bounce"
	PST_FADE_COL,                        // 10: "Fade_Col"
	PST_TEXTURE,                         // 11: "Texture"
	PST_MODIFIER,                        // 12: "Modifier"
	PST_BLEND_MODE,                      // 13: "Blend_Mode"
	PST_TEXTURE_NUMBER,                  // 14: "Texture_Number"
	PST_AXIS_ALIGNED,                    // 15: "Axis_Aligned"
	PST_COLOUR_RANGE,                    // 16: "Colour_Range"
	PST_ANIM_TYPE,                       // 17: "Anim_Type"
	PST_END_FRAME,                       // 18: "End_Frame"
	PST_ANIM_SPEED,                      // 19: "Anim_Speed"
	PST_TEXTURE_SIZE,                    // 20: "Texture_Size"
	PST_VELOCITY_DAMP,                   // 21: "Velocity_Damp"
	PST_LIFE_PCT,                        // 22: "Life_Pct"
	PST_RANDOM_START_FRAME,              // 23: "Random_Start_Frame"
	PST_LENGTH,                          // 24: "Length"
	PST_2D,                              // 25: "2D"
	PST_EMIT_PER_TURN,                   // 26: "Emit_Per_Turn"
	PST_LIFE,                            // 27: "Life"
	PST_PARTICLE_DESCRIPTOR,             // 28: "Particle_Descriptor"
	PST_INITIAL_VELOCITY_X,              // 29: "Initial_Velocity_X"
	PST_INITIAL_VELOCITY_Y,              // 30: "Initial_Velocity_Y"
	PST_INITIAL_VELOCITY_Z,              // 31: "Initial_Velocity_Z"
	PST_VELOCITY_RANDOMNESS,             // 32: "Velocity_Randomness"
	PST_TRANSMIT_LIFE,                   // 33: "Transmit_Life"
	PST_TRANSMIT_FOR,                    // 34: "Transmit_FoR"
	PST_TRANSMIT_VELOCITY,               // 35: "Transmit_Velocity"
	PST_INTERPOLATED_EMISSION,           // 36: "Interpolated_Emission"
	PST_SHAPE,                           // 37: "Shape"
	PST_PASS_NUM_PARTICLES,              // 38: "Pass_Num_Particles"
	PST_OUTWARD_VELOCITY,                // 39: "Outward_Velocity"
	PST_MOVER,                           // 40: "Mover"
	PST_PARTICLE_DESCRIPTOR_0,           // 41: "Particle_Descriptor_0"
	PST_PARTICLE_DESCRIPTOR_1,           // 42: "Particle_Descriptor_1"
	PST_PARTICLE_DESCRIPTOR_2,           // 43: "Particle_Descriptor_2"
	PST_PARTICLE_DESCRIPTOR_3,           // 44: "Particle_Descriptor_3"
	PST_PROBABILITY_0,                   // 45: "Probability_0"
	PST_PROBABILITY_1,                   // 46: "Probability_1"
	PST_PROBABILITY_2,                   // 47: "Probability_2"
	PST_PROBABILITY_3,                   // 48: "Probability_3"
	PST_START_RED,                       // 49: "Start_Red"
	PST_START_GREEN,                     // 50: "Start_Green"
	PST_START_BLUE,                      // 51: "Start_Blue"
	PST_END_RED,                         // 52: "End_Red"
	PST_END_GREEN,                       // 53: "End_Green"
	PST_END_BLUE,                        // 54: "End_Blue"
	PST_TRANSITION_RED,                  // 55: "Transition_Red"
	PST_TRANSITION_GREEN,                // 56: "Transition_Green"
	PST_TRANSITION_BLUE,                 // 57: "Transition_Blue"
	PST_USE_END,                         // 58: "Use_End"
	PST_USE_TRANSITION,                  // 59: "Use_Transition"
	PST_TRANSITION_POINT,                // 60: "Transition_Point"
	PST_NUM_ENTRIES,                     // 61: "Num_Entries"
	PST_TIME,                            // 62: "Time"
	PST_TYPE,                            // 63: "Type"
	PST_RING_AXIS,                       // 64: "Ring_Axis"
	PST_HEMISPHERE,                      // 65: "Hemisphere"
	PST_NUM_PARTICLES,                   // 66: "Num_Particles"
	PST_HOLLOW,                          // 67: "Hollow"
	PST_RANDOMSX,                        // 68: "RandomSX"
	PST_RANDOMSY,                        // 69: "RandomSY"
	PST_RANDOMSZ,                        // 70: "RandomSZ"
	PST_WIDTH,                           // 71: "Width"
	PST_NUM_POINTS,                      // 72: "Num_Points"
	PST_START_WIDTH,                     // 73: "Start_Width"
	PST_WIGGLE_FACTOR,                   // 74: "Wiggle_Factor"
	PST_DISPERSE_RATE,                   // 75: "Disperse_Rate"
	PST_TAPER_START,                     // 76: "Taper_Start"
	PST_WIDTH_WITH_LIFE,                 // 77: "Width_With_Life"
	PST_FADE_POINT,                      // 78: "Fade_Point"
	PST_USE_SEGMENT_LENGTH,              // 79: "Use_Segment_Length"
	PST_SEGMENTLENGTH,                   // 80: "SegmentLength"
	PST_MANUAL_WIGGLE_ENABLED,           // 81: "Manual_Wiggle_Enabled"
	PST_WIGGLE_LENGTH,                   // 82: "Wiggle_Length"
	PST_YAW_LENGTH,                      // 83: "Yaw_Length"
	PST_FLAT,                            // 84: "Flat"
	PST_GRAVITYPC,                       // 85: "GravityPC"
	PST_YAW,                             // 86: "Yaw"
	PST_PITCH,                           // 87: "Pitch"
	PST_ROLL,                            // 88: "Roll"
	PST_YAW_FUNCTION,                    // 89: "Yaw_Function"
	PST_PITCH_FUNCTION,                  // 90: "Pitch_Function"
	PST_ROLL_FUNCTION,                   // 91: "Roll_Function"
	PST_PARAM_A,                         // 92: "Param_A"
	PST_PARAM_B,                         // 93: "Param_B"
	PST_PARAM_C,                         // 94: "Param_C"
	PST_PARAM_D,                         // 95: "Param_D"
	PST_PARAM_FUNCTION,                  // 96: "Param_Function"
	PST_CLIP,                            // 97: "Clip"
	PST_VALUE_TYPE,                      // 98: "Value_Type"
	PST_GAMETURN_SCALE,                  // 99: "Gameturn_Scale"
	PST_OFFSET_GAMETURN,                 // 100: "Offset_Gameturn"
	PST_MESH,                            // 101: "Mesh"
	PST_ANGULAR_MOMENTUM,                // 102: "Angular_Momentum"
	PST_IMPACT_SPAWNEE,                  // 103: "Impact_Spawnee"
	PST_AUTO_CENTRE,                     // 104: "Auto_Centre"
	PST_INITIAL,                         // 105: "Initial"
	PST_DEATH,                           // 106: "Death"
	PST_TILE_U,                          // 107: "Tile_U"
	PST_TILE_V,                          // 108: "Tile_V"
	PST_SCROLL_U,                        // 109: "Scroll_U"
	PST_SCROLL_V,                        // 110: "Scroll_V"
	PST_CYLINDER_NUMPTSAXIAL,            // 111: "Cylinder_NumPtsAxial"
	PST_CYLINDER_NUMPTSRADIAL,           // 112: "Cylinder_NumPtsRadial"
	PST_CYLINDER_RADIUS,                 // 113: "Cylinder_Radius"
	PST_CYLINDER_RADIUS2,                // 114: "Cylinder_Radius2"
	PST_CYLINDER_LENGTH,                 // 115: "Cylinder_Length"
	PST_COLOUR_RANGE2,                   // 116: "Colour_Range2"
	PST_SPHERE_NUMPTSAX,                 // 117: "Sphere_NumPtsAx"
	PST_SPHERE_NUMPTSRAD,                // 118: "Sphere_NumPtsRad"
	PST_SPHERE_RADIUSTIME,               // 119: "Sphere_RadiusTime"
	PST_SPHERE_LATITUDE_START,           // 120: "Sphere_Latitude_Start"
	PST_SPHERE_LATITUDE_END,             // 121: "Sphere_Latitude_End"
	PST_SPHERE_LONGITUDE_START,          // 122: "Sphere_Longitude_Start"
	PST_SPHERE_LONGITUDE_END,            // 123: "Sphere_Longitude_End"
	PST_NUM_TOKENS
};


// A float value with an optional reference to a descriptor by name ("Radius 0.5 Smoke Puff").
class CFloatRef
{
public:
	CFloatRef() { mValue = 0; mRef = NULL; }
	CFloatRef	&operator=(float v) { mValue = v; mRef = NULL; return *this; }
	float	GetValue(float x, class CParticle *p);	// 004c10c0 (ParticleDescriptor.cpp): the value, or the function's at x times it
	float	GetValueByAge(float left, float life, class CParticle *p);	// inline (ParticleDescriptor.h): at (life - left) / life (name ours)

	float					mValue;			// +0x00
	CParticleDescriptor		*mRef;			// +0x04
};

#define MAX_TOKEN_FIXUPS	10000

// Reads the archive line by line; references to descriptors by name are collected as fixups and resolved once the
// whole set is loaded (CParticleSet::LoadFromArchive 004cd7f0 allocates one, 0x1388c bytes).
class CTokenArchive
{
public:
	CTokenArchive(CDXMemBuffer *file) { mFile = file; mUnknown04 = 0; mNumFixups = 0; }

	static char	*GetTokenName(SINT token);				// 004f52b0
	BOOL		ReadToken(SINT *token, SINT *ival, float *fval, char *sval);	// 004f57b0: FALSE at a line it can't use
	void		SetFixup(SINT index, CParticleDescriptor **ref);				// 004f5b70
	void		SetFloatRef(float value, SINT index, CFloatRef *ref);			// 004f5b80
	void		HandleRelocation(CParticleDescriptor **list);					// 004f5ba0 (its name is in CParticleSet::ReadData's trace)

	// The editor's writers; the game build formats each line and writes nothing.
	void		WriteInt(SINT token, SINT value);						// 004f5c90
	void		WriteFloat(SINT token, float value);					// 004f5cd0
	void		WriteString(SINT token, char *value);					// 004f5d10
	void		WriteReference(SINT token, CParticleDescriptor *ref);	// 004f5d50
	void		WriteFloatRef(SINT token, CFloatRef *ref);				// 004f5dc0

	CDXMemBuffer			*mFile;							// +0x00
	SINT					mUnknown04;						// +0x04
	SINT					mNumFixups;						// +0x08
	CParticleDescriptor		**mFixups[MAX_TOKEN_FIXUPS];	// +0x0c
	char					*mNames[MAX_TOKEN_FIXUPS];		// +0x9c4c
};

#endif
