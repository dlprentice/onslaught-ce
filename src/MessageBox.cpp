// MessageBox.cpp: the in-game message box, the radio messages shown with the speaker's portrait while their voice
// plays (reconstructed from MessageBox.obj, 004b6bb0..004b8cda; Stuart's release has no copy). A message's voice is
// an .ogg file read on a worker thread (COggLoader) and played when the load finishes.

#include	"Common.h"

#include	"DebugLog.h"
#include	"MessageBox.h"
#include	"WaitingThread.h"
#include	"OggFileRead.h"
#include	"eventmanager.h"
#include	"Event.h"
#include	"thing.h"
#include	"SoundManager.h"
#include	"CLIParams.h"
#include	"Platform.h"
#include	"console.h"
#include	"frontend.h"
#include	"ltshell.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"
#include	"text.h"
#include	"Hud.h"
#include	"game.h"

// FUNCTION: 0x004b6bb0 init:ZERO_FVECTOR
// FUNCTION: 0x004b6bd0 init:ID_FMATRIX

#define VOICE_BUFFER_SIZE	0x100000

// reads a voice into memory on its own thread (RTTI: CWaitingThread at +0x00, COggFileRead at +0x20)
class COggLoader : public CWaitingThread, public COggFileRead
{
public:
	COggLoader() {}								// 004b6d30
	// In the class: retail emits Run in the class's vtable batch, between the constructor (004b6d30) and ??_G
	// (004b6df0), where an in-class inline virtual comes out, not as an out-of-line function later in the file.
	virtual void	Run()						// 004b6d90
	{
		if (mFileName[0])
		{
			if (Open(mFileName) >= 0)
			{
				if (Read(VOICE_BUFFER_SIZE, mBuffer, &mSize) < 0)
				{
					mSize = 0;
					return;
				}
				Close();
			}
		}
	}

	BYTE		mBuffer[VOICE_BUFFER_SIZE];		// +0x2310
	char		mFileName[260];					// +0x102310
	UINT		mSize;							// +0x102414: what the last load read
};

// FUNCTION: 0x004b6ca0 init:gVoiceLoader
// FUNCTION: 0x004b6cc0 exit:gVoiceLoader
// FUNCTION: 0x004b6cd0 ??1COggLoader@@UAE@XZ
// FUNCTION: 0x004b6d30 ??0COggLoader@@QAE@XZ
// FUNCTION: 0x004b6df0 ??_GCOggLoader@@UAEPAXI@Z
COggLoader				gVoiceLoader;			// 00704f78
char					gVoiceName[260];		// 00704e74: the voice being loaded, empty for none
// FUNCTION: 0x004b6e20 init:gVoiceMessage
// FUNCTION: 0x004b6e40 exit:gVoiceMessage
CActiveReader<CMessage>	gVoiceMessage;			// 00704e70: the message the voice being loaded belongs to
CSample					*gVoiceSample = NULL;			// 008073d0: the voice playing

extern bool				gFileReadFailed;		// 0089c808 (set by CXBOXAsyncCache::LoadAsync, 00539d50)

// FUNCTION: 0x004b6f50 ?GetSoundOrientation@CMessage@@UAE?AVFMatrix@@XZ

//*********************************************************************************
CMessage::CMessage(WCHAR *name, WCHAR *text, int string_id, float delay, CThing *speaker, CScheduledEvent *event, int priority)	// FUNCTION: 0x004b6e50
	: mFinishedPlaying(FALSE), mText(text), mStringId(string_id), mName(name), mDelay(delay), mEvent(event), mPriority(priority), mHasSpeaker(FALSE)
{
	if (speaker)
	{
		mSpeaker.SetReader(speaker);
		mHasSpeaker = TRUE;
	}
	mDuration = wcslen(text) / 22.f + 0.2f;
	mRevealed = 0;
	mDots = 0;
}

// FUNCTION: 0x004b6f10 ??_GCMessage@@UAEPAXI@Z
//*********************************************************************************
FVector CMessage::GetSoundPos()	// FUNCTION: 0x004b6f30
{
	return FVector(0, 0, 0);
}

// Defined after GetSoundPos: retail emits it right after GetSoundPos (a deferred inline follows the later of its first
// user and its definition).
inline FMatrix CMessage::GetSoundOrientation() { return ID_FMATRIX; }

