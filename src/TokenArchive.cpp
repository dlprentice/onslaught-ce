// TokenArchive.cpp: reconstructed from TokenArchive.obj (004f51c0..004f5e3f); Stuart's release has no copy.
// The allocations' __LINE__ values are the retail ones.
#include "common.h"
#include "TokenArchive.h"	// FUNCTION: 0x004f51c0 init:ZERO_FVECTOR
#include "mathsconst.h"		// FUNCTION: 0x004f51e0 init:ID_FMATRIX
#include "membuffer.h"
#include "DXMemBuffer.h"
#include "ParticleDescriptor.h"

static char	sLine[1000];
static char	sToken[1000];
static char	sValue[1000];

//******************************************************************************************
char *CTokenArchive::GetTokenName(SINT token)	// FUNCTION: 0x004f52b0
{
	switch (token)
	{
	case PST_FILE_HEADER:                   return "ParticleSystemEd_File_(C)2000_Lost_Toys_Ltd";
	case PST_FILE_VERSION:                  return "File_Version";
	case PST_NUM_PARTICLE_DESCRIPTORS:      return "Num_Particle_Descriptors";
	case PST_PARTICLE_DESCRIPTOR_TYPE:      return "Particle_Descriptor_Type";
	case PST_PARTICLE_DESCRIPTOR_NAME:      return "Particle_Descriptor_Name";
	case PST_SEPARATOR:                     return "*****************************************************************";
	case PST_RADIUS:                        return "Radius";
	case PST_FINAL_RADIUS:                  return "Final_Radius";
	case PST_GRAVITY:                       return "Gravity";
	case PST_BOUNCE:                        return "Bounce";
	case PST_FADE_COL:                      return "Fade_Col";
	case PST_TEXTURE:                       return "Texture";
	case PST_MODIFIER:                      return "Modifier";
	case PST_BLEND_MODE:                    return "Blend_Mode";
	case PST_TEXTURE_NUMBER:                return "Texture_Number";
	case PST_TEXTURE_SIZE:                  return "Texture_Size";
	case PST_AXIS_ALIGNED:                  return "Axis_Aligned";
	case PST_COLOUR_RANGE:                  return "Colour_Range";
	case PST_ANIM_TYPE:                     return "Anim_Type";
	case PST_END_FRAME:                     return "End_Frame";
	case PST_ANIM_SPEED:                    return "Anim_Speed";
	case PST_VELOCITY_DAMP:                 return "Velocity_Damp";
	case PST_LIFE_PCT:                      return "Life_Pct";
	case PST_RANDOM_START_FRAME:            return "Random_Start_Frame";
	case PST_LENGTH:                        return "Length";
	case PST_2D:                            return "2D";
	case PST_EMIT_PER_TURN:                 return "Emit_Per_Turn";
	case PST_LIFE:                          return "Life";
	case PST_PARTICLE_DESCRIPTOR:           return "Particle_Descriptor";
	case PST_INITIAL_VELOCITY_X:            return "Initial_Velocity_X";
	case PST_INITIAL_VELOCITY_Y:            return "Initial_Velocity_Y";
	case PST_INITIAL_VELOCITY_Z:            return "Initial_Velocity_Z";
	case PST_VELOCITY_RANDOMNESS:           return "Velocity_Randomness";
	case PST_TRANSMIT_LIFE:                 return "Transmit_Life";
	case PST_TRANSMIT_FOR:                  return "Transmit_FoR";
	case PST_TRANSMIT_VELOCITY:             return "Transmit_Velocity";
	case PST_INTERPOLATED_EMISSION:         return "Interpolated_Emission";
	case PST_SHAPE:                         return "Shape";
	case PST_PASS_NUM_PARTICLES:            return "Pass_Num_Particles";
	case PST_OUTWARD_VELOCITY:              return "Outward_Velocity";
	case PST_MOVER:                         return "Mover";
	case PST_PARTICLE_DESCRIPTOR_0:         return "Particle_Descriptor_0";
	case PST_PARTICLE_DESCRIPTOR_1:         return "Particle_Descriptor_1";
	case PST_PARTICLE_DESCRIPTOR_2:         return "Particle_Descriptor_2";
	case PST_PARTICLE_DESCRIPTOR_3:         return "Particle_Descriptor_3";
	case PST_PROBABILITY_0:                 return "Probability_0";
	case PST_PROBABILITY_1:                 return "Probability_1";
	case PST_PROBABILITY_2:                 return "Probability_2";
	case PST_PROBABILITY_3:                 return "Probability_3";
	case PST_START_RED:                     return "Start_Red";
	case PST_START_GREEN:                   return "Start_Green";
	case PST_START_BLUE:                    return "Start_Blue";
	case PST_END_RED:                       return "End_Red";
	case PST_END_GREEN:                     return "End_Green";
	case PST_END_BLUE:                      return "End_Blue";
	case PST_TRANSITION_RED:                return "Transition_Red";
	case PST_TRANSITION_GREEN:              return "Transition_Green";
	case PST_TRANSITION_BLUE:               return "Transition_Blue";
	case PST_USE_END:                       return "Use_End";
	case PST_USE_TRANSITION:                return "Use_Transition";
	case PST_TRANSITION_POINT:              return "Transition_Point";
	case PST_NUM_ENTRIES:                   return "Num_Entries";
	case PST_TIME:                          return "Time";
	case PST_TYPE:                          return "Type";
	case PST_RING_AXIS:                     return "Ring_Axis";
	case PST_HEMISPHERE:                    return "Hemisphere";
	case PST_NUM_PARTICLES:                 return "Num_Particles";
	case PST_HOLLOW:                        return "Hollow";
	case PST_RANDOMSX:                      return "RandomSX";
	case PST_RANDOMSY:                      return "RandomSY";
	case PST_RANDOMSZ:                      return "RandomSZ";
	case PST_WIDTH:                         return "Width";
	case PST_START_WIDTH:                   return "Start_Width";
	case PST_NUM_POINTS:                    return "Num_Points";
	case PST_WIGGLE_FACTOR:                 return "Wiggle_Factor";
	case PST_DISPERSE_RATE:                 return "Disperse_Rate";
	case PST_TAPER_START:                   return "Taper_Start";
	case PST_WIDTH_WITH_LIFE:               return "Width_With_Life";
	case PST_FADE_POINT:                    return "Fade_Point";
	case PST_USE_SEGMENT_LENGTH:            return "Use_Segment_Length";
	case PST_SEGMENTLENGTH:                 return "SegmentLength";
	case PST_MANUAL_WIGGLE_ENABLED:         return "Manual_Wiggle_Enabled";
	case PST_WIGGLE_LENGTH:                 return "Wiggle_Length";
	case PST_YAW_LENGTH:                    return "Yaw_Length";
	case PST_FLAT:                          return "Flat";
	case PST_GRAVITYPC:                     return "GravityPC";
	case PST_YAW:                           return "Yaw";
	case PST_PITCH:                         return "Pitch";
	case PST_ROLL:                          return "Roll";
	case PST_YAW_FUNCTION:                  return "Yaw_Function";
	case PST_PITCH_FUNCTION:                return "Pitch_Function";
	case PST_ROLL_FUNCTION:                 return "Roll_Function";
	case PST_PARAM_A:                       return "Param_A";
	case PST_PARAM_B:                       return "Param_B";
	case PST_PARAM_C:                       return "Param_C";
	case PST_PARAM_D:                       return "Param_D";
	case PST_PARAM_FUNCTION:                return "Param_Function";
	case PST_CLIP:                          return "Clip";
	case PST_VALUE_TYPE:                    return "Value_Type";
	case PST_GAMETURN_SCALE:                return "Gameturn_Scale";
	case PST_OFFSET_GAMETURN:               return "Offset_Gameturn";
	case PST_MESH:                          return "Mesh";
	case PST_ANGULAR_MOMENTUM:              return "Angular_Momentum";
	case PST_IMPACT_SPAWNEE:                return "Impact_Spawnee";
	case PST_AUTO_CENTRE:                   return "Auto_Centre";
	case PST_INITIAL:                       return "Initial";
	case PST_DEATH:                         return "Death";
	case PST_TILE_U:                        return "Tile_U";
	case PST_TILE_V:                        return "Tile_V";
	case PST_SCROLL_U:                      return "Scroll_U";
	case PST_SCROLL_V:                      return "Scroll_V";
	case PST_CYLINDER_NUMPTSAXIAL:          return "Cylinder_NumPtsAxial";
	case PST_CYLINDER_NUMPTSRADIAL:         return "Cylinder_NumPtsRadial";
	case PST_CYLINDER_RADIUS:               return "Cylinder_Radius";
	case PST_CYLINDER_RADIUS2:              return "Cylinder_Radius2";
	case PST_CYLINDER_LENGTH:               return "Cylinder_Length";
	case PST_COLOUR_RANGE2:                 return "Colour_Range2";
	case PST_SPHERE_NUMPTSAX:               return "Sphere_NumPtsAx";
	case PST_SPHERE_NUMPTSRAD:              return "Sphere_NumPtsRad";
	case PST_SPHERE_RADIUSTIME:             return "Sphere_RadiusTime";
	case PST_SPHERE_LATITUDE_START:         return "Sphere_Latitude_Start";
	case PST_SPHERE_LATITUDE_END:           return "Sphere_Latitude_End";
	case PST_SPHERE_LONGITUDE_START:        return "Sphere_Longitude_Start";
	case PST_SPHERE_LONGITUDE_END:          return "Sphere_Longitude_End";
	}
	return "***Unknown_Token***";
}

