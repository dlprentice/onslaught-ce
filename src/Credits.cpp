// Credits.cpp: the scrolling credits (CGame::RollCredits and the front end's credits page draw them). Not in
// Stuart's release: reconstructed from retail (005180b0..0051a1e3).

#include "common.h"

#include	"Credits.h"
#include	"Platform.h"
#include	"renderinfo.h"

extern bool	gInFrontEnd;					// 00679b40 (FrontEnd.cpp)
extern int	gAmericanText;					// 0083d990 (frontend.h)

// The titles of the Lost Toys and publisher sections.
// FUNCTION: 0x005180b0
WCHAR *GetCreditTitle(int id)
{
	switch (id)
	{
	case 0:	return ToWCHAR("Designed and Developed by Lost Toys:");
	case 1:	return ToWCHAR("Project Manager");
	case 2:	return ToWCHAR("Creative Director");
	case 3:	return ToWCHAR("Technical Director");
	case 4:	return ToWCHAR("Lead Design");
	case 5:	return ToWCHAR("Design");
	case 6:	return ToWCHAR("Lead Game Programming");
	case 7:	return ToWCHAR("Programming");
	case 8:	return ToWCHAR("Additional Programming");
	case 9:	return ToWCHAR("Lead Artist");
	case 10:	return ToWCHAR("Art");
	case 11:	return ToWCHAR("Additional Art");
	case 12:	return ToWCHAR("Graphic Design");
	case 13:	return ToWCHAR("Storyboard Artist");
	case 14:	return ToWCHAR("Music and Sound");
	case 15:	return ToWCHAR("Music");
	case 16:	return ToWCHAR("Sound Effects");
	case 17:	return ToWCHAR("Saxophone");
	case 18:	return ToWCHAR("Voice Talent");
	case 20:	return ToWCHAR("Thanks");
	case 21:	return ToWCHAR("Infogrames:");
	case 22:	return ToWCHAR("Infogrames Producer (CORE)");
	case 23:	return ToWCHAR("Production Manager (CORE)");
	case 24:	return ToWCHAR("Infogrames Europe:");
	case 25:	return ToWCHAR("VP European Marketing");
	case 26:	return ToWCHAR("European Marketing Director");
	case 27:	return ToWCHAR("Marketing Manager");
	case 28:	return ToWCHAR("Product Manager");
	case 29:	return ToWCHAR("Core Games Head of Communication");
	case 30:	return ToWCHAR("Gameplay Evaluation Service:");
	case 31:	return ToWCHAR("Manager");
	case 32:	return ToWCHAR("Senior Gameplay Analyst");
	case 33:	return ToWCHAR("Gameplay Analysts");
	case 35:	return ToWCHAR("Infogrames QA:");
	case 36:	return ToWCHAR("QA Group Manager");
	case 37:	return ToWCHAR("QA Manager (Sheffield)");
	case 38:	return ToWCHAR("Quality Manager (Lyon)");
	case 39:	return ToWCHAR("Sheffield QA");
	case 40:	return ToWCHAR("Test Supervisor");
	case 41:	return ToWCHAR("Pre-mastering Technician");
	case 42:	return ToWCHAR("Lead Tester");
	case 43:	return ToWCHAR("Assistant Lead Tester");
	case 34: case 44:	return ToWCHAR("Testers");
	case 45:	return ToWCHAR("Localisation:");
	case 46:	return ToWCHAR("Localisation & Translators");
	case 47:	return ToWCHAR("Localisation Support Group Manager");
	case 48:	return ToWCHAR("Localisation Coordination");
	case 49:	return ToWCHAR("Localisation Technical Consultant");
	case 50:	return ToWCHAR("Translation");
	case 51:	return ToWCHAR("Localisation Testing");
	case 52:	return ToWCHAR("Certification & Planning Support Group:");
	case 53:	return ToWCHAR("Infogrames US:");
	case 54:	return ToWCHAR("Re-Publishing Producer");
	case 55:	return ToWCHAR("Marketing:");
	case 56:	return ToWCHAR("Brand Manager");
	case 57:	return ToWCHAR("Director of Marketing");
	case 58:	return ToWCHAR("Vice President of Marketing");
	case 59:	return ToWCHAR("Studio Sr. Vice President");
	case 60:	return ToWCHAR("VP Product Development");
	case 61:	return ToWCHAR("Director of Creative Services");
	case 62:	return ToWCHAR("Creative Services");
	case 63:	return ToWCHAR("Director of Marketing Communications");
	case 64:	return ToWCHAR("Creative Services Manager");
	case 65:	return ToWCHAR("Strategic Relations Sr. Manager");
	case 66:	return ToWCHAR("PR Manager");
	case 67:	return ToWCHAR("PR Specialist");
	case 19: case 68:	return ToWCHAR("Special Thanks");
	case 69:	return ToWCHAR("Additional Testing");
	case 70:	return ToWCHAR("Sound Engineering");
	default:	return ToWCHAR("********************");
	};
}