//******************************************************************************************
// Lays the revealed text (and the waiting dots) out in lines of up to width characters, 0x56 apart: a word goes to the
// next line whole, a space followed by !?;: stays with its word, the view scrolls up once max_lines are full, and
// what is not yet revealed shows as dots.
// Copy the reveal boundary before extending the end count, and suppress a space break before punctuation.
// These value lifetimes and the line-transition order reproduce the retail 004b6f70 section.
void CMessage::WordWrap(WCHAR *lines, int width, int max_lines)	// FUNCTION: 0x004b6f70
{
	WCHAR	*text = mText;
	int		end = mRevealed;
	int		total = wcslen(text) + mDots;
	WCHAR	punct[5];
	int		i = 0;
	int		col = 0;
	int		n = 0;
	wcscpy(punct, ToWCHAR("!?;:"));

	for (int l = 0; l < 8; l++)
	{
		((char *)(lines + l * 0x56))[0] = 0;
		((char *)(lines + l * 0x56))[1] = 0;
	}

	int		revealed = end;
	end += mDots;
	if (end > 0)
	{
		int		pos = 0;
		for (; i < end; i++)
		{
			int		len = 0;
			if (i < revealed)
			{
				WCHAR	*p = &text[i];
				BOOL	last = FALSE;
				for (;;)
				{
					if (i + len >= total)
						last = TRUE;
					bool space = (*p == ' ');
					if (space && (i + len + 1 < total))
					{
						WCHAR c = text[i + len + 1];
						if ((c == punct[0]) || (c == punct[1]) || (c == punct[2]) || (c == punct[3]))
							space = FALSE;
					}
					if (space || last)
						break;
					len++;
					p++;
					if ((mDots) && (i + len >= revealed))
					{
						len += mDots;
						break;
					}
				}
			}
			else
				len = revealed - i + 5;

			if (len + col >= width)
			{
				lines[pos + col] = 0;
				col = 0;
				n++;
				pos += 0x56;
				if (n >= max_lines)
				{
					WCHAR	*line = lines;
					for (int k = 7; k; k--)
					{
						for (int j = 0; j < 0x55; j++)
							line[j] = line[j + 0x56];
						line += 0x56;
					}
					n--;
					pos -= 0x56;
				}
			}

			WCHAR	ch = text[i];
			if ((ch == ' ') && (!col))
				continue;
			if (i < revealed)
				lines[pos + col++] = ch;
			else
				lines[pos + col++] = '.';
		}
	}
	lines[n * 0x56 + col] = 0;
}

//*********************************************************************************
CMessage::~CMessage()	// FUNCTION: 0x004b7160
{
	mEvent = NULL;
}

// Inline, defined after the destructor: retail emits its copy there (004b71d0), after ~CMessage and before
// CMessageBox's constructor, not with CMessage's vtable after the constructor.
// FUNCTION: 0x004b71d0
inline void CMessage::SampleFinishedPlaying(CSoundEvent* event) { mFinishedPlaying = TRUE; }

// the voice stops and its sample goes
#define KILL_VOICE()											\
	if (gVoiceSample)											\
	{															\
		SOUND.KillAllInstancesOfSample(gVoiceSample);			\
		if (gVoiceSample)										\
		{														\
			delete gVoiceSample;								\
			gVoiceSample = NULL;								\
		}														\
	}

// FUNCTION: 0x004b7300 ??_GCMessageBox@@UAEPAXI@Z

//*********************************************************************************
CMessageBox::CMessageBox()	// FUNCTION: 0x004b71e0
	: mRandom(123456)
{
	mCurrent = NULL;
	mAllowedToPlay = FALSE;
	mUnknown1ac = 0;
	mVoiceName[0] = 0;
	mCanAdvance = TRUE;
	mPortrait = -1;
	mUnknown1b4 = -1.f;
	mPortraitTexture = mPortraits[0][mUnknown1ac];

	SIZE s;
	PLATFORM.Font(FONT_SMALL)->GetTextExtent(ToWCHAR("The quick brown fox jumps over the lazy dog"), &s);
	mCharsPerLine = s.cx ? 8930 / s.cx : 0;
	mUnknown1bc = 3;

	memset(mPortraits, 0, sizeof(mPortraits));
	mUnknown1a4 = NULL;
	mUnknown1a0 = NULL;
	mOpen = 0;

	gVoiceName[0] = 0;
	gVoiceMessage.SetReader(NULL);
}