// The token with that name, or PST_NONE.
static inline SINT FindToken(char *name)
{
	for (SINT t = 0; t < PST_NUM_TOKENS; t++)
		if (strcmp(name, CTokenArchive::GetTokenName(t)) == 0)
			return t;
	return PST_NONE;
}

//******************************************************************************************
// Retail calls sscanf once for the int and float cases: the float case jumps into the int case's `push sValue; call
// sscanf` (004f58ac -> 004f5871), and the return-TRUE block that FILE_HEADER's table entry names (004f587e) follows the
// int case. The float-reference case's else jumps into the name-fixup case's strcpy (004f5ab6 -> 004f597e) and its
// range test into that case's count update (004f5a64 -> 004f59a3). VC6's cross-jumping (C2 0x1073d211) walks the exit
// label's reference list from its head: the first plain jump is the anchor, and each later jump with the same tail
// moves into it (after one merge of more than 20 bytes, any length). The list is built by prepending each jump as it is
// made (0x107038d5), and the IL reader makes the returns' jumps in source order, so the last return made anchors.
// STAND-IN (cross-jump order): the int and name-fixup cases return through `goto ok` and
// `goto nf` to returns after the switch, which makes their jumps to the exit the newest two: int's heads the list and
// name-fixup's comes before the float-reference case's. Patched-C2 permutations of the exit list measured the rule: int
// first and name-fixup directly before the float-reference case give 0/0/0 ([int, name-fixup, float-ref, string,
// float, header], [int, float, name-fixup, float-ref, string, header], [int, header, name-fixup, float-ref, string,
// float], [int, string, name-fixup, float-ref, float, header]); int alone first gives 112/112/525, and plain returns
// 123/123/714 (the float-reference case's copy anchors everything). Reversing every cross-jump list in C2 also matches
// this function but loses 78 others, so the order comes from the source; the late returns emit the plain returns'
// code. Break or return FALSE forms, a result variable, guard-first tests, default or case order, an inline int helper,
// one shared `ok` for both cases and the int case falling into FILE_HEADER leave the order wrong.
BOOL CTokenArchive::ReadToken(SINT *token, SINT *ival, float *fval, char *sval)	// FUNCTION: 0x004f57b0
{
	mFile->ReadLine(sLine, 999);
	SINT	read = sscanf(sLine, "%s %s", sToken, sValue);

	*token = FindToken(sToken);

	switch (*token)
	{
	case PST_NONE:
		return FALSE;

	case PST_NUM_PARTICLE_DESCRIPTORS:
	case PST_PARTICLE_DESCRIPTOR_TYPE:
	case PST_GRAVITY:
	case PST_BOUNCE:
	case PST_FADE_COL:
	case PST_BLEND_MODE:
	case PST_TEXTURE_NUMBER:
	case PST_AXIS_ALIGNED:
	case PST_ANIM_TYPE:
	case PST_END_FRAME:
	case PST_TEXTURE_SIZE:
	case PST_RANDOM_START_FRAME:
	case PST_2D:
	case PST_LIFE:
	case PST_TRANSMIT_LIFE:
	case PST_TRANSMIT_FOR:
	case PST_INTERPOLATED_EMISSION:
	case PST_PASS_NUM_PARTICLES:
	case PST_PROBABILITY_0:
	case PST_PROBABILITY_1:
	case PST_PROBABILITY_2:
	case PST_PROBABILITY_3:
	case PST_USE_END:
	case PST_USE_TRANSITION:
	case PST_NUM_ENTRIES:
	case PST_TIME:
	case PST_TYPE:
	case PST_RING_AXIS:
	case PST_HEMISPHERE:
	case PST_NUM_PARTICLES:
	case PST_HOLLOW:
	case PST_NUM_POINTS:
	case PST_TAPER_START:
	case PST_WIDTH_WITH_LIFE:
	case PST_FADE_POINT:
	case PST_USE_SEGMENT_LENGTH:
	case PST_MANUAL_WIGGLE_ENABLED:
	case PST_FLAT:
	case PST_PARAM_FUNCTION:
	case PST_CLIP:
	case PST_VALUE_TYPE:
	case PST_OFFSET_GAMETURN:
	case PST_AUTO_CENTRE:
	case PST_CYLINDER_NUMPTSAXIAL:
	case PST_CYLINDER_NUMPTSRADIAL:
	case PST_SPHERE_NUMPTSAX:
	case PST_SPHERE_NUMPTSRAD:
		if (ival && read >= 2)
		{
			sscanf(sValue, "%i", ival);
			goto ok;
		}
		break;

	case PST_FILE_HEADER:
	case PST_SEPARATOR:
		return TRUE;

	case PST_FILE_VERSION:
	case PST_FINAL_RADIUS:
	case PST_ANIM_SPEED:
	case PST_VELOCITY_DAMP:
	case PST_LIFE_PCT:
	case PST_VELOCITY_RANDOMNESS:
	case PST_TRANSITION_POINT:
	case PST_RANDOMSX:
	case PST_RANDOMSY:
	case PST_RANDOMSZ:
	case PST_WIDTH:
	case PST_START_WIDTH:
	case PST_WIGGLE_FACTOR:
	case PST_DISPERSE_RATE:
	case PST_SEGMENTLENGTH:
	case PST_YAW:
	case PST_PITCH:
	case PST_ROLL:
	case PST_ANGULAR_MOMENTUM:
		if (fval && read >= 2)
		{
			sscanf(sValue, "%f", fval);
			return TRUE;
		}
		break;

	case PST_PARTICLE_DESCRIPTOR_NAME:
	case PST_TEXTURE:
	case PST_MESH:
		if (sval && read >= 2)
		{
			strcpy(sval, sLine + strlen(sToken) + 1);
			return TRUE;
		}
		break;

	case PST_MODIFIER:
	case PST_COLOUR_RANGE:
	case PST_PARTICLE_DESCRIPTOR:
	case PST_SHAPE:
	case PST_MOVER:
	case PST_PARTICLE_DESCRIPTOR_0:
	case PST_PARTICLE_DESCRIPTOR_1:
	case PST_PARTICLE_DESCRIPTOR_2:
	case PST_PARTICLE_DESCRIPTOR_3:
	case PST_YAW_FUNCTION:
	case PST_PITCH_FUNCTION:
	case PST_ROLL_FUNCTION:
	case PST_IMPACT_SPAWNEE:
	case PST_INITIAL:
	case PST_DEATH:
	case PST_COLOUR_RANGE2:
		if (sval && read >= 2)
		{
			*ival = mNumFixups;
#line 312
			mNames[mNumFixups] = new(MEMTYPE_UNKNOWN) char[strlen(sLine + strlen(sToken) + 1) + 2];
			strcpy(mNames[mNumFixups], sLine + strlen(sToken) + 1);
			mNumFixups++;
			goto nf;
		}
		break;

	case PST_RADIUS:
	case PST_LENGTH:
	case PST_EMIT_PER_TURN:
	case PST_INITIAL_VELOCITY_X:
	case PST_INITIAL_VELOCITY_Y:
	case PST_INITIAL_VELOCITY_Z:
	case PST_TRANSMIT_VELOCITY:
	case PST_OUTWARD_VELOCITY:
	case PST_START_RED:
	case PST_START_GREEN:
	case PST_START_BLUE:
	case PST_END_RED:
	case PST_END_GREEN:
	case PST_END_BLUE:
	case PST_TRANSITION_RED:
	case PST_TRANSITION_GREEN:
	case PST_TRANSITION_BLUE:
	case PST_WIGGLE_LENGTH:
	case PST_YAW_LENGTH:
	case PST_GRAVITYPC:
	case PST_PARAM_A:
	case PST_PARAM_B:
	case PST_PARAM_C:
	case PST_PARAM_D:
	case PST_GAMETURN_SCALE:
	case PST_TILE_U:
	case PST_TILE_V:
	case PST_SCROLL_U:
	case PST_SCROLL_V:
	case PST_CYLINDER_RADIUS:
	case PST_CYLINDER_RADIUS2:
	case PST_CYLINDER_LENGTH:
	case PST_SPHERE_RADIUSTIME:
	case PST_SPHERE_LATITUDE_START:
	case PST_SPHERE_LATITUDE_END:
	case PST_SPHERE_LONGITUDE_START:
	case PST_SPHERE_LONGITUDE_END:
		sscanf(sValue, "%f", fval);
		*ival = mNumFixups;
#line 362
		mNames[mNumFixups] = new(MEMTYPE_UNKNOWN) char[strlen(sLine + strlen(sToken) + strlen(sValue) + 2) + 2];
		if (sLine[strlen(sToken) + strlen(sValue) + 1] == 0)
		{
			mNames[mNumFixups][0] = 0;
			if (*token >= PST_START_RED && *token <= PST_TRANSITION_BLUE)
				*fval /= 255.0f;		// a colour without a reference is read as 0..255
		}
		else
			strcpy(mNames[mNumFixups], sLine + strlen(sToken) + strlen(sValue) + 2);
		mNumFixups++;
		return TRUE;
	}
	return FALSE;
nf:
	return TRUE;
ok:
	return TRUE;
}

