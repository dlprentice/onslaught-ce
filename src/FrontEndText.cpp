// FrontEndText.cpp: the front end's text (reconstructed from the retail PC executable; Stuart's file is not in his
// release): level, episode and briefing names by level number, and the front end's messages by id. The ids come
// from the game's text.stf.

#include	"Common.h"

#include	"Frontend.h"
#include	"data\MissionScripts\text\text.stf"

// FUNCTION: 0x00469460 init:ZERO_FVECTOR
// FUNCTION: 0x00469480 init:ID_FMATRIX

//*********************************************************************************
WCHAR	*CFrontEndText::GetLevelName(SINT level)	// FUNCTION: 0x00469550
{
	switch (level)
	{
	case 200:
	case 201:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_200);

	case 100:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_100);

	case 110:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_110);

	case 211:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_211);

	case 212:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_212);

	case 221:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_221);

	case 222:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_222);

	case 231:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_231);

	case 232:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_232);

	case 300:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_300);

	case 311:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_311);

	case 312:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_312);

	case 321:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_321);

	case 322:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_322);

	case 331:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_331);

	case 332:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_332);

	case 400:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_400);

	case 411:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_411);

	case 412:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_412);

	case 421:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_421);

	case 422:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_422);

	case 431:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_431);

	case 432:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_432);

	case 500:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_500);

	case 511:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_511);

	case 512:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_512);

	case 521:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_521);

	case 522:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_522);

	case 523:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_523);

	case 524:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_524);

	case 600:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_600);

	case 611:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_611);

	case 612:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_612);

	case 621:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_621);

	case 622:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_622);

	case 700:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_700);

	case 710:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_710);

	case 720:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_720);

	case 731:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_731);

	case 732:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_732);

	case 741:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_741);

	case 742:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_742);

	case 800:
		return TEXT_DB.GetString(FETX_LEVEL_NAME_800);

	default:
		return ToWCHAR("Unnamed Level");
	};
}

//*********************************************************************************
WCHAR	*CFrontEndText::GetEpisodeName(SINT episode)	// FUNCTION: 0x00469c20
{
	switch (episode)
	{
	case 1:
		return TEXT_DB.GetString(FETX_EPISODE_1);

	case 2:
		return TEXT_DB.GetString(FETX_EPISODE_2);

	case 3:
		return TEXT_DB.GetString(FETX_EPISODE_3);

	case 4:
		return TEXT_DB.GetString(FETX_EPISODE_4);

	case 5:
		return TEXT_DB.GetString(FETX_EPISODE_5);

	case 6:
		return TEXT_DB.GetString(FETX_EPISODE_6);

	case 7:
		return TEXT_DB.GetString(FETX_EPISODE_7);

	case 8:
		return TEXT_DB.GetString(FETX_EPISODE_8);

	default:
		return ToWCHAR("Unnamed Episode");
	};
}

//*********************************************************************************
DWORD	CFrontEndText::GetBriefingTextId(SINT level)	// FUNCTION: 0x00469cf0
{
	switch (level)
	{
	case 100:
		return BRIEFING_100_1;

	case 110:
		return BRIEFING_110_1;

	case 200:
	case 201:
		return BRIEFING_200_1;

	case 211:
		return BRIEFING_211_1;

	case 212:
		return BRIEFING_212_1;

	case 221:
		return BRIEFING_221_1;

	case 222:
		return BRIEFING_222_1;

	case 231:
		return BRIEFING_231_1;

	case 232:
		return BRIEFING_232_1;

	case 300:
		return BRIEFING_300_1;

	case 311:
		return BRIEFING_311_1;

	case 312:
		return BRIEFING_312_1;

	case 321:
		return BRIEFING_321_1;

	case 322:
		return BRIEFING_322_1;

	case 331:
		return BRIEFING_331_1;

	case 332:
		return BRIEFING_332_1;

	case 400:
		return BRIEFING_400_1;

	case 411:
		return BRIEFING_411_1;

	case 412:
		return BRIEFING_412_1;

	case 421:
		return BRIEFING_421_1;

	case 422:
		return BRIEFING_422_1;

	case 431:
		return BRIEFING_431_1;

	case 432:
		return BRIEFING_432_1;

	case 500:
		return BRIEFING_500_1;

	case 511:
		return BRIEFING_511_1;

	case 512:
		return BRIEFING_512_1;

	case 521:
		return BRIEFING_521_1;

	case 522:
		return BRIEFING_522_1;

	case 523:
		return BRIEFING_523_1;

	case 524:
		return BRIEFING_524_1;

	case 600:
		return BRIEFING_600_1;

	case 611:
		return BRIEFING_611_1;

	case 612:
		return BRIEFING_612_1;

	case 621:
		return BRIEFING_621_1;

	case 622:
		return BRIEFING_622_1;

	case 700:
		return BRIEFING_700_1;

	case 710:
		return BRIEFING_710_1;

	case 720:
		return BRIEFING_720_1;

	case 731:
		return BRIEFING_731_1;

	case 732:
		return BRIEFING_732_1;

	case 741:
		return BRIEFING_741_1;

	case 742:
		return BRIEFING_742_1;

	case 800:
		return BRIEFING_800_1;

	default:
		return DWORD(-1);
	};
}