//*********************************************************************************
void	CMessageBox::InitResources()	// FUNCTION: 0x004b7320
{
	mPortraits[0][0] = CDXTexture::FindTexture("MessageBox\\Carver_oo.tga", 0, 0, 1, 1, 1);
	mPortraits[0][1] = CDXTexture::FindTexture("MessageBox\\Carver_ee.tga", 0, 0, 1, 1, 1);
	mPortraits[0][2] = CDXTexture::FindTexture("MessageBox\\Carver_mm.tga", 0, 0, 1, 1, 1);
	mPortraits[0][3] = CDXTexture::FindTexture("MessageBox\\Carver_aa.tga", 0, 0, 1, 1, 1);
	mPortraits[0][4] = NULL;
	mPortraits[0][5] = NULL;
	mSpeakerIds[0] = 0xdcdee;
	mPortraits[1][0] = CDXTexture::FindTexture("MessageBox\\tat_oo.tga", 0, 0, 1, 1, 1);
	mPortraits[1][1] = CDXTexture::FindTexture("MessageBox\\tat_ee.tga", 0, 0, 1, 1, 1);
	mPortraits[1][2] = CDXTexture::FindTexture("MessageBox\\tat_mm.tga", 0, 0, 1, 1, 1);
	mPortraits[1][3] = CDXTexture::FindTexture("MessageBox\\tat_aa.tga", 0, 0, 1, 1, 1);
	mPortraits[1][4] = NULL;
	mPortraits[1][5] = NULL;
	mSpeakerIds[1] = 0x170470;
	mPortraits[2][0] = CDXTexture::FindTexture("MessageBox\\FC_Kramer_oo.tga", 0, 0, 1, 1, 1);
	mPortraits[2][1] = CDXTexture::FindTexture("MessageBox\\FC_Kramer_ee.tga", 0, 0, 1, 1, 1);
	mPortraits[2][2] = CDXTexture::FindTexture("MessageBox\\FC_Kramer_mm.tga", 0, 0, 1, 1, 1);
	mPortraits[2][3] = CDXTexture::FindTexture("MessageBox\\FC_Kramer_aa.tga", 0, 0, 1, 1, 1);
	mPortraits[2][4] = NULL;
	mPortraits[2][5] = NULL;
	mSpeakerIds[2] = 0xe0831;
	mPortraits[3][0] = NULL;
	mPortraits[3][1] = NULL;
	mPortraits[3][2] = NULL;
	mPortraits[3][3] = NULL;
	mPortraits[3][4] = NULL;
	mPortraits[3][5] = NULL;
	mSpeakerIds[3] = 0x5c2de;
	mPortraits[4][0] = CDXTexture::FindTexture("MessageBox\\fc_Tara_oo.tga", 0, 0, 1, 1, 1);
	mPortraits[4][1] = CDXTexture::FindTexture("MessageBox\\fc_Tara_ee.tga", 0, 0, 1, 1, 1);
	mPortraits[4][2] = CDXTexture::FindTexture("MessageBox\\fc_Tara_mm.tga", 0, 0, 1, 1, 1);
	mPortraits[4][3] = CDXTexture::FindTexture("MessageBox\\fc_Tara_aa.tga", 0, 0, 1, 1, 1);
	mPortraits[4][4] = NULL;
	mPortraits[4][5] = NULL;
	mSpeakerIds[4] = 0x3c345;
	mPortraits[5][0] = CDXTexture::FindTexture("MessageBox\\FC_Simmons_surprise_closed.tga", 0, 0, 1, 1, 1);
	mPortraits[5][1] = CDXTexture::FindTexture("MessageBox\\FC_Simmons_surprise_open.tga", 0, 0, 1, 1, 1);
	mPortraits[5][2] = CDXTexture::FindTexture("MessageBox\\FC_Simmons_stern_closed.tga", 0, 0, 1, 1, 1);
	mPortraits[5][3] = CDXTexture::FindTexture("MessageBox\\FC_Simmons_stern_open.tga", 0, 0, 1, 1, 1);
	mPortraits[5][4] = CDXTexture::FindTexture("MessageBox\\FC_Simmons_blank_closed.tga", 0, 0, 1, 1, 1);
	mPortraits[5][5] = CDXTexture::FindTexture("MessageBox\\FC_Simmons_blank_open.tga", 0, 0, 1, 1, 1);
	mSpeakerIds[5] = 0x1a19e0;
	mPortraits[6][0] = CDXTexture::FindTexture("MessageBox\\FC_Lorenzo_oo.tga", 0, 0, 1, 1, 1);
	mPortraits[6][1] = CDXTexture::FindTexture("MessageBox\\FC_Lorenzo_ee.tga", 0, 0, 1, 1, 1);
	mPortraits[6][2] = CDXTexture::FindTexture("MessageBox\\FC_Lorenzo_mm.tga", 0, 0, 1, 1, 1);
	mPortraits[6][3] = CDXTexture::FindTexture("MessageBox\\FC_Lorenzo_aa.tga", 0, 0, 1, 1, 1);
	mPortraits[6][4] = NULL;
	mPortraits[6][5] = NULL;
	mSpeakerIds[6] = 0x19f8cf;
	mPortraits[7][0] = CDXTexture::FindTexture("MessageBox\\FC_Billy_oo.tga", 0, 0, 1, 1, 1);
	mPortraits[7][1] = CDXTexture::FindTexture("MessageBox\\FC_Billy_ee.tga", 0, 0, 1, 1, 1);
	mPortraits[7][2] = CDXTexture::FindTexture("MessageBox\\FC_Billy_mm.tga", 0, 0, 1, 1, 1);
	mPortraits[7][3] = CDXTexture::FindTexture("MessageBox\\FC_Billy_aa.tga", 0, 0, 1, 1, 1);
	mPortraits[7][4] = NULL;
	mPortraits[7][5] = NULL;
	mSpeakerIds[7] = 0xcb1fd;
	mPortraits[8][0] = CDXTexture::FindTexture("MessageBox\\cmdr_oo.tga", 0, 0, 1, 1, 1);
	mPortraits[8][1] = CDXTexture::FindTexture("MessageBox\\cmdr_ee.tga", 0, 0, 1, 1, 1);
	mPortraits[8][2] = CDXTexture::FindTexture("MessageBox\\cmdr_mm.tga", 0, 0, 1, 1, 1);
	mPortraits[8][3] = CDXTexture::FindTexture("MessageBox\\cmdr_aa.tga", 0, 0, 1, 1, 1);
	mPortraits[8][4] = NULL;
	mPortraits[8][5] = NULL;
	mSpeakerIds[8] = 0x534dfa;
	mPortraits[9][0] = CDXTexture::FindTexture("MessageBox\\officer_oo.tga", 0, 0, 1, 1, 1);
	mPortraits[9][1] = CDXTexture::FindTexture("MessageBox\\officer_ee.tga", 0, 0, 1, 1, 1);
	mPortraits[9][2] = CDXTexture::FindTexture("MessageBox\\officer_mm.tga", 0, 0, 1, 1, 1);
	mPortraits[9][3] = CDXTexture::FindTexture("MessageBox\\officer_aa.tga", 0, 0, 1, 1, 1);
	mPortraits[9][4] = NULL;
	mPortraits[9][5] = NULL;
	mSpeakerIds[9] = 0x1837c4;
	mPortraits[10][0] = CDXTexture::FindTexture("MessageBox\\rad_officr_oo.tga", 0, 0, 1, 1, 1);
	mPortraits[10][1] = CDXTexture::FindTexture("MessageBox\\rad_officr_ee.tga", 0, 0, 1, 1, 1);
	mPortraits[10][2] = CDXTexture::FindTexture("MessageBox\\rad_officr_mm.tga", 0, 0, 1, 1, 1);
	mPortraits[10][3] = CDXTexture::FindTexture("MessageBox\\rad_officr_aa.tga", 0, 0, 1, 1, 1);
	mPortraits[10][4] = NULL;
	mPortraits[10][5] = NULL;
	mSpeakerIds[10] = 0x85595;
	mPortraits[11][0] = CDXTexture::FindTexture("MessageBox\\technic_oo.tga", 0, 0, 1, 1, 1);
	mPortraits[11][1] = CDXTexture::FindTexture("MessageBox\\technic_ee.tga", 0, 0, 1, 1, 1);
	mPortraits[11][2] = CDXTexture::FindTexture("MessageBox\\technic_mm.tga", 0, 0, 1, 1, 1);
	mPortraits[11][3] = CDXTexture::FindTexture("MessageBox\\technic_aa.tga", 0, 0, 1, 1, 1);
	mPortraits[11][4] = NULL;
	mPortraits[11][5] = NULL;
	mSpeakerIds[11] = 0xa13898;
	mPortraits[12][0] = CDXTexture::FindTexture("MessageBox\\pilot_oo.tga", 0, 0, 1, 1, 1);
	mPortraits[12][1] = CDXTexture::FindTexture("MessageBox\\pilot_ee.tga", 0, 0, 1, 1, 1);
	mPortraits[12][2] = CDXTexture::FindTexture("MessageBox\\pilot_mm.tga", 0, 0, 1, 1, 1);
	mPortraits[12][3] = CDXTexture::FindTexture("MessageBox\\pilot_aa.tga", 0, 0, 1, 1, 1);
	mPortraits[12][4] = NULL;
	mPortraits[12][5] = NULL;
	mSpeakerIds[12] = 0x552e30d;
	mUnknown1a4 = CDXTexture::FindTexture("MessageBox\\noisebig.tga", 0, 0, 1, 1, 1);
	mUnknown1a0 = CDXTexture::FindTexture("MessageBox\\box.tga", 0, 0, 1, 1, 1);
}