//******************************************************************************************
void CTokenArchive::SetFixup(SINT index, CParticleDescriptor **ref)	// FUNCTION: 0x004f5b70
{
	mFixups[index] = ref;
}

//******************************************************************************************
void CTokenArchive::SetFloatRef(float value, SINT index, CFloatRef *ref)	// FUNCTION: 0x004f5b80
{
	ref->mValue = value;
	mFixups[index] = &ref->mRef;
}

//******************************************************************************************
static int CompareDescriptorName(const void *name, const void *pd)	// FUNCTION: 0x004f5c70
{
	CParticleDescriptor	*d = *(CParticleDescriptor **)pd;
	return stricmp((char *)name, d->mName);
}

//******************************************************************************************
void CTokenArchive::HandleRelocation(CParticleDescriptor **list)	// FUNCTION: 0x004f5ba0
{
	SINT	count = 0;
	CParticleDescriptor	*pd;
	for (pd = *list; pd; pd = pd->mNext)
		count++;

#line 450
	CParticleDescriptor	**sorted = new(MEMTYPE_SCRATCHPAD) CParticleDescriptor *[count];
	SINT	n = 0;
	for (pd = *list; pd; pd = pd->mNext)
		sorted[n++] = pd;

	for (SINT i = 0; i < mNumFixups; i++)
	{
		CParticleDescriptor	**found = (CParticleDescriptor **)bsearch(mNames[i], sorted, n, 4, CompareDescriptorName);
		if (found)
			*mFixups[i] = *found;
		else
			*mFixups[i] = NULL;
		delete [] mNames[i];
	}
	delete [] sorted;
	mNumFixups = 0;
}