//*********************************************************************************
WCHAR	*CFrontEndText::GetBriefingText(SINT level, SINT n)	// FUNCTION: 0x0046a1f0
{
	DWORD	id = GetBriefingTextId(level);

	return TEXT_DB.GetFollowingString(id, n);
}

//*********************************************************************************
DWORD	CFrontEndText::GetBriefingTextColour(SINT level, SINT n)	// FUNCTION: 0x0046a210
{
	return 0xffffdf5f;
}

//*********************************************************************************
WCHAR	*CFrontEndText::GetMultiplayerLevelDescription(SINT type)	// FUNCTION: 0x0046a220
{
	switch (type)
	{
	case 0:
		return TEXT_DB.GetString(FETX_MULTIPLAYER_DESCRIPTION_1);

	case 1:
		return TEXT_DB.GetString(FETX_MULTIPLAYER_DESCRIPTION_2);

	case 3:
		return TEXT_DB.GetStringByIdTwin(FETX_MULTIPLAYER_DESCRIPTION_1);

	case 2:
	case 4:
		return TEXT_DB.GetString(FETX_MULTIPLAYER_DESCRIPTION_3);

	default:
		return ToWCHAR("Unknown Multiplayer Level Description");
	};
}

//*********************************************************************************
static WCHAR	sText[256];						// 00679b8c
static UBYTE	sShowEnglish;					// 00679b88: Insert toggles the English (GetAsciiText)

// The PC build replaces the console's memory-card messages with one text of its own (0xb5), still evaluating the
// original (0046a9d8..0046aa04).
inline WCHAR	*PCText(WCHAR *console)
{
	wcscpy(sText, Localization_GetStringById(0xb5));
	return sText;
}