#define SAFE_RELEASE_TEXTURE(t)	if (t) { t->Release(); t = NULL; }

//*********************************************************************************
CMessageBox::~CMessageBox()	// FUNCTION: 0x004b7930
{
	for (int i = 0; i < MESSAGE_BOX_SPEAKERS; i ++)
		for (int j = 0; j < MESSAGE_BOX_FRAMES; j ++)
			SAFE_RELEASE_TEXTURE(mPortraits[i][j]);

	SAFE_RELEASE_TEXTURE(mUnknown1a4);
	SAFE_RELEASE_TEXTURE(mUnknown1a0);

	KILL_VOICE();

	mQueue.DeleteAll();
	mCurrent = NULL;
	mAllowedToPlay = TRUE;
	mCanAdvance = TRUE;
	mOpen = 0;

	KILL_VOICE();

	gVoiceName[0] = 0;
	gVoiceMessage.SetReader(NULL);
}

//*********************************************************************************
int		CMessageBox::GetPortrait(WCHAR *name)	// FUNCTION: 0x004b7ab0
{
	BOOL	found = FALSE;
	int		portrait = 2;

	for (int i = 0; i < MESSAGE_BOX_SPEAKERS; i ++)
	{
		// The two matches set the result in a block each: `portrait` then has two assignments in the loop, which keeps it in
		// a register (retail's `name` stays on the stack); one `||` condition spills `portrait` and keeps `name` in ebp.
		if (!wcscmp(name, TEXT.GetStringById(mSpeakerIds[i])))
		{
			portrait = i;
			found = TRUE;
		}
		else if ((!found) && (mSpeakerIds[i] == 0x534dfa) && (wcsstr(name, TEXT.GetStringById(0x0a336c2c))))
		{
			portrait = i;
			found = TRUE;
		}
	}

	if (!found)
		LOG.AddMessage("ERROR: no portraits for '%s'", FromWCHAR(name));

	if (portrait == 3)
		return 2;
	return portrait;
}

