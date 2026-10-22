// FrontEndData.cpp: the front end's shared data, FED (reconstructed from the retail PC executable; not in Stuart's
// release): the camera, 86 textures and 6 meshes, and the particle effects the pages use.

#include	"Common.h"

#include	"Frontend.h"
#include	"CLIParams.h"
#include	"resourceaccumulator.h"
#include	"ParticleManager.h"
#include	"mouse.h"


//*********************************************************************************
BOOL	CFrontEndData::Init()	// FUNCTION: 0x004687e0
{
	if (!CLIPARAMS.mBuildResources)
		CResourceAccumulator::ReadResources(-2);

	CTEXTURE::LoadLevelResources(-2);
	CMESH::LoadLevelResources(-2);

	mCamera.mPos = FVector(0.f, -20.f, 0.f);
	mCamera.mOrientation = ID_FMATRIX;
	mCamera.mZoom = 1.f;

	PARTICLE_MANAGER.mSystem = NULL;
	PARTICLE_MANAGER.mFreeParticles = NULL;
	PARTICLE_MANAGER.mNumLiveParticles = 0;
	PARTICLE_MANAGER.mTime = 0;

	if (!PARTICLE_SET.LoadAllFromDisk(PARTSET_FRONTEND))
		return FALSE;

	int		i;

	for (i = 0; i < FET_NUM; i++)
		mTextures[i] = NULL;

	TRACE("Loading frontend textures:\n");

	mTextures[FET_LEVEL_LINK] = CTEXTURE::FindTexture("FrontEnd\\LevelLink.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BARL] = CTEXTURE::FindTexture("FrontEnd\\BarL.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BARC] = CTEXTURE::FindTexture("FrontEnd\\BarC.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BARR] = CTEXTURE::FindTexture("FrontEnd\\BarR.tga", 4, 0, 1, 0, 1);
	mTextures[FET_GLARE_ALPHA] = CTEXTURE::FindTexture("FrontEnd\\GlareAlpha.tga", 4, 0, 1, 0, 1);
	mTextures[FET_LOST_TOYS] = CTEXTURE::FindTexture("FrontEnd\\LostToys.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_S] = CTEXTURE::FindTexture("FrontEnd\\RankingS.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_A] = CTEXTURE::FindTexture("FrontEnd\\RankingA.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_B] = CTEXTURE::FindTexture("FrontEnd\\RankingB.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_C] = CTEXTURE::FindTexture("FrontEnd\\RankingC.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_D] = CTEXTURE::FindTexture("FrontEnd\\RankingD.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_E] = CTEXTURE::FindTexture("FrontEnd\\RankingE.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_SMASK] = CTEXTURE::FindTexture("FrontEnd\\RankingSMask.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_AMASK] = CTEXTURE::FindTexture("FrontEnd\\RankingAMask.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_BMASK] = CTEXTURE::FindTexture("FrontEnd\\RankingBMask.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_CMASK] = CTEXTURE::FindTexture("FrontEnd\\RankingCMask.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_DMASK] = CTEXTURE::FindTexture("FrontEnd\\RankingDMask.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_EMASK] = CTEXTURE::FindTexture("FrontEnd\\RankingEMask.tga", 4, 0, 1, 0, 1);
	mTextures[FET_FORSETI_TEXT] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Forseti_Writing_large.tga", 4, 0, 1, 0, 1);
	mTextures[FET_FORSETI_WRITING_SMALL] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Forseti_Writing_small.tga", 4, 0, 1, 0, 1);
	int		oldmode = LT.m_bTextureCompressionDisabled;
	LT.m_bTextureCompressionDisabled = 1;
	mTextures[FET_WHITE_RING] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_white_ring.tga", 4, 0, 1, 0, 1);
	LT.m_bTextureCompressionDisabled = oldmode;
	mTextures[FET_LOGO_SHINE] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Reflection_map.tga", 4, 0, 1, 0, 1);
	mTextures[FET_SELECT_LEVEL_RING_BRACKET01] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_select_level_ring_bracket01.tga", 4, 0, 1, 0, 1);
	mTextures[FET_SELECT_LEVEL_RING_BRACKET02] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_select_level_ring_bracket02.tga", 4, 0, 1, 0, 1);
	mTextures[FET_LEVEL_SELECT_MASK] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_level_select_mask.tga", 4, 0, 1, 0, 1);
	mTextures[FET_METAL_RING_TRANS_FROM_LEVSEL1] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_metal_ring_trans_from_levsel1.tga", 4, 0, 1, 0, 1);
	mTextures[FET_METAL_RING_TRANS_FROM_LEVSEL2] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_metal_ring_trans_from_levsel2.tga", 4, 0, 1, 0, 1);
	mTextures[FET_LEVEL_SELECT_GLOW_BLUE] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_level_select_glow_blue.tga", 4, 0, 1, 0, 1);
	mTextures[FET_LEVEL_SELECT_GLOW_ORANGE] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_level_select_glow_orange.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_SMALL_MASK_A] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Ranking_Small_Mask_A.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_SMALL_MASK_B] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Ranking_Small_Mask_B.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_SMALL_MASK_C] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Ranking_Small_Mask_C.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_SMALL_MASK_D] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Ranking_Small_Mask_D.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_SMALL_MASK_E] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Ranking_Small_Mask_E.tga", 4, 0, 1, 0, 1);
	mTextures[FET_RANKING_SMALL_MASK_S] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Ranking_Small_Mask_S.tga", 4, 0, 1, 0, 1);
	mTextures[FET2_WHITE] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_White.tga", 4, 0, 1, 0, 1);
	mTextures[FET_SELECTION_MARKER] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Rock_Background.tga", 4, 0, 1, 0, 1);
	mTextures[FET_SELECTION_MARKER]->mUnknown155 = TRUE;
	mTextures[FET_AVI_BOX] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_avi_box.tga", 4, 0, 1, 0, 1);
	mTextures[FET_HELP_ARROW] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Arrow.tga", 4, 0, 1, 0, 1);
	mTextures[FET_GOODIES1] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Goodies1.tga", 4, 0, 1, 0, 1);
	mTextures[FET_GOODIES2] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Goodies2.tga", 4, 0, 1, 0, 1);
	mTextures[FET_GOODIES3] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Goodies3.tga", 4, 0, 1, 0, 1);
	mTextures[FET_GOODIES4] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Goodies4.tga", 4, 0, 1, 0, 1);
	mTextures[FET_FLAG_UK] = CTEXTURE::FindTexture("FrontEnd\\v2\\Flag_UK.tga", 4, 0, 1, 0, 1);
	mTextures[FET_FLAG_FR] = CTEXTURE::FindTexture("FrontEnd\\v2\\Flag_FR.tga", 4, 0, 1, 0, 1);
	mTextures[FET_FLAG_GR] = CTEXTURE::FindTexture("FrontEnd\\v2\\Flag_GR.tga", 4, 0, 1, 0, 1);
	mTextures[FET_FLAG_SP] = CTEXTURE::FindTexture("FrontEnd\\v2\\Flag_SP.tga", 4, 0, 1, 0, 1);
	mTextures[FET_FLAG_IT] = CTEXTURE::FindTexture("FrontEnd\\v2\\Flag_IT.tga", 4, 0, 1, 0, 1);
	mTextures[FET_I_TROOP] = CTEXTURE::FindTexture("FrontEnd\\v2\\i_troop.tga", 4, 0, 1, 0, 1);
	mTextures[FET_I_TANK] = CTEXTURE::FindTexture("FrontEnd\\v2\\i_tank.tga", 4, 0, 1, 0, 1);
	mTextures[FET_I_PLANE] = CTEXTURE::FindTexture("FrontEnd\\v2\\i_plane.tga", 4, 0, 1, 0, 1);
	mTextures[FET_I_BILLY] = CTEXTURE::FindTexture("FrontEnd\\v2\\i_Billy.tga", 4, 0, 1, 0, 1);
	mTextures[FET_I_LORENZO] = CTEXTURE::FindTexture("FrontEnd\\v2\\i_Lorenzo.tga", 4, 0, 1, 0, 1);
	mTextures[FET_I_TARA] = CTEXTURE::FindTexture("FrontEnd\\v2\\i_Tara.tga", 4, 0, 1, 0, 1);
	mTextures[FET_FE_SPLASH1] = CTEXTURE::FindTexture("FrontEnd\\v2\\fe_splash1.tga", 4, 0, 1, 0, 1);
	mTextures[FET_FE_SPLASH1]->mUnknown155 = TRUE;
	mTextures[FET_FE_INFOGRAMES] = CTEXTURE::FindTexture("FrontEnd\\v2\\fe_infogrames.tga", 4, 0, 1, 0, 1);
	mTextures[FET_SELECT_LEVEL_EPISODE_CURVE] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_select_level_episode_curve.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE2] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_Title2.tga", 4, 0, 1, 0, 1);
	mTextures[FET_SMALL_BEA_LOGO] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_Gamelogo_small.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_BRACKET01] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_bracket01.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_BRACKET02] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_bracket02.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_TEXT_BOX] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_text_box.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_SYMBOL_BRACKET01] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_symbol_bracket01.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_SYMBOL_BRACKET02] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_symbol_bracket02.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_NAV_SYMBOL_NEWGAME01] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_nav_symbol_newgame01.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_NAV_SYMBOL_CONTINUEGAME01] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_nav_symbol_continuegame01.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_NAV_SYMBOL_SAVEGAME01] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_nav_symbol_savegame01.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_NAV_SYMBOL_LOADGAME01] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_nav_symbol_loadgame01.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_NAV_SYMBOL_MULTIPLAYER01] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_nav_symbol_multiplayer01.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_NAV_SYMBOL_GOODIES01] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_nav_symbol_goodies01.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_NAV_SYMBOL_OPTIONS01] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_nav_symbol_options01.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_NAV_SYMBOL_LANGUAGE01] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_nav_symbol_language01.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_TITLE_NAV_SYMBOL_QUITGAME01] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_title_nav_symbol_quitgame01.tga", 4, 0, 1, 0, 1);
	mTextures[FET3_HEADER_BRACKET1] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_header_bracket01.tga", 4, 0, 1, 0, 1);
	mTextures[FET3_HEADER_BRACKET2] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_header_bracket02.tga", 4, 0, 1, 0, 1);
	mTextures[FET_BEA_HEADER_BRACKET03] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_header_bracket03.tga", 4, 0, 1, 0, 1);
	mTextures[FET3_HEADER_TEXT_BOX] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_BEA_header_box.tga", 4, 0, 1, 0, 1);
	mTextures[FET3_SELECT_BRACKET1] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_select_level_bracket01.tga", 4, 0, 1, 0, 1);
	mTextures[FET3_SELECT_BRACKET2] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_select_level_bracket02.tga", 4, 0, 1, 0, 1);
	mTextures[FET3_SELECT_BRACKET_MASK] = CTEXTURE::FindTexture("FrontEnd\\v3\\FE_select_level_bracket_mask.tga", 4, 0, 1, 0, 1);
	mTextures[FET_SMALL_STAR] = CTEXTURE::FindTexture("FrontEnd\\v3\\SmallStar.tga", 4, 0, 1, 0, 1);
	mTextures[FET2_BLANK] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_Blank.tga", 4, 0, 1, 0, 1);
	mTextures[FET_VK_KEY] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_VK_Key.tga", 4, 0, 1, 0, 1);
	mTextures[FET_XB_XBOX] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_XB_Xbox.tga", 4, 0, 1, 0, 1);
	mTextures[FET_XB_MU] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_XB_MU.tga", 4, 0, 1, 0, 1);
	mTextures[FET_XB_MU_CORRUPT] = CTEXTURE::FindTexture("FrontEnd\\v2\\FE_XB_MU_Corrupt.tga", 4, 0, 1, 0, 1);

	TRACE("Loading meshes\n");

	for (i = 0; i < FEM_NUM; i++)
		mMeshes[i] = NULL;

	if (!PLAYABLE_DEMO)
	{
		mMeshes[FEM_BE_TRANS] = CMESH::GetMesh("be_trans.msh", 1);
		mMeshes[FEM_BE_TRANSM] = CMESH::GetMesh("be_transm.msh", 1);
		mMeshes[FEM_WILDCARD_T] = CMESH::GetMesh("fa_wildcard_T.msh", 1);
		mMeshes[FEM_WILDCARD_B] = CMESH::GetMesh("fa_wildcard_B.msh", 1);
		mMeshes[FEM_WILDCARD_J] = CMESH::GetMesh("fa_wildcard_J.msh", 1);
		mMeshes[FEM_PS2_LOGO] = CMESH::GetMesh("PS2_Normal_Logo3.msh", 1);
	}

	TRACE("Done frontenddata.\n");

	return TRUE;
}