// The same titles for the other section type; American text spells Localization with a z.
// FUNCTION: 0x005185c0
WCHAR *GetCreditTitleUS(int id)
{
	if (gAmericanText)
	{
		switch (id)
		{
		case 45:	return ToWCHAR("Localization:");
		case 46:	return ToWCHAR("Localization & Translators");
		case 47:	return ToWCHAR("Localization Support Group Manager");
		case 48:	return ToWCHAR("Localization Coordination");
		case 49:	return ToWCHAR("Localization Technical Consultant");
		case 51:	return ToWCHAR("Localization Testing");
		};
	}

	switch (id)
	{
	case 0:	return ToWCHAR("Designed and Developed by Lost Toys:");
	case 1:	return ToWCHAR("Project Manager");
	case 2:	return ToWCHAR("Creative Director");
	case 3:	return ToWCHAR("Technical Director");
	case 4:	return ToWCHAR("Lead Design");
	case 5:	return ToWCHAR("Design");
	case 71:	return ToWCHAR("Lead Technical Programming");
	case 6:	return ToWCHAR("Lead Game Programming");
	case 7:	return ToWCHAR("Programming");
	case 8:	return ToWCHAR("Additional Programming");
	case 9:	return ToWCHAR("Lead Artist");
	case 10:	return ToWCHAR("Art");
	case 11:	return ToWCHAR("Additional Art");
	case 12:	return ToWCHAR("Graphic Design");
	case 13:	return ToWCHAR("Storyboard Artist");
	case 14:	return ToWCHAR("Music and Sound");
	case 15:	return ToWCHAR("Music");
	case 16:	return ToWCHAR("Sound Effects");
	case 17:	return ToWCHAR("Saxophone");
	case 18:	return ToWCHAR("Voice Talent");
	case 20:	return ToWCHAR("Thanks");
	case 21:	return ToWCHAR("Infogrames:");
	case 22:	return ToWCHAR("Infogrames Producer (CORE)");
	case 23:	return ToWCHAR("Production Manager (CORE)");
	case 24:	return ToWCHAR("Infogrames Europe:");
	case 25:	return ToWCHAR("VP European Marketing");
	case 26:	return ToWCHAR("European Marketing Director");
	case 27:	return ToWCHAR("Marketing Manager");
	case 28:	return ToWCHAR("Product Manager");
	case 29:	return ToWCHAR("Core Games Head of Communication");
	case 30:	return ToWCHAR("Gameplay Evaluation Service:");
	case 31:	return ToWCHAR("Manager");
	case 32:	return ToWCHAR("Senior Gameplay Analyst");
	case 33:	return ToWCHAR("Gameplay Analysts");
	case 35:	return ToWCHAR("Infogrames QA:");
	case 36:	return ToWCHAR("QA Group Manager");
	case 37:	return ToWCHAR("QA Manager (Sheffield)");
	case 38:	return ToWCHAR("Quality Manager (Lyon)");
	case 39:	return ToWCHAR("Sheffield QA");
	case 40:	return ToWCHAR("Test Supervisor");
	case 41:	return ToWCHAR("Pre-mastering Technician");
	case 42:	return ToWCHAR("Lead Tester");
	case 43:	return ToWCHAR("Assistant Lead Tester");
	case 34: case 44:	return ToWCHAR("Testers");
	case 45:	return ToWCHAR("Localisation:");
	case 46:	return ToWCHAR("Localisation & Translators");
	case 47:	return ToWCHAR("Localisation Support Group Manager");
	case 48:	return ToWCHAR("Localisation Coordination");
	case 49:	return ToWCHAR("Localisation Technical Consultant");
	case 50:	return ToWCHAR("Translation");
	case 51:	return ToWCHAR("Localisation Testing");
	case 52:	return ToWCHAR("Certification & Planning Support Group:");
	case 53:	return ToWCHAR("Infogrames US:");
	case 54:	return ToWCHAR("Re-Publishing Producer");
	case 55:	return ToWCHAR("Marketing:");
	case 56:	return ToWCHAR("Brand Manager");
	case 57:	return ToWCHAR("Director of Marketing");
	case 58:	return ToWCHAR("Vice President of Marketing");
	case 59:	return ToWCHAR("Studio Sr. Vice President");
	case 60:	return ToWCHAR("VP Product Development");
	case 61:	return ToWCHAR("Director of Creative Services");
	case 62:	return ToWCHAR("Creative Services");
	case 63:	return ToWCHAR("Director of Marketing Communications");
	case 64:	return ToWCHAR("Creative Services Manager");
	case 65:	return ToWCHAR("Strategic Relations Sr. Manager");
	case 66:	return ToWCHAR("PR Manager");
	case 67:	return ToWCHAR("PR Specialist");
	case 19: case 68:	return ToWCHAR("Special Thanks");
	case 69:	return ToWCHAR("Additional Testing");
	case 70:	return ToWCHAR("Sound Engineering");
	case 100:	return ToWCHAR("QA Supervisors");
	default:	return ToWCHAR("********************");
	};
}