//*********************************************************************************
void	CMessageBox::SwitchMessagesOn()	// FUNCTION: 0x004b7b60
{
	mCanAdvance = TRUE;
	TryAdvance();
}

//*********************************************************************************
void	CMessageBox::SwitchMessagesOff()	// FUNCTION: 0x004b7b70
{
	mCanAdvance = FALSE;
}

//*********************************************************************************
void	CMessageBox::TryAdvance()	// FUNCTION: 0x004b7b80
{
	while (mQueue.Size() && (!mCurrent) && mAllowedToPlay && mCanAdvance && (GAME.GetGameState() <= GAME_STATE_PLAYING))
	{
		CMessage *m = mQueue.First();

		// a message from a speaker who has died is dropped
		if (m->mHasSpeaker && ((!m->mSpeaker.ToRead()) || m->mSpeaker->IsDying()))
		{
			mQueue.Remove(m);
			delete m;
			continue;
		}

		mCurrent = m;
		mStartTime = EVENT_MANAGER.GetTime();
		mAllowedToPlay = FALSE;
		mUnknown020 = 1.f;
		mVoiceName[0] = 0;
		mUnknown024 = FALSE;
		if (m->mSpeaker.ToRead())
			HUD.SetMessageSpeaker(m->mSpeaker);
		WCHAR *name = mCurrent->mName; mPortrait = GetPortrait(name);	// STAND-IN (order; review F 2026-10-15): the name through a local; passing mCurrent->mName loses 004b7b80
		mUnknown1ac = 0;
		mPortraitTexture = mPortraits[mPortrait][0];
		mUnknown1b4 = EVENT_MANAGER.GetTime();
		mCurrent->mEndTime = GAME.GetLevelStartTime() + EVENT_MANAGER.GetTime();
		EVENT_MANAGER.AddEvent(0.2f, MESSAGE_BOX_START, this);
		return;
	}
}

//*********************************************************************************
// STAND-IN (registers): the new message's priority is read into a local at the top of each pass. The plain test
// `message->mPriority < m->mPriority` (either way round, or with `!added` first) leaves this function 17 to 28 bytes off.
void	CMessageBox::AddMessage(CMessage *message)	// FUNCTION: 0x004b7ca0
{
	SPtrSet<CMessage>	sorted;
	BOOL				added = FALSE;
	CMessage			*m;

	FOR_ALL_ITEMS_IN(mQueue, m)
	{
		SINT priority = message->mPriority; if ((priority < m->mPriority) && (!added))	// STAND-IN (registers), see above
		{
			sorted.Append(message);
			added = TRUE;
		}
		sorted.Append(m);
	}
	if (!added)
		sorted.Append(message);

	mQueue = sorted;

	if ((mQueue.Size() == 1) && (mAllowedToPlay == TRUE))
		TryAdvance();
}

//*********************************************************************************
void	CMessageBox::PlayPendingSamples()	// FUNCTION: 0x004b7d90
{
	if (GAME.IsPaused())
		return;
	if (GAME.GetGameState() > GAME_STATE_PLAYING)
		return;
	if (!gVoiceName[0])
		return;
	if (gVoiceLoader.IsBusy())
		return;

	if (gVoiceSample)
	{
		delete gVoiceSample;
		gVoiceSample = NULL;
	}

	if (!gVoiceLoader.mSize)
	{
		if (gFileReadFailed)
			CONSOLE.FatalFileError(gVoiceName, -1);
		return;
	}

	gVoiceLoader.Lock();
	gVoiceSample = SOUND.CreateSampleFromData(gVoiceLoader.mBuffer, gVoiceLoader.mSize, FALSE, NULL);
	gVoiceLoader.mSize = 0;
	gVoiceLoader.Unlock();

	if (gVoiceSample)
		SOUND.PlaySample(gVoiceSample, gVoiceMessage.ToRead(), SOUND.GetRadioMessageVolume(), ST_NOTRACKING, false, 0, 0.f, -1.f, false, 1.f, TRUE, TRUE, ST_GAME_SOUND);

	gVoiceName[0] = 0;
	gVoiceMessage.SetReader(NULL);
}