//*********************************************************************************
void	CFrontEndData::Shutdown()	// FUNCTION: 0x004691c0
{
	PARTICLE_MANAGER.Shutdown();
	PARTICLE_SET.Shutdown();

	int		i;

	for (i = 0; i < FET_NUM; i++)
	{
		if (mTextures[i])
		{
			mTextures[i]->Release();
			mTextures[i] = NULL;
		}
	}

	for (i = 0; i < FEM_NUM; i++)
	{
		if (mMeshes[i])
		{
			mMeshes[i]->Release();
			mMeshes[i] = NULL;
		}
	}

	CMESH::FreeLevelResources();
	CVBufTexture::FreeLevelResources();
	CTEXTURE::FreeLevelResources();
}

// FUNCTION: 0x004692a0 init:ZERO_FVECTOR
// FUNCTION: 0x004692c0 init:ID_FMATRIX

//*********************************************************************************
bool	CFrontEnd::ProcessMouseClickInRect(float tlx, float tly, float brx, float bry, SINT button)	// FUNCTION: 0x00469390
{
	if (FEMESSBOX.IsInteractive())
		return FALSE;

	return MouseDispatchClickInRect(tlx, tly, brx, bry, button);
}

//*********************************************************************************
bool	CFrontEnd::GetCursorStateInRect(float tlx, float tly, float brx, float bry)	// FUNCTION: 0x004693d0
{
	if (FEMESSBOX.IsInteractive())
		return FALSE;

	return MouseInRect(tlx, tly, brx, bry);
}

//*********************************************************************************
bool	CFrontEnd::MouseClickedInRect(float tlx, float tly, float brx, float bry)	// FUNCTION: 0x00469400
{
	if (FEMESSBOX.IsInteractive())
		return FALSE;

	return ::MouseClickedInRect(tlx, tly, brx, bry);
}

//*********************************************************************************
bool	CFrontEnd::MouseOverRect(float tlx, float tly, float brx, float bry)	// FUNCTION: 0x00469430
{
	if (FEMESSBOX.IsInteractive())
		return FALSE;

	return ::MouseOverRect(tlx, tly, brx, bry);
}