// A credits line: a section (2 and 3 look their titles up), a title id or a text, and a style (0 heading, 1 title,
// 2 name, 3 the end of the list).
struct CCreditEntry
{
	// FUNCTION: 0x00519ff0 ??0CCreditEntry@@QAE@HHH@Z
	CCreditEntry(int section, int id, int style) { mSection = section; mId = id; mText = NULL; mStyle = style; }
	// FUNCTION: 0x0051a010 ??0CCreditEntry@@QAE@HPADH@Z
	CCreditEntry(int section, char *text, int style) { mSection = section; mId = -1; mText = text; mStyle = style; }
	CCreditEntry(int section, int id, char *text, int style) { mSection = section; mId = id; mText = text; mStyle = style; }

	int		mSection;		// +0x00
	int		mId;			// +0x04: -1 for a text
	char	*mText;			// +0x08
	int		mStyle;			// +0x0c
};
CCredits CREDITS;		// 00897a88: defined before gCredits, so it comes after it in .bss (placement evidence)
// FUNCTION: 0x00518be0 init:gCredits
// FUNCTION: 0x00518bf0 initbody:gCredits
static CCreditEntry gCredits[] =
{
	CCreditEntry(2, 0, 0),
	CCreditEntry(2, 1, 1),
	CCreditEntry(2, "Jeremy Longley", 2),
	CCreditEntry(2, 2, 1),
	CCreditEntry(2, "Darran Thomas", 2),
	CCreditEntry(2, 3, 1),
	CCreditEntry(2, "Glenn Corpes", 2),
	CCreditEntry(1, "PC Version Programming", 1),
	CCreditEntry(1, "Jan Svarovsky", 2),
	CCreditEntry(1, "PC Version Test", 1),
	CCreditEntry(1, "Jim Thompson", 2),
	CCreditEntry(2, 4, 1),
	CCreditEntry(2, "Alex Trowers", 2),
	CCreditEntry(2, 5, 1),
	CCreditEntry(2, "Jim Thompson", 2),
	CCreditEntry(2, 6, 1),
	CCreditEntry(2, "Stuart Gillam", 2),
	CCreditEntry(2, 7, 1),
	CCreditEntry(2, "Glenn Corpes", 2),
	CCreditEntry(2, "John Treece-Birch", 2),
	CCreditEntry(2, "Ben Carter", 2),
	CCreditEntry(2, "Richard Smith", 2),
	CCreditEntry(2, "Jeremy Longley", 2),
	CCreditEntry(2, 8, 1),
	CCreditEntry(2, "Mark Feldman", 2),
	CCreditEntry(2, "Jan Svarovsky", 2),
	CCreditEntry(2, "Matt Taylor", 2),
	CCreditEntry(2, 9, 1),
	CCreditEntry(2, "Jeremy Elford", 2),
	CCreditEntry(2, 10, 1),
	CCreditEntry(2, "Darran Thomas", 2),
	CCreditEntry(2, "Neil Gartell", 2),
	CCreditEntry(2, "Dylan Murray", 2),
	CCreditEntry(2, "Chris Young", 2),
	CCreditEntry(2, "David Cathro", 2),
	CCreditEntry(2, "Nick McMahon", 2),
	CCreditEntry(2, "Mike McCarthy", 2),
	CCreditEntry(2, "Alex Cave", 2),
	CCreditEntry(2, "Kate Kerrigan", 2),
	CCreditEntry(2, "Michael Dove", 2),
	CCreditEntry(2, 11, 1),
	CCreditEntry(2, "Mark Pitcher", 2),
	CCreditEntry(2, "Edmund Perryman", 2),
	CCreditEntry(2, "Ron Ong", 2),
	CCreditEntry(2, "Joel Kemp", 2),
	CCreditEntry(2, "Chris Hill", 2),
	CCreditEntry(2, "Christian Bradley", 2),
	CCreditEntry(2, 12, 1),
	CCreditEntry(2, "Tze Lim of Subliminal Influence", 2),
	CCreditEntry(2, 13, 1),
	CCreditEntry(2, "Neil Maguire", 2),
	CCreditEntry(2, 14, 1),
	CCreditEntry(2, "Nathan McCree Music Production Limited", 2),
	CCreditEntry(2, 15, 1),
	CCreditEntry(2, "Nathan McCree", 2),
	CCreditEntry(2, 16, 1),
	CCreditEntry(2, "Nathan McCree & Gabriel Wetz", 2),
	CCreditEntry(2, 70, 1),
	CCreditEntry(2, "Matt Kemp", 2),
	CCreditEntry(2, 17, 1),
	CCreditEntry(2, "Gabriel Wetz", 2),
	CCreditEntry(2, 18, 1),
	CCreditEntry(2, "Brad Lavelle", 2),
	CCreditEntry(2, "Rob Brown", 2),
	CCreditEntry(2, "Amanda Mealing", 2),
	CCreditEntry(2, "Sean Chapman", 2),
	CCreditEntry(2, 19, 1),
	CCreditEntry(2, "Les Edgar, John Cook, Alison Beasley,", 2),
	CCreditEntry(2, "and Mucky Foot for cardid.*", 2),
	CCreditEntry(2, 20, 1),
	CCreditEntry(2, "Mike Man, Andy Trowers, Ben Board,", 2),
	CCreditEntry(2, "Si Handby, Martin Griffiths, Morten Pedersen,", 2),
	CCreditEntry(2, "Jenny Graham, Philip Elford, Oliver Hecks,", 2),
	CCreditEntry(2, "Mike Pawley and everyone on GGDev", 2),
	CCreditEntry(2, "Uses Bink Video.", 1),
	CCreditEntry(2, "Copyright (C) 1997-2003 by RAD Game Tools, Inc.", 1),
	CCreditEntry(1, "RelQ / International Testing SA", 0),
	CCreditEntry(1, 0, "Project Manager", 1),
	CCreditEntry(1, "Malek Agha", 2),
	CCreditEntry(1, 0, "Production Manager", 1),
	CCreditEntry(1, "Minh Tadang", 2),
	CCreditEntry(1, 0, "Operations Manager", 1),
	CCreditEntry(1, "Guillaume Fournier", 2),
	CCreditEntry(1, 0, "Testers", 1),
	CCreditEntry(1, "Julien \"Nemo\" Chabrot", 2),
	CCreditEntry(1, "Nicolas \"The Dwarf\" Poissenot", 2),
	CCreditEntry(1, "Fuat \"Fut\" Kaygisiz", 2),
	CCreditEntry(1, "Yves \"DarkZagor\" Zagatti", 2),
	CCreditEntry(1, 0, "System Administrator", 1),
	CCreditEntry(1, "Stephane \"Steffdemon\" Benard", 2),
	CCreditEntry(1, 0, "Special thanks", 1),
	CCreditEntry(1, "Fabrice \"Grand Fromage\" Drevon, Delphine Delamare", 2),
	CCreditEntry(2, 21, 0),
	CCreditEntry(2, 22, 1),
	CCreditEntry(2, "Judi Spiers", 2),
	CCreditEntry(2, 23, 1),
	CCreditEntry(2, "Ivan Davies", 2),
	CCreditEntry(2, 24, 0),
	CCreditEntry(2, 25, 1),
	CCreditEntry(2, "Larry Sparks", 2),
	CCreditEntry(2, 26, 1),
	CCreditEntry(2, "Frank Heissat", 2),
	CCreditEntry(2, 27, 1),
	CCreditEntry(2, "Cindy Church", 2),
	CCreditEntry(2, 28, 1),
	CCreditEntry(2, "Oliver Green", 2),
	CCreditEntry(2, 29, 1),
	CCreditEntry(2, "Lynn Daniel", 2),
	CCreditEntry(2, 30, 1),
	CCreditEntry(2, 31, 1),
	CCreditEntry(2, "Dominique Morel", 2),
	CCreditEntry(2, 32, 1),
	CCreditEntry(2, "Emmanuel Cholley", 2),
	CCreditEntry(2, 33, 1),
	CCreditEntry(2, "William Jaulain", 2),
	CCreditEntry(2, "Yan Schied", 2),
	CCreditEntry(2, 34, 1),
	CCreditEntry(2, "Fabien Lauton", 2),
	CCreditEntry(2, "Alexis Recoup\351", 2),
	CCreditEntry(2, "Christophe Chabal", 2),
	CCreditEntry(2, "G\351rard Barnaud", 2),
	CCreditEntry(2, "Gregory Eche", 2),
	CCreditEntry(2, 35, 0),
	CCreditEntry(2, 36, 1),
	CCreditEntry(2, "Jean-Marcel Nicolai", 2),
	CCreditEntry(2, 37, 1),
	CCreditEntry(2, "Lewis Glover", 2),
	CCreditEntry(2, 38, 1),
	CCreditEntry(2, "Olivier Robin", 2),
	CCreditEntry(2, 39, 0),
	CCreditEntry(2, 40, 1),
	CCreditEntry(2, "Rob Lunt", 2),
	CCreditEntry(2, 41, 1),
	CCreditEntry(2, "Chris Greatbatch", 2),
	CCreditEntry(2, 42, 1),
	CCreditEntry(2, "Sam Attenborough", 2),
	CCreditEntry(2, 43, 1),
	CCreditEntry(2, "Jody Hindle", 2),
	CCreditEntry(2, 44, 1),
	CCreditEntry(2, "Jay Hartley", 2),
	CCreditEntry(2, "Stef Reali", 2),
	CCreditEntry(2, "Ian Whitaker", 2),
	CCreditEntry(2, "Wayne Adkin", 2),
	CCreditEntry(2, "Elton Broadhead", 2),
	CCreditEntry(2, 69, 1),
	CCreditEntry(2, "Sam Hindle", 2),
	CCreditEntry(2, 45, 0),
	CCreditEntry(2, 46, 1),
	CCreditEntry(2, "Sylviane Pivot-Chossat", 2),
	CCreditEntry(2, "Sarah Bennett", 2),
	CCreditEntry(2, 47, 1),
	CCreditEntry(2, "Sylviane Pivot-Chossat", 2),
	CCreditEntry(2, 48, 1),
	CCreditEntry(2, "Maud Favier", 2),
	CCreditEntry(2, "Sarah Bennett", 2),
	CCreditEntry(2, "Diane Delaye", 2),
	CCreditEntry(2, "Weronika Larsson", 2),
	CCreditEntry(2, 49, 1),
	CCreditEntry(2, "Fabien Roset", 2),
	CCreditEntry(2, 50, 1),
	CCreditEntry(2, "KBP", 2),
	CCreditEntry(2, "Synthesis/Interference", 2),
	CCreditEntry(2, 51, 1),
	CCreditEntry(2, "Babel Media Ltd.", 2),
	CCreditEntry(2, 52, 1),
	CCreditEntry(2, "Emilie Ballivy", 2),
	CCreditEntry(2, "J\351r\364me Di Tullio", 2),
	CCreditEntry(2, "Caroline Fauchille", 2),
	CCreditEntry(2, "Rebecka Pernered", 2),
	CCreditEntry(2, 53, 0),
	CCreditEntry(2, 54, 1),
	CCreditEntry(2, "Dave T Brown", 2),
	CCreditEntry(2, 55, 1),
	CCreditEntry(2, 56, 1),
	CCreditEntry(2, "Tom Richardson", 2),
	CCreditEntry(2, 57, 1),
	CCreditEntry(2, "Jean Raymond", 2),
	CCreditEntry(2, 58, 1),
	CCreditEntry(2, "Steve Allison", 2),
	CCreditEntry(2, 59, 1),
	CCreditEntry(2, "Jean-Philippe Agati", 2),
	CCreditEntry(2, 60, 1),
	CCreditEntry(2, "Steve Ackrich", 2),
	CCreditEntry(2, 61, 1),
	CCreditEntry(2, "David Gaines", 2),
	CCreditEntry(2, 62, 1),
	CCreditEntry(2, "Larry Fukuoka", 2),
	CCreditEntry(2, 63, 1),
	CCreditEntry(2, "Kristine Keever", 2),
	CCreditEntry(2, 64, 1),
	CCreditEntry(2, "Cecelia Hernandez", 2),
	CCreditEntry(2, 65, 1),
	CCreditEntry(2, "Joy Schneer", 2),
	CCreditEntry(2, 66, 1),
	CCreditEntry(2, "Matt Frary", 2),
	CCreditEntry(2, 67, 1),
	CCreditEntry(2, "Wiebke Vallentin", 2),
	CCreditEntry(1, "Xbox Test", 0),
	CCreditEntry(2, 42, 1),
	CCreditEntry(2, "Helen Hinchcliffe", 2),
	CCreditEntry(2, 43, 1),
	CCreditEntry(2, "Andrew Lemat", 2),
	CCreditEntry(2, 44, 1),
	CCreditEntry(2, "Arif Sinan, Arthur Long, William Deets,", 2),
	CCreditEntry(2, "Chris Sur, Michael Shamid-deen, Ken Moodie", 2),
	CCreditEntry(1, "PS2 Test", 0),
	CCreditEntry(3, 42, 1),
	CCreditEntry(3, "Pepe Jauregui", 2),
	CCreditEntry(3, 43, 1),
	CCreditEntry(3, "Wilfredo Dimas", 2),
	CCreditEntry(3, 100, 1),
	CCreditEntry(3, "Chuck Nunez, Jeff Loney, Donny Clay", 2),
	CCreditEntry(3, 44, 1),
	CCreditEntry(3, "Corey Eiland, Kory O' Daniels, Ken Moodie,", 2),
	CCreditEntry(3, "Mark Florentino, Renee Barber, Ted Tomasko,", 2),
	CCreditEntry(3, "Michael Shamsid-Deen, Jeff Kelm, Lupe De Leon,", 2),
	CCreditEntry(3, "Jorge Oseguera", 2),
	CCreditEntry(2, 68, 1),
	CCreditEntry(2, "Patty, L Rivas, K Edwards, S 'Beany' Monroe,", 2),
	CCreditEntry(2, "M Gilmartin, Sunnyvale QA, D Clay, J Loney,", 2),
	CCreditEntry(2, "G Marquez, T Hess, Kirby, J Koronaios, Matt and Gayle.", 2),
	CCreditEntry(3, 0, 3),
};