//*********************************************************************************
// Near miss (4 bytes): one scratch-register rotation in the no-sample branch. Passing `id` to the error message keeps it
// live across the calls (88 bytes); the permuter found nothing in 2,908 tries.
void	CMessageBox::StartMessage()	// FUNCTION: 0x004b7ea0
{
	if (mCurrent->mHasSpeaker && ((!mCurrent->mSpeaker.ToRead()) || mCurrent->mSpeaker->IsDying()))
	{
		mUnknown020 = 1.f;
		mUnknown024 = TRUE;
		EVENT_MANAGER.AddEvent(0.5f, MESSAGE_BOX_FINISH, this);
		return;
	}

	SINT id = mCurrent->mStringId; char *sound = TEXT.GetSoundName(id);	// the id is read into a local first (a value copy: retail's registers)
	if (sound)
	{
		sprintf(mVoiceName, "%s\\messagebox\\%s", TEXT.GetLanguageName(), sound);
		CMessage *message = mCurrent;
		if (SOUND.IsInitialised())
		{
			char path[260];
			sprintf(path, "data\\sounds\\%s.ogg", mVoiceName);
			strcpy(gVoiceName, mVoiceName);
			gVoiceMessage.SetReader(message);
			gVoiceLoader.Acquire();
			strcpy(gVoiceLoader.mFileName, path);
			gVoiceLoader.mSize = 0;
			gVoiceLoader.StartAsync();
		}
	}
	else
	{
		SINT	messageId = mCurrent->mStringId;		// read into locals first: retail's scratch registers rotate one place (eax is reused for the text)
		WCHAR	*messageText = mCurrent->mText;
		LOG.AddMessage("ERROR:(MB) No sample for message '%s' id = %d", FromWCHAR(messageText), messageId);
		mCurrent->mFinishedPlaying = TRUE;
	}

	AdvanceReveal();
}

//*********************************************************************************
void	CMessageBox::AdvanceReveal()	// FUNCTION: 0x004b8020
{
	CMessage *m = mCurrent;
	if (m->SpeakerGone())
	{
		mUnknown020 = 1.f;
		if (m->mRevealed < (int)wcslen(m->mText))
		{
			StopCurrentPlayingSound();
			mCurrent->mDots ++;
			if (mCurrent->mDots < 5)
			{
				EVENT_MANAGER.AddEvent(0.038f, MESSAGE_BOX_REVEAL, this);
				return;
			}
			mUnknown024 = TRUE;
			EVENT_MANAGER.AddEvent(2.7f, MESSAGE_BOX_FINISH, this);
			return;
		}
		mUnknown024 = TRUE;
	}

	m = mCurrent;
	m->RevealLetter();
	m->RevealLetter();

	if (mCurrent->mDuration + mStartTime + 0.2f > EVENT_MANAGER.GetTime())
	{
		EVENT_MANAGER.AddEvent(0.038f, MESSAGE_BOX_REVEAL, this);
		return;
	}

	if ((mCurrent->mFinishedPlaying != TRUE) && (EVENT_MANAGER.GetTime() - mStartTime <= 12.f))
	{
		EVENT_MANAGER.AddEvent(0.038f, MESSAGE_BOX_REVEAL, this);
		return;
	}

	mUnknown020 = 1.f;
	mUnknown024 = TRUE;
	EVENT_MANAGER.AddEvent(0.3f, MESSAGE_BOX_FINISH, this);
}

//*********************************************************************************
void	CMessageBox::HandleEvent(CEvent *event)	// FUNCTION: 0x004b81d0
{
	switch (event->GetEventNum())
	{
	case MESSAGE_BOX_START:
		StartMessage();
		break;

	case MESSAGE_BOX_REVEAL:
		AdvanceReveal();
		break;

	case MESSAGE_BOX_FINISH:
	{
		CMessage *m = mCurrent;
		if (m->mEvent)
		{
			EVENT_MANAGER.AddEvent(m->mEvent);
			m->mEvent = NULL;
		}
		mQueue.Remove(mCurrent);
		GAME.GetMessageLog()->AddMessage(mCurrent);
		mCurrent = NULL;
		EVENT_MANAGER.AddEvent(0.2f, MESSAGE_BOX_NEXT, this);
		break;
	}

	case MESSAGE_BOX_NEXT:
	case ALLOWED_TO_PLAY_MESSAGES:
		mAllowedToPlay = TRUE;
		TryAdvance();
		break;
	}
}