//******************************************************************************************
void CTokenArchive::WriteInt(SINT token, SINT value)	// FUNCTION: 0x004f5c90
{
	char	line[400];
	sprintf(line, "%s %d\r\n", GetTokenName(token), value);
}

//******************************************************************************************
void CTokenArchive::WriteFloat(SINT token, float value)	// FUNCTION: 0x004f5cd0
{
	char	line[400];
	sprintf(line, "%s %f\r\n", GetTokenName(token), value);
}

//******************************************************************************************
void CTokenArchive::WriteString(SINT token, char *value)	// FUNCTION: 0x004f5d10
{
	char	line[400];
	sprintf(line, "%s %s\r\n", GetTokenName(token), value);
}

//******************************************************************************************
void CTokenArchive::WriteReference(SINT token, CParticleDescriptor *ref)	// FUNCTION: 0x004f5d50
{
	char	line[400];
	if (ref)
		sprintf(line, "%s %s\r\n", GetTokenName(token), ref->mName);
	else
		sprintf(line, "%s NONE\r\n", GetTokenName(token));
}

//******************************************************************************************
void CTokenArchive::WriteFloatRef(SINT token, CFloatRef *ref)	// FUNCTION: 0x004f5dc0
{
	char	line[400];
	if (ref->mRef)
		sprintf(line, "%s %f %s\r\n", GetTokenName(token), ref->mValue, ref->mRef->mName);
	else
		sprintf(line, "%s %f NONE\r\n", GetTokenName(token), ref->mValue);
}
// STAND-IN (link layout): retail's 8200 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_TokenArchive[8200] = { 0 };