// FUNCTION: 0x0051a030
BOOL CCredits::RenderCredits(float time, int alpha)
{
	bool old_in_front_end = gInFrontEnd;
	float y = 430.f - time * 32.f;
	gInFrontEnd = TRUE;

	for (CCreditEntry *e = gCredits; e->mStyle != 3; e++)
	{
		WCHAR *text;
		if (e->mId != -1 && e->mText == NULL)
		{
			// STAND-IN (register order only). Retail tests the section minus 2 against zero (section 2 reads
			// GetCreditTitle) and steps the rest down with a decrement; every other section reads GetCreditTitleUS, case 1
			// and the default being merged by the compiler. The natural `switch (e->mSection)` (cases 2 and 3 and a default,
			// in any order, or on a named copy of the section) makes the same instructions with the loop's first scratch
			// registers the other way round (mId in eax, retail has ecx): shape 0, 18 instructions and 19 bytes off. An
			// if/else on the section is 6/18/284, a two-way switch 4/4/279; only the minus-2 local gives retail's bytes.
			int	kind = e->mSection - 2;
			if (kind != 0)
			{
				switch (kind)
				{
				case 1:		text = GetCreditTitleUS(e->mId);	break;
				default:	text = GetCreditTitleUS(e->mId);	break;
				}
			}
			else
				text = GetCreditTitle(e->mId);
		}
		else
			text = ToWCHAR(e->mText);

		DWORD col = 0xff2f5f7f;
		switch (e->mStyle)
		{
		case 0:	col = 0xffffffff;	break;
		case 1:	col = 0xff7f5f2f;	break;
		}

		switch (e->mStyle)
		{
		case 0:		y += 50.f;	break;
		case 1:		y += 40.f;	break;
		default:	y += 16.f;	break;
		}

		if (y > -20.f && y < 500.f)
		{
			SIZE size;
			PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);
			PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(320.f - size.cx * 0.5f, y, 0.1f, 1.f, 1.f,
				((((col & 0xff000000) >> 8) * alpha) & 0xff000000) | (col & 0x00ffffff), text, 1000.f, 0, 0x10);
		}
	}

	gInFrontEnd = old_in_front_end;
	RENDERINFO.SetFogEnabled(false);
	return y > -30.f;
}