//*********************************************************************************
CDXTexture	*CMessageBox::GetPortraitTexture(int n)	// FUNCTION: 0x004b82a0
{
	return mPortraits[n][2];
}

void	CMessageBox::RenderBattleLinePulse(float x, float y, int a)	// FUNCTION: 0x004b82b0
{
	if (mCurrent)
	{
		int		seed = mRandom.mValue;

		LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		RENDERINFO.Apply();

		float	level = mUnknown020;
		if ((mRandom.Next() % 200) / 100.0f < mUnknown020)
		{
			level = mUnknown020 + 0.2f;
			if (level < 1.0f)
				level = 1.0f;
		}

		if (mPortrait == 5)
		{
			if (mUnknown1b4 + 0.05f < EVENT_MANAGER.GetTime())
			{
				mUnknown1b4 = EVENT_MANAGER.GetTime();
				if ((mRandom.Next() % 2) == 0)
				{
					if ((mRandom.Next() % 7) == 0)
						mUnknown1ac = mRandom.Next() % 5;
					if ((mUnknown1ac % 2) == 0)
					{
						mUnknown1ac ++;
						mPortraitTexture = mPortraits[mPortrait][mUnknown1ac];
					}
					else
					{
						mUnknown1ac --;
						mPortraitTexture = mPortraits[mPortrait][mUnknown1ac];
					}
				}
			}
		}
		else if (mUnknown1b4 + 0.05f < EVENT_MANAGER.GetTime())
		{
			mUnknown1b4 = EVENT_MANAGER.GetTime();
			if ((mRandom.Next() % 2) == 0)
			{
				int		old = mUnknown1ac;
				do
				{
					float	r = (mRandom.Next() & 0xff) * (100.0 / 256.0);
					if (r < 8.0f)
						mUnknown1ac = 0;
					else if (r < 20.0f)
						mUnknown1ac = 1;
					else if (r < 60.0f)
						mUnknown1ac = 2;
					else
						mUnknown1ac = 3;
				} while (old == mUnknown1ac);
				mPortraitTexture = mPortraits[mPortrait][mUnknown1ac];
			}
		}

		if (!mUnknown024)
		{
			int		alpha = 0x40;
			if (level > 0.3f)
				alpha = mRandom.Next() % 64;

			float	v = 0;
			if (level > 0.1f)
				v = level * ((mRandom.Next() % 100) / 300.0f);
			float	v1 = v + 1.0f;
			float	u = 0;
			if (level > 0.1f)
				u = level * ((mRandom.Next() % 100) / 500.0f);
			float	u1 = u + 1.0f;

			DWORD	col = (alpha << 24) + 0xffffff;
			for (int i = 5; i >= 0; i --)
			{
				float	su0 = u;
				float	su1 = u1;
				if (level > 0.2)
				{
					float	s = Clamped(level, 0.5f, level);
					if ((i % 2) == 0)
					{
						float	start = u + s * i * 0.16f;
						su0 = start;
						su1 = start + 1.0f;
					}
					else
					{
						su0 = u - s * i * 0.16f;
						su1 = 1.0f - s * i * 0.16f + u;
					}
				}
				if (mPortraitTexture)
					CDXSpriteRenderer::DrawSprite(x + 16.0f, y - 16.0f, a / 1000.0f, mPortraitTexture, (ESpriteAlignment)2, SPT_NONE, 1.0f, 0,
												  col, 0.75f, 0.75f, su0, su1, v, v1);
			}
		}

		float	u = (mRandom.Next() % 100) / 100.0f;
		float	v = (mRandom.Next() % 100) / 100.0f;
		SINT	alpha = SINT(((mRandom.Next() % 10) + 40) * (level + 1.5f));
		CDXSpriteRenderer::DrawSprite(x, y, a / 1000.0f, mUnknown1a4, (ESpriteAlignment)2, SPT_NONE, 1.0f, 0,
									  (alpha << 24) + 0xffffff, 1.0f, 1.0f, u, u + 1.0f, v, v + 1.0f);

		float	target = mCurrent->mDelay;
		if ((!mUnknown024) && (level > target) && (!GAME.IsPaused()))
		{
			mUnknown020 -= (60.0f / PLATFORM.GetFPS()) * 0.06f;
			if (mUnknown020 < target)
				mUnknown020 = target;
		}
		if (GAME.IsPaused())
			mRandom.mValue = seed;
	}
}

#pragma auto_inline(off)
void	CMessageBox::StopCurrentPlayingSound()	// FUNCTION: 0x004b8800
{
	if (!CLIPARAMS.mUnknown18c)
	{
		if (mVoiceName[0])
			mVoiceName[0] = 0;
		KILL_VOICE();
	}
}

// The message panel's layout (0062ce5c..0062ce6c; names ours), defined in Hud.cpp: retail places these five .data words
// directly before HUD_SAFE_AREA (0062ce74), where the link puts Hud.cpp's data (MessageBox.cpp's would follow the memory manager's).
extern int		gMessageWrapWidth;
extern float	gMessageBoxY;
extern float	gMessageBoxX;
extern float	gMessageBoxWidth;
extern float	gMessageTextY;