//*********************************************************************************
WCHAR	*CFrontEndText::GetText(EFETextHack text, SINT n)	// FUNCTION: 0x0046a2a0
{
	if (PLATFORM.KeyOnce(DIK_X))
		sShowEnglish ^= 1;

	if (sShowEnglish)
		return GetAsciiText(text);

	switch (text)
	{
	case FEX_NEW_GAME:
		return TEXT_DB.GetString(FETX_NEW_GAME);

	case FEX_CONTINUE_GAME:
		return TEXT_DB.GetString(FETX_CONTINUE_GAME);

	case FEX_LOAD_GAME:
		return TEXT_DB.GetString(FETX_LOAD_GAME);

	case FEX_SAVE_GAME:
		return TEXT_DB.GetString(FETX_SAVE_GAME);

	case FEX_MULTIPLAYER:
		return TEXT_DB.GetString(FETX_MULTIPLAYER);

	case FEX_GOODIES:
		return TEXT_DB.GetString(FETX_GOODIES);

	case FEX_OPTIONS:
		return TEXT_DB.GetString(FETX_OPTIONS);

	case FEX_LANGUAGE:
		return TEXT_DB.GetString(FETX_LANGUAGE);

	case FEX_QUIT:
		return TEXT_DB.GetString(FETX_QUIT);

	case FEX_DEBRIEFING:
		return TEXT_DB.GetString(FETX_DEBRIEFING);

	case FEX_SELECT_LEVEL:
		return TEXT_DB.GetString(FETX_SELECT_LEVEL);

	case FEX_MISSION_BRIEFING:
		return TEXT_DB.GetString(FETX_MISSION_BRIEFING);

	case FEX_GOODIES2:
		return TEXT_DB.GetString(FETX_GOODIES2);

	case FEX_LOADGAME2:
		return TEXT_DB.GetString(FETX_LOADGAME2);

	case FEX_DELETEGAME2:
		return TEXT_DB.GetString(FETX_DELETEGAME2);

	case FEX_SELECTLOADGAME2:
		return TEXT_DB.GetString(FETX_SELECTLOADGAME2);

	case FEX_CHOOSEGAMENAME2:
		return TEXT_DB.GetString(FETX_CHOOSEGAMENAME2);

	case FEX_SAVEGAME2:
		return TEXT_DB.GetString(FETX_SAVEGAME2);

	case FEX_SAVENEWGAME2:
		return TEXT_DB.GetString(FETX_SAVENEWGAME2);

	case FEX_SELECTDEVICE:
		return TEXT_DB.GetString(FETX_SELECTDEVICE);

	case FEX_BE_CONFIG:
		return TEXT_DB.GetString(FETX_BE_CONFIG);

	case FEX_MULTIPLAYER2:
		return TEXT_DB.GetString(FETX_MULTIPLAYER2);

	case FEX_GROUND_WEAPONS:
		return TEXT_DB.GetString(FETX_GROUND_WEAPONS);

	case FEX_AIR_WEAPONS:
		return TEXT_DB.GetString(FETX_AIR_WEAPONS);

	case FEX_ABANDONCG:
		return TEXT_DB.GetString(FETX_ABANDONCG);

	case FEX_ABANDONCG2:
		return TEXT_DB.GetString(FETX_ABANDONCG2);

	case FEX_NOAUTOSAVE:
		return TEXT_DB.GetString(FETX_NOAUTOSAVE);

	case FEX_YES:
		return TEXT_DB.GetString(FETX_YES);

	case FEX_NO:
		return TEXT_DB.GetString(FETX_NO);

	case FEX_OK:
		return TEXT_DB.GetString(FETX_OK);

	case FEX_SELECTLOADLOC:
		return TEXT_DB.GetString(FETX_SELECTLOADLOC);

	case FEX_SELECTSAVLOC:
		return TEXT_DB.GetString(FETX_SELECTSAVLOC);

	case FEX_NOWSAVING:
		return TEXT_DB.GetString(FETX_NOWSAVING);

	case FEX_NOWSAVING2:
		return TEXT_DB.GetString(FETX_NOWSAVING2);

	case FEX_SAVEDONE:
		return TEXT_DB.GetString(FETX_SAVEDONE);

	case FEX_FRESHSAVEDONE:
		return TEXT_DB.GetString(FETX_FRESHSAVEDONE);

	case FEX_SAVEGAME_DEFAULT_NAME:
		return TEXT_DB.GetString(FETX_SAVEGAME_DEFAULT_NAME);

	case FEX_SAVEGAME_COULD_NOT_DELETE:
		return TEXT_DB.GetString(FETX_SAVEGAME_COULD_NOT_DELETE);

	case FEX_OVERWRITE_SAVEGAME:
		return TEXT_DB.GetString(FETX_OVERWRITE_SAVEGAME);

	case FEX_CHOOSEDELETE:
		return TEXT_DB.GetString(FETX_CHOOSEDELETE);

	case FEX_CONFIRM_SAVEGAME_DELETE:
		return TEXT_DB.GetString(FETX_CONFIRM_SAVEGAME_DELETE);

	case FEX_GO_TO_DASHBOARD:
		return TEXT_DB.GetString(FETX_GO_TO_DASHBOARD);

	case FEX_EMPTY_DIRECTORY:
		return TEXT_DB.GetString(FETX_EMPTY_DIRECTORY);

	case FEX_LOADDONE:
		return TEXT_DB.GetString(FETX_LOADDONE);

	case FEX_FREEUP_XBOX:
		return TEXT_DB.GetString(FETX_FREEUP_XBOX);

	case FEX_REMOVED_XBOX:
		return TEXT_DB.GetString(FETX_REMOVED_XBOX);

	case FEX_REMOVEDSAVE:
		return TEXT_DB.GetString(FETX_REMOVEDSAVE_XBOX);

	case FEX_REMOVEDLOAD:
		return TEXT_DB.GetString(FETX_REMOVEDLOAD_XBOX);

	case FEX_LOADERROR:
		return TEXT_DB.GetString(FETX_LOADERROR);

	case FEX_REMOVEDDIR_XBOX:
		return TEXT_DB.GetString(FETX_REMOVEDDIR_XBOX);

	case FEX_SELECTLOC_XBOX:
		return TEXT_DB.GetString(FETX_SELECTLOC_XBOX);

	case FEX_NOTFOUND_XBOX:
		return TEXT_DB.GetString(FETX_NOTFOUND_XBOX);

	case FEX_LOADINVALID:
		return TEXT_DB.GetString(FETX_LOADINVALID_XBOX);

	case FEX_BADSAVEVERSION:
		return TEXT_DB.GetString(FETX_BADSAVEVERSION_XBOX);

	case FEX_NOMEMORYCARD_XBOX:
		return TEXT_DB.GetString(FETX_NOMEMORYCARD_XBOX);

	case FEX_DS_AVAILABLE_BLOCKS_XBOX:
		return TEXT_DB.GetString(FETX_DS_AVAILABLE_BLOCKS_XBOX);

	case FEX_AUTOSAVE_SCARED:
		return TEXT_DB.GetString(FETX_AUTOSAVE_SCARED_XBOX);

	case FEX_SAVEERROR:
		return TEXT_DB.GetString(FETX_SAVEERROR);

	case FEX_MU_1A_XBOX:
		return TEXT_DB.GetString(FETX_MU_1A_XBOX);

	case FEX_MU_1B_XBOX:
		return TEXT_DB.GetString(FETX_MU_1B_XBOX);

	case FEX_MU_2A_XBOX:
		return TEXT_DB.GetString(FETX_MU_2A_XBOX);

	case FEX_MU_2B_XBOX:
		return TEXT_DB.GetString(FETX_MU_2B_XBOX);

	case FEX_MU_3A_XBOX:
		return TEXT_DB.GetString(FETX_MU_3A_XBOX);

	case FEX_MU_3B_XBOX:
		return TEXT_DB.GetString(FETX_MU_3B_XBOX);

	case FEX_MU_4A_XBOX:
		return TEXT_DB.GetString(FETX_MU_4A_XBOX);

	case FEX_MU_4B_XBOX:
		return TEXT_DB.GetString(FETX_MU_4B_XBOX);

	case FEX_XBOX_HARD_DISK:
		return TEXT_DB.GetString(FETX_XBOX_HARD_DISK);

	case FEX_HELP_STRING1:
		return TEXT_DB.GetString(FETX_HELP_STRING1);

	case FEX_HELP_STRING2_XBOX:
		return TEXT_DB.GetString(FETX_HELP_STRING2_XBOX);

	case FEX_HELP_STRING3:
		return TEXT_DB.GetString(FETX_HELP_STRING3);

	case FEX_HELP_STRING4:
		return TEXT_DB.GetString(FETX_HELP_STRING4);

	case FEX_HELP_STRING5_XBOX:
		return TEXT_DB.GetString(FETX_HELP_STRING5_XBOX);

	case FEX_VK_SPACE:
		return TEXT_DB.GetString(FETX_VK_SPACE);

	case FEX_VK_BACKSPACE:
		return TEXT_DB.GetString(FETX_VK_BACKSPACE);

	case FEX_VK_SHIFT:
		return TEXT_DB.GetString(FETX_VK_SHIFT);

	case FEX_VK_CAPSLOCK:
		return TEXT_DB.GetString(FETX_VK_CAPSLOCK);

	case FEX_VK_ALPHABET:
		return TEXT_DB.GetString(FETX_VK_ALPHABET);

	case FEX_VK_SYMBOLS:
		return TEXT_DB.GetString(FETX_VK_SYMBOLS);

	case FEX_VK_ACCENTS:
		return TEXT_DB.GetString(FETX_VK_ACCENTS);

	case FEX_VK_DONE:
		return TEXT_DB.GetString(FETX_VK_DONE);

	case FEX_DS_TOTAL_BLOCKS:
		return TEXT_DB.GetString(FETX_DS_TOTAL_BLOCKS);

	case FEX_DS_XBOXHARDDISK:
		return TEXT_DB.GetString(FETX_DS_XBOXHARDDISK);

	case FEX_DELETE_SAVES_XBOX:
		return TEXT_DB.GetString(FETX_DELETE_SAVES_XBOX);

	case FEX_GOODY_TO_UNLOCK:
		return TEXT_DB.GetString(FETX_GOODY_TO_UNLOCK);

	case FEX_GOODY_UNLOCKED:
		return TEXT_DB.GetString(FETX_GOODY_UNLOCKED);

	case FEX_GOODY_UNKNOWN:
		return TEXT_DB.GetString(FETX_GOODY_UNKNOWN);

	case FEX_STATS_FOR_LEVEL:
		return TEXT_DB.GetString(FETX_STATS_FOR_LEVEL);

	case FEX_MISSION_STATS:
		return TEXT_DB.GetString(FETX_MISSION_STATS);

	case FEX_MISSION_STATUS:
		return TEXT_DB.GetString(FETX_MISSION_STATUS);

	case FEX_VICTORY:
		return TEXT_DB.GetString(FETX_VICTORY);

	case FEX_DEFEAT:
		return TEXT_DB.GetString(FETX_DEFEAT);

	case FEX_ABORTED:
		return TEXT_DB.GetString(FETX_ABORTED);

	case FEX_PRIMARY_OBJECTIVES:
		return TEXT_DB.GetString(FETX_PRIMARY_OBJECTIVES);

	case FEX_SECONDARY_OBJECTIVES:
		return TEXT_DB.GetString(FETX_SECONDARY_OBJECTIVES);

	case FEX_COMPLETE:
		return TEXT_DB.GetString(FETX_COMPLETE);

	case FEX_INCOMPLETE:
		return TEXT_DB.GetString(FETX_INCOMPLETE);

	case FEX_GRADE:
		return TEXT_DB.GetString(FETX_GRADE);

	case FEX_GRADENA:
		return TEXT_DB.GetString(FETX_GRADENA);

	case FEX_PRESS_START:
	case FEX_PRESS_START_2:
		return TEXT_DB.GetString(FETX_PRESS_START);

	case FEX_MULTIPLAYER_START_P1:
		return TEXT_DB.GetString(FETX_MULTIPLAYER_START_P1);

	case FEX_MULTIPLAYER_START_P2:
		return TEXT_DB.GetString(FETX_MULTIPLAYER_START_P2);

	case FEX_TOO_MANY_SAVES:
		return TEXT_DB.GetString(FETX_TOO_MANY_SAVES);

	case FEX_CONTROLLER_DISCONNECTED_PLEA:
		return TEXT_DB.GetString(IG_CONTROLLER_REMOVED_A);

	case FEX_UNUSED_43:
		return GetAsciiText(FEX_UNUSED_43);

	case FEX_UNUSED_87:
		return GetAsciiText(FEX_UNUSED_87);

	case FEX_COMPLETE_LEVEL_1_00:
		return GetAsciiText(FEX_COMPLETE_LEVEL_1_00);

	case FEX_COMPLETE_LEVEL_1_10:
		return GetAsciiText(FEX_COMPLETE_LEVEL_1_10);

	case FEX_COMPLETE_LEVEL_2_00:
		return GetAsciiText(FEX_COMPLETE_LEVEL_2_00);

	case FEX_COMPLETE_LEVEL_2_11:
		return GetAsciiText(FEX_COMPLETE_LEVEL_2_11);

	case FEX_COMPLETE_LEVEL_2_12:
		return GetAsciiText(FEX_COMPLETE_LEVEL_2_12);

	case FEX_COMPLETE_LEVEL_2_21:
		return GetAsciiText(FEX_COMPLETE_LEVEL_2_21);

	case FEX_HELP_STRING6_XBOX:
		return TEXT_DB.GetString(FETX_HELP_STRING6_XBOX);

	case FEX_PRESS_A_CONTINUE_PS2:
		return TEXT_DB.GetString(FETX_PRESS_A_CONTINUE_PS2);

	case FEX_MEMORY_CARD_REMOVED_XBOX:
		return TEXT_DB.GetString(FETX_MEMORY_CARD_REMOVED_XBOX);

	case FEX_DELETING:
		return PCText(TEXT_DB.GetString(FETX_DELETING_XBOX));

	case FEX_LOADING:
		return PCText(TEXT_DB.GetString(FETX_LOADING_XBOX));

	case FEX_SAVING:
		return PCText(TEXT_DB.GetString(FETX_SAVING_XBOX));

	case FEX_NO_SPACE_HD_DASH:
		return TEXT_DB.GetString(FETX_NO_SPACE_HD_DASH);

	case FEX_NO_SPACE_THERE:
		return TEXT_DB.GetString(FETX_NO_SPACE_THERE);

	case FEX_NO_SPACE_DASH:
		return TEXT_DB.GetString(FETX_NO_SPACE_DASH);

	case FEX_LOADING_HD:
		return TEXT_DB.GetString(FETX_LOADING_HD);

	case FEX_SAVING_HD:
		return TEXT_DB.GetString(FETX_SAVING_HD);

	case FEX_DELETING_HD:
		return TEXT_DB.GetString(FETX_DELETING_HD);

	case FEX_REMOVED_DURING_LOAD:
		return PCText(TEXT_DB.GetString(FETX_REMOVED_DURING_LOAD));

	case FEX_REMOVED_DURING_SAVE:
		return PCText(TEXT_DB.GetString(FETX_REMOVED_DURING_SAVE));

	case FEX_UNABLE_TO_LOAD:
		return TEXT_DB.GetString(FETX_UNABLE_TO_LOAD);

	case FEX_THAT_WILL_KILL_CAREER:
		return TEXT_DB.GetString(FETX_THAT_WILL_KILL_CAREER);

	case FEX_BOOTUP_INSUFFICIENT_SPACE_PS2_SLOT1:
		return TEXT_DB.GetString(FETX_BOOTUP_INSUFFICIENT_SPACE_PS2_SLOT1);

	case FEX_BOOTUP_INSUFFICIENT_SPACE_PS2_SLOT2:
		return TEXT_DB.GetString(FETX_BOOTUP_INSUFFICIENT_SPACE_PS2_SLOT2);

	case FEX_BOOTUP_INSUFFICIENT_SPACE_PS2_BOTH:
		return TEXT_DB.GetString(FETX_BOOTUP_INSUFFICIENT_SPACE_PS2_BOTH);

	case FEX_BOOTUP_NO_CARD_PS2:
		if (gAmericanText)
			return ToWCHAR("There is no memory card (8MB) (for PlayStation\xae" "2) inserted.  Please insert a memory card (8MB) (for PlayStation\xae" "2) in MEMORY CARD slot 1 or MEMORY CARD slot 2 with at least 110KB of free space, or press [ to continue.");
		return TEXT_DB.GetString(FETX_BOOTUP_NO_CARD_PS2);

	case FEX_UNFORMATTED_PS2:
		return PCText(TEXT_DB.GetString(FETX_UNFORMATTED));

	case FEX_REMOVEDSAVE_PS2_V3:
		return PCText(TEXT_DB.GetString(FETX_REMOVEDSAVE_PS2_V3));

	case FEX_REMOVEDLOAD_PS2_V3:
		return PCText(TEXT_DB.GetString(FETX_REMOVEDLOAD_PS2_V3));

	case FEX_REMOVEDDIR_PS2_V3:
		return PCText(TEXT_DB.GetString(FETX_REMOVEDDIR_PS2_V3));

	case FEX_NOTFOUND_PS2_V3:
		return PCText(TEXT_DB.GetString(FETX_NOTFOUND_PS2_V3));

	case FEX_LOADINVALID_PS2_V3:
		return PCText(TEXT_DB.GetString(FETX_LOADINVALID_PS2_V3));

	case FEX_NOSPACE_WITHSAVES_PS2:
		return PCText(TEXT_DB.GetString(FETX_NOSPACE_WITHSAVES_PS2));

	case FEX_TOOMANYSAVES_PS2:
		return PCText(TEXT_DB.GetString(FETX_TOOMANYSAVES_PS2));

	case FEX_NOSPACE_NOSAVES_PS2:
		return PCText(TEXT_DB.GetString(FETX_NOSPACE_NOSAVES_PS2));

	case FEX_HELP_STRING10:
		return TEXT_DB.GetString(FETX_HELP_STRING10);

	case FEX_BACK:
		return TEXT_DB.GetString(FETX_BACK);

	case FEX_CONTROLS_FIRE:
		return TEXT_DB.GetString(FETX_CONTROLS_FIRE);

	case FEX_CONTROLS_SELECT_WEAPON:
		return TEXT_DB.GetString(FETX_CONTROLS_SELECT_WEAPON);

	case FEX_CONTROLS_AIR_BRAKE:
		return TEXT_DB.GetString(FETX_CONTROLS_AIR_BRAKE);

	case FEX_CONTROLS_SPECIAL_FUNCTION:
		return TEXT_DB.GetString(FETX_CONTROLS_SPECIAL_FUNCTION);

	case FEX_CONTROLS_TRANSFORM:
		return TEXT_DB.GetString(FETX_CONTROLS_TRANSFORM);

	case FEX_CONTROLS_ZOOM:
		return TEXT_DB.GetString(FETX_CONTROLS_ZOOM);

	case FEX_CONTROLS_ZOOM_IN:
		return TEXT_DB.GetString(FETX_CONTROLS_ZOOM_IN);

	case FEX_CONTROLS_ZOOM_OUT:
		return TEXT_DB.GetString(FETX_CONTROLS_ZOOM_OUT);

	case FEX_CONTROLS_MOVE:
		return TEXT_DB.GetString(FETX_CONTROLS_MOVE);

	case FEX_CONTROLS_LOOK:
		return TEXT_DB.GetString(FETX_CONTROLS_LOOK);

	case FEX_CONTROLS_PAUSE:
		return TEXT_DB.GetString(FETX_CONTROLS_PAUSE);

	case FEX_CONTROLLER_OPTIONS:
		return TEXT_DB.GetString(FETX_CONTROLLER_OPTIONS);

	case FEX_CONTROLLER_CONFIGURATION_1:
		return TEXT_DB.GetString(FETX_CONTROLLER_CONFIGURATION_1);

	case FEX_CONTROLLER_CONFIGURATION_2:
		return TEXT_DB.GetString(FETX_CONTROLLER_CONFIGURATION_2);

	case FEX_CONTROLLER_CONFIGURATION_3:
		return TEXT_DB.GetString(FETX_CONTROLLER_CONFIGURATION_3);

	case FEX_CONTROLLER_CONFIGURATION_4:
		return TEXT_DB.GetString(FETX_CONTROLLER_CONFIGURATION_4);

	case FEX_LEFT_ANALOG_STICK:
		return TEXT_DB.GetString(FETX_LEFT_ANALOG_STICK);

	case FEX_RIGHT_ANALOG_STICK:
		return TEXT_DB.GetString(FETX_RIGHT_ANALOG_STICK);

	case FEX_CARD_OR_CONTINUE:
		return TEXT_DB.GetString(FETX_CARD_OR_CONTINUE);

	case FEX_SAVE_FAILED_PS2:
		{
			WCHAR	*text;

			switch (TEXT.GetLanguage())
			{
			case 1:	text = ToWCHAR("Echec de la sauvegarde ! V\xe9rifier la memory card (carte m\xe9moire) (8MB) (pour PlayStation\xae" "2) dans la fente pour MEMORY CARD %d et r\xe9" "essayer."); break;
			case 2:	text = ToWCHAR("Speichern gescheitert. Bitte \xfc" "berpr\xfc" "fen Sie die Memory Card (8MB) (f\xfcr PlayStation\xae" "2) in MEMORY CARD-Steckplatz %d und versuchen Sie es noch einmal."); break;
			case 4:	text = ToWCHAR("Salvataggio fallito! Controllare la memory card (8MB) (per PlayStation\xae" "2) nell'ingresso MEMORY CARD %d e riprovare."); break;
			case 3:	text = ToWCHAR("\xa1" "Error al guardar! Comprueba la memory card (tarjeta de memoria) (8MB) (para PlayStation\xe2" "2) de la ranura para MEMORY CARD (tarjeta de memoria) %d e int\xe9ntalo de nuevo."); break;
			default: text = ToWCHAR("Save failed! Check memory card (8MB) (for PlayStation\xae" "2) in MEMORY CARD slot %d and please try again."); break;
			};

			return PCText(text);
		}

	default:
		return ToWCHAR("Unknown Text");
	};
}

