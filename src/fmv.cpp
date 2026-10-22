// fmv.cpp: the platform-independent full-motion video player, CFMV (reconstructed; Stuart's file is not in his
// release). CDXFMV (DXFMV) plays the clips; this part skips them and offers the fmv_play console command.

#include	"Common.h"

#define FMV_CPP
#include	"fmv.h"
#include	"console.h"
#include	"CLIParams.h"
#include	"text.h"


//*********************************************************************************
// fmv_play <filename>
void	con_fmv_play(char *cmd)	// FUNCTION: 0x004655d0
{
	if (strlen(cmd) > 9)
		FMV.PlayFullscreen(cmd + 9, FALSE, FALSE);
	else
		CONSOLE.Print("Syntax : fmv_play <filename>\n");
}

//*********************************************************************************
BOOL	CFMV::PlayFullscreen(char *name, BOOL a, BOOL localise, int b, int c, int d, int e)	// FUNCTION: 0x00465640
{
	mSkippable = (a || CLIPARAMS.mAttractMode) ? TRUE : FALSE;
	CController::SetNonInteractiveSection(true);
	int		language = localise ? TEXT.GetLanguage() : 0;
	BOOL	played = Play(name, language, b, c, d, e);
	CController::SetNonInteractiveSection(false);
	return played;
}

//*********************************************************************************
void	CFMV::Init()	// FUNCTION: 0x004656b0
{
	CONSOLE.RegisterCommand("fmv_play", "Play a named FMV clip", con_fmv_play);
	mQuit = FALSE;
}

//*********************************************************************************
void	CFMV::ReceiveButtonAction(CController *from_controller, int button, float val)	// FUNCTION: 0x004656e0
{
	if ((button == BUTTON_SKIP_CUTSCENE) || ((button == 0x42) && mSkippable))
	{
		mQuit = TRUE;
		CLIPARAMS.mAttractMode = FALSE;
	}
}