//******************************************************************************************
// The message panel: it opens (to 1, 0.4 a second) and then shows its text (easing to 2) while there is a message, and
// shuts the same way once the queue is empty; the frame is the HUD's objective bar, and the text five wrapped lines.
// The brackets matter: VC6 reorders an unbracketed chain of terms by where each was first read in the function and keeps
// a bracketed group as written, which gives retail's order (the HUD's offsets first, then the message box's own layout
// values). Each bracket is also an IL tuple: the one round the y subtraction puts its store between SetFogEnabled(true)'s
// two byte stores.
void	CMessageBox::RenderOverlay(int a)	// FUNCTION: 0x004b8850
{
	if (GAME.IsGameFinished())
	{
		StopCurrentPlayingSound();
		return;
	}

	if (!mCurrent)
	{
		if (mOpen > 1.0f)
		{
			if (!mQueue.Size())
			{
				float	step = GAME.GetRenderFrameLength() * 0.5f;
				BOOL	opening;
				float	dir;
				if (mOpen < 1.0f)
				{
					dir = 0.01f;
					opening = TRUE;
				}
				else
				{
					dir = -0.01f;
					opening = FALSE;
				}
				mOpen += (mOpen - 2.0f + dir) * step;
				if (opening)
				{
					if (mOpen > 1.0f)
						mOpen = 1.0f;
				}
				else if (mOpen < 1.0f)
					mOpen = 1.0f;
			}
		}
		else
		{
			mOpen -= GAME.GetRenderFrameLength() * 0.4f;
			if (mOpen < 0)
				mOpen = 0;
		}
		if (mOpen == 0)
			return;
	}
	else
	{
		if (mOpen < 1.0f)
		{
			mOpen += GAME.GetRenderFrameLength() * 0.4f;
			if (mOpen > 1.0f)
				mOpen = 1.0f;
		}
		else
		{
			float	f = GAME.GetRenderFrameLength() * 0.5f;
			SmoothResize(mOpen, 2.0f, f, 0.01f);
		}
	}

	float	y;
	if (GAME.IsMultiplayer())
		y = PLATFORM.GetWindowHeight() * 0.5f + 5.0f;
	else
		y = (HUD.mViewportBottom + 16.0f - HUD_OFFSET_Y - 128.0f + 48.0f) + gMessageBoxY;
	float	width = 230.0f - HUD.mViewportX * 2 - HUD_OFFSET_X * 2 + gMessageBoxWidth;
	float	open = mOpen;
	if (open < 1.0f)
		HUD.RenderObjectiveBar((HUD.mViewportWidth * 0.5f + HUD.mViewportX) + gMessageBoxX + 12.0f, y, width, 60.0f, open);
	else
		HUD.RenderObjectiveBar((HUD.mViewportWidth * 0.5f + HUD.mViewportX) + gMessageBoxX + 12.0f, y, (mOpen - 1.0f) * width, 60.0f, 1.0f);

	if ((mCurrent) && (!(mOpen < 2.0f)))
	{
		WCHAR	lines[8][0x56];
		mCurrent->WordWrap(lines[0], gMessageWrapWidth, mUnknown1bc);

		if (GAME.IsMultiplayer())
			y = PLATFORM.GetWindowHeight() * 0.5f + 5.0f;
		else
			y = (HUD.mViewportBottom + 16.0f - HUD_OFFSET_Y - 128.0f + 48.0f) + gMessageBoxY;
		y = (y - gMessageTextY);

		RENDERINFO.SetFogEnabled(true);
		RENDERINFO.Apply();
		PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow((HUD.mViewportWidth * 0.5f + HUD.mViewportX) + gMessageBoxX + 12.0f - (230.0f - HUD.mViewportX * 2 - HUD_OFFSET_X * 2 + gMessageBoxWidth) * 0.5f - 12.0f,
														y, 0xffffffff, L"  ", 0, 1.0f, 1.0f, 1.0f);

		for (int i = 0; i < 5; i++)
		{
			RENDERINFO.EnableLighting(false);
			RENDERINFO.SetFogEnabled(false);
			RENDERINFO.Apply();
			LT.D3D_ValidateDevice();
			PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow((HUD.mViewportWidth * 0.5f + HUD.mViewportX) + gMessageBoxX + 12.0f - (230.0f - HUD.mViewportX * 2 - HUD_OFFSET_X * 2 + gMessageBoxWidth) * 0.5f - 12.0f,
															i * 15 + y, 0xffffffff, lines[i], 0, 1.0f, 1.0f, 1.0f);
		}
	}
}
#pragma auto_inline(on)

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004b6d90 ?Run@COggLoader@@UAEXXZ