//*********************************************************************************
WCHAR	*CFrontEndText::GetAsciiText(EFETextHack text)	// FUNCTION: 0x0046b1e0
{
	switch (text)
	{
	case FEX_NEW_GAME:
		return ToWCHAR("New Game");

	case FEX_CONTINUE_GAME:
		return ToWCHAR("Continue Game");

	case FEX_LOAD_GAME:
		return ToWCHAR("Load Game");

	case FEX_SAVE_GAME:
		return ToWCHAR("Save Game");

	case FEX_MULTIPLAYER:
		return ToWCHAR("Multiplayer");

	case FEX_GOODIES:
		return ToWCHAR("Goodies");

	case FEX_OPTIONS:
		return ToWCHAR("Options");

	case FEX_LANGUAGE:
		return ToWCHAR("Language");

	case FEX_QUIT:
		return ToWCHAR("Quit");

	case FEX_DEBRIEFING:
		return ToWCHAR("DEBRIEFING");

	case FEX_SELECT_LEVEL:
		return ToWCHAR("SELECT LEVEL");

	case FEX_MISSION_BRIEFING:
		return ToWCHAR("MISSION BRIEFING");

	case FEX_GOODIES2:
		return ToWCHAR("GOODIES");

	case FEX_LOADGAME2:
		return ToWCHAR("LOAD GAME");

	case FEX_DELETEGAME2:
		return ToWCHAR("DELETE SAVED GAME");

	case FEX_SELECTLOADGAME2:
		return ToWCHAR("SELECT LOAD GAME");

	case FEX_CHOOSEGAMENAME2:
		return ToWCHAR("CHOOSE GAME NAME");

	case FEX_SAVEGAME2:
		return ToWCHAR("SAVE GAME");

	case FEX_SAVENEWGAME2:
		return ToWCHAR("SAVE NEW GAME");

	case FEX_SELECTDEVICE:
		return ToWCHAR("SELECT DEVICE");

	case FEX_BE_CONFIG:
		return ToWCHAR("SELECT CONFIGURATION");

	case FEX_MULTIPLAYER2:
		return ToWCHAR("MULTIPLAYER");

	case FEX_GROUND_WEAPONS:
		return ToWCHAR("Ground");

	case FEX_AIR_WEAPONS:
		return ToWCHAR("Air");

	case FEX_ABANDONCG:
		return ToWCHAR("This will abandon the current game. Are you sure you wish to do this?");

	case FEX_ABANDONCG2:
		return ToWCHAR("This will abandon the current game and load a new game. Are you sure you wish to do this?");

	case FEX_NOAUTOSAVE:
		return ToWCHAR("Are you sure? If you select 'yes' game progress will not be saved as you play. If you select 'no' you can go back to choosing a saved game.");

	case FEX_YES:
		return ToWCHAR("Yes");

	case FEX_NO:
		return ToWCHAR("No");

	case FEX_OK:
		return ToWCHAR("OK");

	case FEX_SELECTLOADLOC:
		return ToWCHAR("Please select load location");

	case FEX_SELECTSAVLOC:
		return ToWCHAR("Please select save location");

	case FEX_NOWSAVING:
		return ToWCHAR("Now saving progress - please do not remove MU.");

	case FEX_NOWSAVING2:
		return ToWCHAR("Now saving progress - please do not turn off your XBOX.");

	case FEX_SAVEDONE:
		return ToWCHAR("Your progress has been saved.");

	case FEX_FRESHSAVEDONE:
		return ToWCHAR("A new save game has been created.");

	case FEX_SAVEERROR:
		return ToWCHAR("An error occured during saving.");

	case FEX_SAVEGAME_DEFAULT_NAME:
		return ToWCHAR("Aquila career");

	case FEX_SAVEGAME_COULD_NOT_DELETE:
		return ToWCHAR("Could not delete that saved game");

	case FEX_OVERWRITE_SAVEGAME:
		return ToWCHAR("A game with this name already exists and will be overwritten. Are you sure?");

	case FEX_CHOOSEDELETE:
		return ToWCHAR("DELETE A GAME");

	case FEX_CONFIRM_SAVEGAME_DELETE:
		return ToWCHAR("Are you sure you want to delete that saved game?");

	case FEX_GO_TO_DASHBOARD:
		return ToWCHAR("The Xbox itself does not have enough space for a single saved game. Do you want to go to the dashboard right now to free up some space, before you start a career?");

	case FEX_EMPTY_DIRECTORY:
		return ToWCHAR("(no saved games here)");

	case FEX_LOADDONE:
		return ToWCHAR("Your progress has been loaded.");

	case FEX_LOADERROR:
		return ToWCHAR("An error occured during loading.");

	case FEX_FREEUP_XBOX:
		return ToWCHAR(" does not have enough free space to save your progress. Do you wish to free up space?");

	case FEX_REMOVED_XBOX:
		return ToWCHAR("The memory unit containing your saved game has been removed. Please select another location to save your progress to.");

	case FEX_REMOVEDSAVE:
		return ToWCHAR("The memory unit was removed. Your game did not save correctly.");

	case FEX_REMOVEDLOAD:
		return ToWCHAR("The memory unit was removed. The game could not be loaded.");

	case FEX_REMOVEDDIR_XBOX:
		return ToWCHAR("The memory unit was removed. Please select another.");

	case FEX_SELECTLOC_XBOX:
		return ToWCHAR("Please select a location to save your progress to.");

	case FEX_NOTFOUND_XBOX:
		return ToWCHAR("This device does not contain any Battle Engine Aquila saved games.");

	case FEX_LOADINVALID:
		return ToWCHAR("The saved game on this device is invalid and could not be loaded.");

	case FEX_BADSAVEVERSION:
		return ToWCHAR("This saved game is from a different version of the game so cannot be loaded.");

	case FEX_NOMEMORYCARD_XBOX:
		return ToWCHAR("Please insert a memory card");

	case FEX_DS_AVAILABLE_BLOCKS_XBOX:
		return ToWCHAR("Available Blocks:");

	case FEX_AUTOSAVE_SCARED:
		return ToWCHAR("The memory unit being used for saving has been removed since the last autosave. Please re-select your save location");

	case FEX_HELP_STRING1:
		return ToWCHAR("[ to start level");

	case FEX_HELP_STRING2_XBOX:
		return ToWCHAR("] to go back");

	case FEX_HELP_STRING3:
		return ToWCHAR("[ to continue");

	case FEX_HELP_STRING4:
		return ToWCHAR("@ to move, [ to select");

	case FEX_HELP_STRING5_XBOX:
		return ToWCHAR("] to go back, ^ don't save");

	case FEX_VK_SPACE:
		return ToWCHAR("Space");

	case FEX_VK_BACKSPACE:
		return ToWCHAR("Backspace");

	case FEX_VK_SHIFT:
		return ToWCHAR("Shift");

	case FEX_VK_CAPSLOCK:
		return ToWCHAR("Caps Lock");

	case FEX_VK_ALPHABET:
		return ToWCHAR("Alphabet");

	case FEX_VK_SYMBOLS:
		return ToWCHAR("Symbols");

	case FEX_VK_ACCENTS:
		return ToWCHAR("Accents");

	case FEX_VK_DONE:
		return ToWCHAR("Done");

	case FEX_DS_TOTAL_BLOCKS:
		return ToWCHAR("Total Blocks:");

	case FEX_DS_XBOXHARDDISK:
		return ToWCHAR("Xbox Hard Disk");

	case FEX_DELETE_SAVES_XBOX:
		return ToWCHAR("There isn't enough space to save your game there. Do you want to delete a saved game to make some space?");

	case FEX_GOODY_TO_UNLOCK:
		return ToWCHAR("To Unlock : ");

	case FEX_GOODY_UNLOCKED:
		return ToWCHAR("Unlocked!");

	case FEX_GOODY_UNKNOWN:
		return ToWCHAR("???");

	case FEX_COMPLETE_LEVEL_1_00:
		return ToWCHAR("Complete Level 1.00");

	case FEX_COMPLETE_LEVEL_1_10:
		return ToWCHAR("Complete Level 1.10");

	case FEX_COMPLETE_LEVEL_2_00:
		return ToWCHAR("Complete Level 2.00");

	case FEX_COMPLETE_LEVEL_2_11:
		return ToWCHAR("Complete Level 2.11");

	case FEX_COMPLETE_LEVEL_2_12:
		return ToWCHAR("Complete Level 2.12");

	case FEX_COMPLETE_LEVEL_2_21:
		return ToWCHAR("Complete Level 2.21");

	case FEX_STATS_FOR_LEVEL:
		return ToWCHAR("Stats for Level ");

	case FEX_MISSION_STATS:
		return ToWCHAR("Mission Stats");

	case FEX_MISSION_STATUS:
		return ToWCHAR("Mission Status");

	case FEX_VICTORY:
		return ToWCHAR("Victory");

	case FEX_DEFEAT:
		return ToWCHAR("Defeat");

	case FEX_ABORTED:
		return ToWCHAR("Aborted");

	case FEX_PRIMARY_OBJECTIVES:
		return ToWCHAR("Primary Objectives");

	case FEX_SECONDARY_OBJECTIVES:
		return ToWCHAR("Secondary Objectives");

	case FEX_COMPLETE:
		return ToWCHAR("Complete");

	case FEX_INCOMPLETE:
		return ToWCHAR("Incomplete");

	case FEX_GRADE:
		return ToWCHAR("Grade:");

	case FEX_GRADENA:
		return ToWCHAR("Grade: n/a");

	case FEX_CONTROLLER_DISCONNECTED_PLEA:
		return ToWCHAR("Controller disconnected. Please reconnect into the same port and press START");

	case FEX_MULTIPLAYER_START_P1:
		return ToWCHAR("Player 1 press START");

	case FEX_MULTIPLAYER_START_P2:
		return ToWCHAR("Player 2 press START");

	case FEX_PRESS_START:
	case FEX_PRESS_START_2:
		return ToWCHAR("Press START");

	case FEX_TOO_MANY_SAVES:
		return ToWCHAR("There are too many savegames there. Do you want to delete a saved game to make some space?");

	default:
		return ToWCHAR("Unknown Text");
	};
}

//******************************************************************************************
// The front-end text's set-up and shut-down (nothing on the PC) that no code above defined. Each compiles to a body
// another function already has, so the linker folds it into that one (/OPT:ICF): the retail table's slot points at the
// survivor, whose address each annotation names (the class header notes the slots' survivors too).

// FUNCTION: 0x004fdc10
BOOL CFrontEndText::Init()
{
	return TRUE;
}

// FUNCTION: 0x0040c640
void CFrontEndText::Shutdown()
{
}
