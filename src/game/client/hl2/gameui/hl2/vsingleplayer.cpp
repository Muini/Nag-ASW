//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "BasePanel.h"
#include "vsingleplayer.h"
#include "EngineInterface.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/CheckButton.h"
#include "KeyValues.h"
#include "vgui/ISurface.h"
#include "vgui/IInput.h"
#include "vgui/ILocalize.h"
#include <vgui/ISystem.h>
#include "vgui_controls/RadioButton.h"
#include "vgui_controls/ComboBox.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui_controls/Frame.h"
#include "vgui_controls/ControllerMap.h"
#include "FileSystem.h"
#include "ModInfo.h"
#include "tier1/convar.h"
#include "GameUI_Interface.h"
#include "tier0/icommandline.h"
#include "vgui_controls/AnimationController.h"
//#include "CommentaryExplanationDialog.h"
#include "vgui_controls/BitmapImagePanel.h"
//#include "BonusMapsDatabase.h"
#include "nb_header_footer.h"
#include "nb_button.h"

#include <stdio.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace BaseModUI;
static float	g_ScrollSpeedSlow;
static float	g_ScrollSpeedFast;

// sort function used in displaying chapter list
struct chapter_t
{
	char filename[32];
};
static int __cdecl ChapterSortFunc(const void *elem1, const void *elem2)
{
	chapter_t *c1 = (chapter_t *)elem1;
	chapter_t *c2 = (chapter_t *)elem2;

	// compare chapter number first
	static int chapterlen = strlen("chapter");
	if (atoi(c1->filename + chapterlen) > atoi(c2->filename + chapterlen))
		return 1;
	else if (atoi(c1->filename + chapterlen) < atoi(c2->filename + chapterlen))
		return -1;

	// compare length second (longer string show up later in the list, eg. chapter9 before chapter9a)
	if (strlen(c1->filename) > strlen(c2->filename))
		return 1;
	else if (strlen(c1->filename) < strlen(c2->filename))
		return -1;

	// compare strings third
	return strcmp(c1->filename, c2->filename);
}

//-----------------------------------------------------------------------------
// Purpose: invisible panel used for selecting a chapter panel
//-----------------------------------------------------------------------------
class CSelectionOverlayPanel : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CSelectionOverlayPanel, Panel );
	int m_iChapterIndex;
	CNewGameDialog *m_pSelectionTarget;
public:
	CSelectionOverlayPanel( Panel *parent, CNewGameDialog *selectionTarget, int chapterIndex ) : BaseClass( parent, NULL )
	{
		m_iChapterIndex = chapterIndex;
		m_pSelectionTarget = selectionTarget;
		SetPaintEnabled(false);
		SetPaintBackgroundEnabled(false);
	}

	virtual void OnMousePressed( vgui::MouseCode code )
	{
		if (GetParent()->IsEnabled())
		{
			m_pSelectionTarget->SetSelectedChapterIndex( m_iChapterIndex );
		}
	}

	virtual void OnMouseDoublePressed( vgui::MouseCode code )
	{
		// call the panel
		OnMousePressed( code );
		if (GetParent()->IsEnabled())
		{
			PostMessage( m_pSelectionTarget, new KeyValues("Command", "command", "play") );
		}
	}
};

//-----------------------------------------------------------------------------
// Purpose: selectable item with screenshot for an individual chapter in the dialog
//-----------------------------------------------------------------------------
class CGameChapterPanel : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CGameChapterPanel, vgui::EditablePanel );

	ImagePanel *m_pLevelPicBorder;
	ImagePanel *m_pLevelPic;
	ImagePanel *m_pCommentaryIcon;
	Label *m_pChapterLabel;
	Label *m_pChapterNameLabel;

	Color m_TextColor;
	Color m_DisabledColor;
	Color m_SelectedColor;
	Color m_FillColor;

	char m_szConfigFile[_MAX_PATH];
	char m_szChapter[32];

	bool m_bTeaserChapter;
	bool m_bHasBonus;
	bool m_bCommentaryMode;

public:
	CGameChapterPanel( CNewGameDialog *parent, const char *name, const char *chapterName, int chapterIndex, const char *chapterNumber, const char *chapterConfigFile, bool bCommentary ) : BaseClass( parent, name )
	{
		Q_strncpy( m_szConfigFile, chapterConfigFile, sizeof(m_szConfigFile) );
		Q_strncpy( m_szChapter, chapterNumber, sizeof(m_szChapter) );

		m_pLevelPicBorder = SETUP_PANEL( new ImagePanel( this, "LevelPicBorder" ) );
		m_pLevelPic = SETUP_PANEL( new ImagePanel( this, "LevelPic" ) );
		m_pCommentaryIcon = NULL;
		m_bCommentaryMode = bCommentary;

		wchar_t text[32];
		wchar_t num[32];
		wchar_t *chapter = g_pVGuiLocalize->Find("#GameUI_Chapter");
		g_pVGuiLocalize->ConvertANSIToUnicode( chapterNumber, num, sizeof(num) );
		_snwprintf( text, sizeof(text), L"%s %s", chapter ? chapter : L"CHAPTER", num );

		if ( ModInfo().IsSinglePlayerOnly() )
		{
			m_pChapterLabel = new Label( this, "ChapterLabel", text );
			m_pChapterNameLabel = new Label( this, "ChapterNameLabel", chapterName );
		}
		else
		{
			m_pChapterLabel = new Label( this, "ChapterLabel", chapterName );
			m_pChapterNameLabel = new Label( this, "ChapterNameLabel", "#GameUI_LoadCommentary" );
		}

		SetPaintBackgroundEnabled( false );

		// the image has the same name as the config file
		char szMaterial[ MAX_PATH ];
		Q_snprintf( szMaterial, sizeof(szMaterial), "chapters/%s", chapterConfigFile );
		char *ext = strstr( szMaterial, "." );
		if ( ext )
		{
			*ext = 0;
		}
		m_pLevelPic->SetImage( szMaterial );

		KeyValues *pKeys = NULL;
		LoadControlSettings( "Resource/NewGameChapterPanel.res", NULL, pKeys );

		int px, py;
		m_pLevelPicBorder->GetPos( px, py );
		SetSize( m_pLevelPicBorder->GetWide(), py + m_pLevelPicBorder->GetTall() );

		// create a selection panel the size of the page
		CSelectionOverlayPanel *overlay = new CSelectionOverlayPanel( this, parent, chapterIndex );
		overlay->SetBounds(0, 0, GetWide(), GetTall());
		overlay->MoveToFront();

		// HACK: Detect new episode teasers by the "Coming Soon" text
		wchar_t w_szStrTemp[256];
		m_pChapterNameLabel->GetText( w_szStrTemp, sizeof(w_szStrTemp)  );
		m_bTeaserChapter = !wcscmp(w_szStrTemp, L"Coming Soon");
		m_bHasBonus = false;
	}

	virtual void ApplySchemeSettings( IScheme *pScheme )
	{
		m_TextColor = pScheme->GetColor( "NewGame.TextColor", Color(255, 255, 255, 255) );
		m_FillColor = pScheme->GetColor( "NewGame.FillColor", Color(255, 255, 255, 255) );
		m_DisabledColor = pScheme->GetColor( "NewGame.DisabledColor", Color(255, 255, 255, 255) );
		m_SelectedColor = pScheme->GetColor( "NewGame.SelectionColor", Color(255, 255, 255, 255) );

		BaseClass::ApplySchemeSettings( pScheme );

		// Hide chapter numbers for new episode teasers
		if ( m_bTeaserChapter )
		{
			m_pChapterLabel->SetVisible( false );
		}

		m_pCommentaryIcon = dynamic_cast<ImagePanel*>( FindChildByName( "CommentaryIcon" ) );
		if ( m_pCommentaryIcon )
			m_pCommentaryIcon->SetVisible( m_bCommentaryMode );
	}

	void SetSelected( bool state )
	{
		// update the text/border colors
		if ( !IsEnabled() )
		{
			m_pChapterLabel->SetFgColor( m_DisabledColor );
			m_pChapterNameLabel->SetFgColor( Color(0, 0, 0, 0) );
			m_pLevelPicBorder->SetFillColor( m_DisabledColor );
			m_pLevelPic->SetAlpha( GameUI().IsConsoleUI() ? 64 : 128 );
			return;
		}

		if ( state )
		{
			if ( !GameUI().IsConsoleUI() )
			{
				m_pChapterLabel->SetFgColor( m_SelectedColor );
				m_pChapterNameLabel->SetFgColor( m_SelectedColor );
			}
			m_pLevelPicBorder->SetFillColor( m_SelectedColor );
		}
		else
		{
			m_pChapterLabel->SetFgColor( m_TextColor );
			m_pChapterNameLabel->SetFgColor( m_TextColor );
			m_pLevelPicBorder->SetFillColor( m_FillColor );
		}
		m_pLevelPic->SetAlpha( 255 );
	}

	const char *GetConfigFile()
	{
		return m_szConfigFile;
	}

	const char *GetChapter()
	{
		return m_szChapter;
	}

	bool IsTeaserChapter()
	{
		return m_bTeaserChapter;
	}

	bool HasBonus()
	{
		return m_bHasBonus;
	}

	void SetCommentaryMode( bool bCommentaryMode )
	{
		m_bCommentaryMode = bCommentaryMode;
		if ( m_pCommentaryIcon )
			m_pCommentaryIcon->SetVisible( m_bCommentaryMode );
	}
};

const char *COM_GetModDirectory2()
{
	static char modDir[MAX_PATH];
	if ( Q_strlen( modDir ) == 0 )
	{
		const char *gamedir = CommandLine()->ParmValue("-game", CommandLine()->ParmValue( "-defaultgamedir", "hl2" ) );
		Q_strncpy( modDir, gamedir, sizeof(modDir) );
		if ( strchr( modDir, '/' ) || strchr( modDir, '\\' ) )
		{
			Q_StripLastDir( modDir, sizeof(modDir) );
			int dirlen = Q_strlen( modDir );
			Q_strncpy( modDir, gamedir + dirlen, sizeof(modDir) - dirlen );
		}
	}

	return modDir;
}

//-----------------------------------------------------------------------------
// Purpose: new game chapter selection
//-----------------------------------------------------------------------------
CNewGameDialog::CNewGameDialog(vgui::Panel *parent, const char *name, bool bCommentaryMode) : BaseClass(parent, name)
{
	GameUI().PreventEngineHideGameUI();

	m_pHeaderFooter = new CNB_Header_Footer( this, "HeaderFooter" );
	m_pHeaderFooter->SetTitle( "" );
	m_pHeaderFooter->SetHeaderEnabled( false );
	m_pHeaderFooter->SetFooterEnabled( true );
	m_pHeaderFooter->SetGradientBarEnabled( true );
	m_pHeaderFooter->SetGradientBarPos( 150, 190 );

	SetDeleteSelfOnClose(true);
	//SetBounds(0, 0, 372, 160);
	SetSizeable( false );
	SetProportional( true );

	SetUpperGarnishEnabled(true);
	SetLowerGarnishEnabled( true );
	SetOkButtonEnabled( false );

	m_iSelectedChapter = -1;
	m_ActiveTitleIdx = 0;

	m_bCommentaryMode = bCommentaryMode;
	m_bMapStarting = false;
	m_bScrolling = false;
	m_ScrollCt = 0;
	m_ScrollSpeed = 0.f;
	m_ButtonPressed = SCROLL_NONE;
	m_ScrollDirection = SCROLL_NONE;
	m_pCommentaryLabel = NULL;

	m_iBonusSelection = 0;
	m_bScrollToFirstBonusMap = false;

	//SetTitle("#GameUI_NewGame", true);

	m_pNextButton = new Button( this, "Next", "#gameui_next" );
	m_pPrevButton = new Button( this, "Prev", "#gameui_prev" );

	//m_pCenterBg = SETUP_PANEL( new Panel( this, "CenterBG" ) );
	//m_pCenterBg->SetVisible( false );

	// parse out the chapters off disk
	static const int MAX_CHAPTERS = 32;
	chapter_t chapters[MAX_CHAPTERS];

	char szFullFileName[MAX_PATH];
	int chapterIndex = 0;

	if ( IsPC() )
	{
		FileFindHandle_t findHandle = FILESYSTEM_INVALID_FIND_HANDLE;
		const char *fileName = "cfg/chapter*.cfg";
		fileName = g_pFullFileSystem->FindFirst( fileName, &findHandle );
		while ( fileName && chapterIndex < MAX_CHAPTERS )
		{
			// Only load chapter configs from the current mod's cfg dir
			// or else chapters appear that we don't want!
			Q_snprintf( szFullFileName, sizeof(szFullFileName), "cfg/%s", fileName );
			FileHandle_t f = g_pFullFileSystem->Open( szFullFileName, "rb", "MOD" );
			if ( f )
			{	
				// don't load chapter files that are empty, used in the demo
				if ( g_pFullFileSystem->Size(f) > 0	)
				{
					Q_strncpy(chapters[chapterIndex].filename, fileName, sizeof(chapters[chapterIndex].filename));
					++chapterIndex;
				}
				g_pFullFileSystem->Close( f );
			}
			fileName = g_pFullFileSystem->FindNext(findHandle);
		}
	}

	bool bBonusesUnlocked = false;

	// sort the chapters
	qsort(chapters, chapterIndex, sizeof(chapter_t), &ChapterSortFunc);

	// work out which chapters are unlocked
	ConVarRef var( "sv_unlockedchapters" );

	if ( bBonusesUnlocked )
	{
		// Bonuses are unlocked so we need to unlock all the chapters too
		var.SetValue( 15 );
	}

	const char *unlockedChapter = var.IsValid() ? var.GetString() : "1";
	int iUnlockedChapter = atoi(unlockedChapter);

	// add chapters to combobox
	for (int i = 0; i < chapterIndex; i++)
	{
		const char *fileName = chapters[i].filename;
		char chapterID[32] = { 0 };
		sscanf(fileName, "chapter%s", chapterID);
		// strip the extension
		char *ext = V_stristr(chapterID, ".cfg");
		if (ext)
		{
			*ext = 0;
		}

		const char *pGameDir = COM_GetModDirectory2();

		char chapterName[64];
		Q_snprintf(chapterName, sizeof(chapterName), "#%s_Chapter%s_Title", pGameDir, chapterID);

		Q_snprintf( szFullFileName, sizeof( szFullFileName ), "%s", fileName );
		CGameChapterPanel *chapterPanel = SETUP_PANEL( new CGameChapterPanel( this, NULL, chapterName, i, chapterID, szFullFileName, m_bCommentaryMode ) );
		chapterPanel->SetVisible( false );

		UpdatePanelLockedStatus( iUnlockedChapter, i + 1, chapterPanel );

		m_ChapterPanels.AddToTail( chapterPanel );
	}

	KeyValues *pKeys = NULL;
	LoadControlSettings( "Resource/ui/basemodui/NewGame.res", NULL, pKeys );

	// Reset all properties
	for ( int i = 0; i < NUM_SLOTS; ++i )
	{
		m_PanelIndex[i] = INVALID_INDEX;
	}

	if ( !m_ChapterPanels.Count() )
	{
		UpdateMenuComponents( SCROLL_NONE );
		return;
	}

	// Layout panel positions relative to the dialog center.
	int panelWidth = m_ChapterPanels[0]->GetWide() + 16;
	int dialogWidth = GetWide();
	m_PanelXPos[2] = ( dialogWidth - panelWidth ) / 2 + 8;
	m_PanelXPos[1] = m_PanelXPos[2] - panelWidth;
	m_PanelXPos[0] = m_PanelXPos[1];
	m_PanelXPos[3] = m_PanelXPos[2] + panelWidth;
	m_PanelXPos[4] = m_PanelXPos[3];

	m_PanelAlpha[0] = 0;
	m_PanelAlpha[1] = 255;
	m_PanelAlpha[2] = 255;
	m_PanelAlpha[3] = 255;
	m_PanelAlpha[4] = 0;

	/*int panelHeight;
	m_ChapterPanels[0]->GetSize( panelWidth, panelHeight );
	m_pCenterBg->SetWide( panelWidth + 16 );
	m_pCenterBg->SetPos( m_PanelXPos[2] - 8, m_PanelYPos[2] - (m_pCenterBg->GetTall() - panelHeight) + 8 );
	m_pCenterBg->SetBgColor( Color( 190, 115, 0, 255 ) );*/

	// start the first item selected
	SetSelectedChapterIndex( 0 );
}

CNewGameDialog::~CNewGameDialog()
{
	GameUI().AllowEngineHideGameUI();
}

void CNewGameDialog::Activate( void )
{
	m_bMapStarting = false;

	// Commentary stuff is set up on activate because in XBox the new game menu is never deleted
	SetTitle( ( ( m_bCommentaryMode ) ? ( "#GameUI_LoadCommentary" ) : ( "#GameUI_NewGame") ), true);

	if ( m_pCommentaryLabel )
		m_pCommentaryLabel->SetVisible( m_bCommentaryMode );

	// work out which chapters are unlocked
	ConVarRef var( "sv_unlockedchapters" );
	const char *unlockedChapter = var.IsValid() ? var.GetString() : "1";
	int iUnlockedChapter = atoi(unlockedChapter);

	for ( int i = 0; i < m_ChapterPanels.Count(); i++)
	{
		CGameChapterPanel *pChapterPanel = m_ChapterPanels[ i ];

		if ( pChapterPanel )
		{
			pChapterPanel->SetCommentaryMode( m_bCommentaryMode );

			UpdatePanelLockedStatus( iUnlockedChapter, i + 1, pChapterPanel );
		}
	}

	BaseClass::Activate();
}

//-----------------------------------------------------------------------------
// Purpose: Apply special properties of the menu
//-----------------------------------------------------------------------------
void CNewGameDialog::ApplySettings( KeyValues *inResourceData )
{
	BaseClass::ApplySettings( inResourceData );

	int ypos = inResourceData->GetInt( "chapterypos", 40 );
	for ( int i = 0; i < NUM_SLOTS; ++i )
	{
		m_PanelYPos[i] = ypos + 315; //thats hardcoded. fuck. [str]
	}

	//m_pCenterBg->SetTall( inResourceData->GetInt( "centerbgtall", 0 ) );

	g_ScrollSpeedSlow = inResourceData->GetFloat( "scrollslow", 0.0f );
	g_ScrollSpeedFast = inResourceData->GetFloat( "scrollfast", 0.0f );
	SetFastScroll( false );
}

void CNewGameDialog::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

	UpdateMenuComponents( SCROLL_NONE );

	SetPaintBackgroundEnabled( true );
	SetupAsDialogStyle();

	m_pCommentaryLabel = dynamic_cast<vgui::Label*>( FindChildByName( "CommentaryUnlock" ) );
	if ( m_pCommentaryLabel )
		m_pCommentaryLabel->SetVisible( m_bCommentaryMode );
}
void CNewGameDialog::UpdateFooter()
{
	CBaseModFooterPanel *footer = BaseModUI::CBaseModPanel::GetSingleton().GetFooterPanel();
	if ( footer )
	{
		footer->SetButtons( FB_ABUTTON | FB_BBUTTON, FF_AB_ONLY, false );
		footer->SetButtonText( FB_ABUTTON, "#L4D360UI_Select" );
		footer->SetButtonText( FB_BBUTTON, "#L4D360UI_Done" );
	}
}

static float GetArrowAlpha( void )
{
	// X360TBD: Pulsing arrows
	return 255.f;
}

//-----------------------------------------------------------------------------
// Purpose: sets the correct properties for visible components
//-----------------------------------------------------------------------------
void CNewGameDialog::UpdateMenuComponents( EScrollDirection dir )
{
	// This is called prior to any scrolling, 
	// so we need to look ahead to the post-scroll state
	int centerIdx = SLOT_CENTER;
	if ( dir == SCROLL_LEFT )
	{
		++centerIdx;
	}
	else if ( dir == SCROLL_RIGHT )
	{
		--centerIdx;
	}
	int leftIdx = centerIdx - 1;
	int rightIdx = centerIdx + 1;

	if ( GameUI().IsConsoleUI() )
	{
		bool bHasBonus = false;
		if ( m_PanelIndex[centerIdx] != INVALID_INDEX )
		{
			wchar_t buffer[ MAX_PATH ];
			m_ChapterPanels[ m_PanelIndex[centerIdx] ]->m_pChapterNameLabel->GetText( buffer, sizeof(buffer) );
			m_pChapterTitleLabels[m_ActiveTitleIdx]->SetText( buffer );

			// If it has bonuses show the scroll up and down arrows
			bHasBonus = m_ChapterPanels[ m_PanelIndex[centerIdx] ]->HasBonus();
		}

		vgui::Panel *leftArrow = this->FindChildByName( "LeftArrow" );
		vgui::Panel *rightArrow = this->FindChildByName( "RightArrow" );
		if ( leftArrow )
		{
			if ( m_PanelIndex[leftIdx] != INVALID_INDEX )
			{
				leftArrow->SetFgColor( Color( 255, 255, 255, GetArrowAlpha() ) );
			}
			else
			{
				leftArrow->SetFgColor( Color( 128, 128, 128, 64 ) );
			}
		}
		if ( rightArrow )
		{
			if ( m_PanelIndex[rightIdx] != INVALID_INDEX )
			{
				rightArrow->SetFgColor( Color( 255, 255, 255, GetArrowAlpha() ) );
			}
			else
			{
				rightArrow->SetFgColor( Color( 128, 128, 128, 64 ) );
			}
		}

			m_pBonusMapDescription = NULL;

		vgui::Panel *upArrow = this->FindChildByName( "UpArrow" );
		vgui::Panel *downArrow = this->FindChildByName( "DownArrow" );

		if ( upArrow )
			upArrow->SetVisible( bHasBonus );
		if ( downArrow )
			downArrow->SetVisible( bHasBonus );

		m_pBonusSelection->SetVisible( bHasBonus );
		m_pBonusSelectionBorder->SetVisible( bHasBonus );

		UpdateBonusSelection();
	}

	// No buttons in the xbox ui
	if ( !GameUI().IsConsoleUI() )
	{
		if ( m_PanelIndex[leftIdx] == INVALID_INDEX || m_PanelIndex[leftIdx] == 0 )
		{
			m_pPrevButton->SetVisible( false );
			m_pPrevButton->SetEnabled( false );
		}
		else
		{
			m_pPrevButton->SetVisible( true );
			m_pPrevButton->SetEnabled( true );
		}

		if ( m_ChapterPanels.Count() < 4 ) // if there are less than 4 chapters show the next button but disabled
		{
			m_pNextButton->SetVisible( true );
			m_pNextButton->SetEnabled( false );
		}
		else if ( m_PanelIndex[rightIdx] == INVALID_INDEX || m_PanelIndex[rightIdx] == m_ChapterPanels.Count()-1 )
		{
			m_pNextButton->SetVisible( false );
			m_pNextButton->SetEnabled( false );
		}
		else
		{
			m_pNextButton->SetVisible( true );
			m_pNextButton->SetEnabled( true );
		}
	}
}

void CNewGameDialog::UpdateBonusSelection( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: sets a chapter as selected
//-----------------------------------------------------------------------------
void CNewGameDialog::SetSelectedChapterIndex( int index )
{
	m_iSelectedChapter = index;

	for (int i = 0; i < m_ChapterPanels.Count(); i++)
	{
		if ( i == index )
		{
			m_ChapterPanels[i]->SetSelected( true );
		}
		else
		{
			m_ChapterPanels[i]->SetSelected( false );
		}
	}

	SetControlEnabled( "BtnPlay", true );

	// Setup panels to the left of the selected panel
	int selectedSlot = GameUI().IsConsoleUI() ? SLOT_CENTER : index % 3 + 1;
	int currIdx = index;
	for ( int i = selectedSlot; i >= 0 && currIdx >= 0; --i )
	{
		m_PanelIndex[i] = currIdx;
		--currIdx;
		InitPanelIndexForDisplay( i );
	}

	// Setup panels to the right of the selected panel
	currIdx = index + 1;
	for ( int i = selectedSlot + 1; i < NUM_SLOTS && currIdx < m_ChapterPanels.Count(); ++i )
	{
		m_PanelIndex[i] = currIdx;
		++currIdx;
		InitPanelIndexForDisplay( i );
	}

	UpdateMenuComponents( SCROLL_NONE );
}

//-----------------------------------------------------------------------------
// Purpose: sets a chapter as selected
//-----------------------------------------------------------------------------
void CNewGameDialog::SetSelectedChapter( const char *chapter )
{
	Assert( chapter );
	for (int i = 0; i < m_ChapterPanels.Count(); i++)
	{
		if ( chapter && !Q_stricmp(m_ChapterPanels[i]->GetChapter(), chapter) )
		{
			m_iSelectedChapter = i;
			m_ChapterPanels[m_iSelectedChapter]->SetSelected( true );
		}
		else
		{
			m_ChapterPanels[i]->SetSelected( false );
		}
	}

	SetControlEnabled( "BtnPlay", true );
}


//-----------------------------------------------------------------------------
// iUnlockedChapter - the value of sv_unlockedchapters, 1-based. A value of 0
//		is treated as a 1, since at least one chapter must be unlocked.
//
// i - the 1-based index of the chapter we're considering.
//-----------------------------------------------------------------------------
void CNewGameDialog::UpdatePanelLockedStatus( int iUnlockedChapter, int i, CGameChapterPanel *pChapterPanel )
{
	if ( iUnlockedChapter <= 0 )
	{
		iUnlockedChapter = 1;
	}

	// Commentary mode requires chapters to be finished before they can be chosen
	bool bLocked = false;

	if ( m_bCommentaryMode )
	{
		bLocked = ( iUnlockedChapter <= i );
	}
	else
	{
		if ( iUnlockedChapter < i )
		{
			// Never lock the first chapter
			bLocked = ( i != 0 );
		}
	}

	pChapterPanel->SetEnabled( !bLocked );
}

//-----------------------------------------------------------------------------
// Purpose: Called before a panel scroll starts.
//-----------------------------------------------------------------------------
void CNewGameDialog::PreScroll( EScrollDirection dir )
{
	int hideIdx = INVALID_INDEX;
	if ( dir == SCROLL_LEFT )
	{
		hideIdx = m_PanelIndex[SLOT_LEFT];
	}
	else if ( dir == SCROLL_RIGHT )
	{
		hideIdx = m_PanelIndex[SLOT_RIGHT];
	}
	if ( hideIdx != INVALID_INDEX )
	{
		// Push back the panel that's about to be hidden
		// so the next panel scrolls over the top of it.
		m_ChapterPanels[hideIdx]->SetZPos( 0 );
	}

	// Flip the active title label prior to the crossfade
	m_ActiveTitleIdx ^= 0x01;
}

//-----------------------------------------------------------------------------
// Purpose: Called after a panel scroll finishes.
//-----------------------------------------------------------------------------
void CNewGameDialog::PostScroll( EScrollDirection dir )
{
	int index = INVALID_INDEX;
	if ( dir == SCROLL_LEFT )
	{
		index = m_PanelIndex[SLOT_RIGHT];
	}
	else if ( dir == SCROLL_RIGHT )
	{
		index = m_PanelIndex[SLOT_LEFT];
	}

	// Fade in the revealed panel
	if ( index != INVALID_INDEX )
	{
		CGameChapterPanel *panel = m_ChapterPanels[index];
		panel->SetZPos( 50 );
		GetAnimationController()->RunAnimationCommand( panel, "alpha", 255, 0, m_ScrollSpeed, vgui::AnimationController::INTERPOLATOR_LINEAR );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Initiates a panel scroll and starts the animation.
//-----------------------------------------------------------------------------
void CNewGameDialog::ScrollSelectionPanels( EScrollDirection dir )
{
	// Only initiate a scroll if panels aren't currently scrolling
	if ( !m_bScrolling )
	{
		// Handle any pre-scroll setup
		PreScroll( dir );

		if ( dir == SCROLL_LEFT)
		{
			m_ScrollCt += SCROLL_LEFT;
		}
		else if ( dir == SCROLL_RIGHT && m_PanelIndex[SLOT_CENTER] != 0 )
		{
			m_ScrollCt += SCROLL_RIGHT;
		}

		m_bScrolling = true;
		AnimateSelectionPanels();

		// Update the arrow colors, help text, and buttons. Doing it here looks better than having
		// the components change after the entire scroll animation has finished.
		UpdateMenuComponents( m_ScrollDirection );
	}
}

void CNewGameDialog::ScrollBonusSelection( EScrollDirection dir )
{
	// Don't scroll if there's no bonuses for this panel
	if ( !m_pBonusMapDescription )
		return;

	m_iBonusSelection += dir;

	vgui::surface()->PlaySound( "UI/buttonclick.wav" );

	UpdateBonusSelection();
}

//-----------------------------------------------------------------------------
// Purpose: Initiates the scripted scroll and fade effects of all five slotted panels 
//-----------------------------------------------------------------------------
void CNewGameDialog::AnimateSelectionPanels( void )
{
	int idxOffset = 0;
	int startIdx = SLOT_LEFT;
	int endIdx = SLOT_RIGHT;

	// Don't scroll outside the bounds of the panel list
	if ( m_ScrollCt >= SCROLL_LEFT && (m_PanelIndex[SLOT_CENTER] < m_ChapterPanels.Count() - 1 || !GameUI().IsConsoleUI()) )
	{
		idxOffset = -1;
		endIdx = SLOT_OFFRIGHT;
		m_ScrollDirection = SCROLL_LEFT;
	}
	else if ( m_ScrollCt <= SCROLL_RIGHT && (m_PanelIndex[SLOT_CENTER] > 0 || !GameUI().IsConsoleUI()) )
	{
		idxOffset = 1;
		startIdx = SLOT_OFFLEFT;
		m_ScrollDirection = SCROLL_RIGHT;
	}

	if ( 0 == idxOffset )
	{
		// Kill the scroll, it's outside the bounds
		m_ScrollCt = 0;
		m_bScrolling = false;
		m_ScrollDirection = SCROLL_NONE;
		vgui::surface()->PlaySound( "player/suit_denydevice.wav" );
		return;
	}

	// Should never happen
	if ( startIdx > endIdx )
		return;

	for ( int i = startIdx; i <= endIdx; ++i )
	{
		if ( m_PanelIndex[i] != INVALID_INDEX )
		{
			int nextIdx = i + idxOffset;
			CGameChapterPanel *panel = m_ChapterPanels[ m_PanelIndex[i] ];
			GetAnimationController()->RunAnimationCommand( panel, "xpos",  m_PanelXPos[nextIdx],  0, m_ScrollSpeed, vgui::AnimationController::INTERPOLATOR_LINEAR );
			GetAnimationController()->RunAnimationCommand( panel, "ypos",  m_PanelYPos[nextIdx],  0, m_ScrollSpeed, vgui::AnimationController::INTERPOLATOR_LINEAR );
			GetAnimationController()->RunAnimationCommand( panel, "alpha", m_PanelAlpha[nextIdx], 0, m_ScrollSpeed, vgui::AnimationController::INTERPOLATOR_LINEAR );
		}
	}

	if ( GameUI().IsConsoleUI() )
	{
		vgui::surface()->PlaySound( "UI/buttonclick.wav" );

		// Animate the center background panel
//		GetAnimationController()->RunAnimationCommand( m_pCenterBg, "alpha", 0, 0, m_ScrollSpeed * 0.25f, vgui::AnimationController::INTERPOLATOR_LINEAR );
		
		// Crossfade the chapter title labels
		int inactiveTitleIdx = m_ActiveTitleIdx ^ 0x01;
		GetAnimationController()->RunAnimationCommand( m_pChapterTitleLabels[m_ActiveTitleIdx], "alpha", 255, 0, m_ScrollSpeed, vgui::AnimationController::INTERPOLATOR_LINEAR );
		GetAnimationController()->RunAnimationCommand( m_pChapterTitleLabels[inactiveTitleIdx], "alpha", 0, 0, m_ScrollSpeed, vgui::AnimationController::INTERPOLATOR_LINEAR );
		
		// Scrolling up through chapters, offset is negative
		m_iSelectedChapter -= idxOffset;
	}

	PostMessage( this, new KeyValues( "FinishScroll" ), m_ScrollSpeed );
}

//-----------------------------------------------------------------------------
// Purpose: After a scroll, each panel slot holds the index of a panel that has 
//			scrolled to an adjacent slot. This function updates each slot so
//			it holds the index of the panel that is actually in that slot's position.
//-----------------------------------------------------------------------------
void CNewGameDialog::ShiftPanelIndices( int offset )
{
	// Shift all the elements over one slot, then calculate what the last slot's index should be.
	int lastSlot = NUM_SLOTS - 1;
	if ( offset > 0 )
	{
		// Hide the panel that's dropping out of the slots
		if ( IsValidPanel( m_PanelIndex[0] ) )
		{
			m_ChapterPanels[ m_PanelIndex[0] ]->SetVisible( false );
		}

		// Scrolled panels to the right, so shift the indices one slot to the left
		Q_memmove( &m_PanelIndex[0], &m_PanelIndex[1], lastSlot * sizeof( m_PanelIndex[0] ) );
		if ( m_PanelIndex[lastSlot] != INVALID_INDEX )
		{
			int num = m_PanelIndex[ lastSlot ] + 1;
			if ( IsValidPanel( num ) )
			{
				m_PanelIndex[lastSlot] = num;
				InitPanelIndexForDisplay( lastSlot );
			}
			else
			{
				m_PanelIndex[lastSlot] = INVALID_INDEX;
			}
		}
	}
	else
	{
		// Hide the panel that's dropping out of the slots
		if ( IsValidPanel( m_PanelIndex[lastSlot] ) )
		{
			m_ChapterPanels[ m_PanelIndex[lastSlot] ]->SetVisible( false );
		}

		// Scrolled panels to the left, so shift the indices one slot to the right
		Q_memmove( &m_PanelIndex[1], &m_PanelIndex[0], lastSlot * sizeof( m_PanelIndex[0] ) );
		if ( m_PanelIndex[0] != INVALID_INDEX )
		{
			int num = m_PanelIndex[0] - 1;
			if ( IsValidPanel( num ) )
			{
				m_PanelIndex[0] = num;
				InitPanelIndexForDisplay( 0 );
			}
			else
			{
				m_PanelIndex[0] = INVALID_INDEX;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Validates an index into the selection panels vector
//-----------------------------------------------------------------------------
bool CNewGameDialog::IsValidPanel( const int idx )
{
	if ( idx < 0 || idx >= m_ChapterPanels.Count() )
		return false;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Sets up a panel's properties before it is displayed
//-----------------------------------------------------------------------------
void CNewGameDialog::InitPanelIndexForDisplay( const int idx )
{
	CGameChapterPanel *panel = m_ChapterPanels[ m_PanelIndex[idx] ];
	if ( panel )
	{
		panel->SetPos( m_PanelXPos[idx], m_PanelYPos[idx] );
		panel->SetAlpha( m_PanelAlpha[idx] );
		panel->SetVisible( true );
		if ( m_PanelAlpha[idx] )
		{
			panel->SetZPos( 50 );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Sets which scroll speed should be used
//-----------------------------------------------------------------------------
void CNewGameDialog::SetFastScroll( bool fast )
{
	m_ScrollSpeed = fast ? g_ScrollSpeedFast : g_ScrollSpeedSlow;
}

//-----------------------------------------------------------------------------
// Purpose: Checks if a button is being held down, and speeds up the scroll 
//-----------------------------------------------------------------------------
void CNewGameDialog::ContinueScrolling( void )
{
	if ( !GameUI().IsConsoleUI() )
	{
		if ( m_PanelIndex[SLOT_CENTER-1] % 3 )
		{
	//		m_ButtonPressed = m_ScrollDirection;
			ScrollSelectionPanels( m_ScrollDirection );
		}
		return;
	}

	if ( m_ButtonPressed == m_ScrollDirection )
	{
		SetFastScroll( true );
		ScrollSelectionPanels( m_ScrollDirection );
	}
	else if ( m_ButtonPressed != SCROLL_NONE )
	{
		// The other direction has been pressed - start a slow scroll
		SetFastScroll( false );
		ScrollSelectionPanels( (EScrollDirection)m_ButtonPressed );
	}
	else
	{
		SetFastScroll( false );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called when a scroll distance of one slot has been completed
//-----------------------------------------------------------------------------
void CNewGameDialog::FinishScroll( void )
{
	// Fade the center bg panel back in
//	GetAnimationController()->RunAnimationCommand( m_pCenterBg, "alpha", 255, 0, m_ScrollSpeed * 0.25f, vgui::AnimationController::INTERPOLATOR_LINEAR );

	ShiftPanelIndices( m_ScrollDirection );
	m_bScrolling = false;
	m_ScrollCt = 0;
	
	// End of scroll step
	PostScroll( m_ScrollDirection );

	// Continue scrolling if necessary
	ContinueScrolling();
}

//-----------------------------------------------------------------------------
// Purpose: starts the game at the specified skill level
//-----------------------------------------------------------------------------
void CNewGameDialog::StartGame( void )
{
	if ( m_ChapterPanels.IsValidIndex( m_iSelectedChapter ) )
	{
		char mapcommand[512];
		mapcommand[0] = 0;
		Q_snprintf( mapcommand, sizeof( mapcommand ), "disconnect\ndeathmatch 0\nprogress_enable\nexec %s\n", m_ChapterPanels[m_iSelectedChapter]->GetConfigFile() );

		// Set commentary
		ConVarRef commentary( "commentary" );
		commentary.SetValue( m_bCommentaryMode );

		ConVarRef sv_cheats( "sv_cheats" );
		sv_cheats.SetValue( m_bCommentaryMode );

		if ( IsPC() )
		{
			{
				// start map
				engine->ClientCmd( mapcommand ); //replace with proper fading and stuff? [str]
//				BasePanel()->FadeToBlackAndRunEngineCommand( mapcommand );
			}
		}

		OnClose();
	}
}

void CNewGameDialog::OnClose( void )
{
	BaseClass::OnClose();
}

//-----------------------------------------------------------------------------
// Purpose: handles button commands
//-----------------------------------------------------------------------------
void CNewGameDialog::OnCommand( const char *command )
{
	if ( !stricmp( command, "Play" ) )
	{
		if ( m_bMapStarting )
			return;

		{
			StartGame();
		}
	}

	else if ( !stricmp( command, "Next" ) )
	{
		if ( m_bMapStarting )
			return;

		ScrollSelectionPanels( SCROLL_LEFT );
	}
	else if ( !stricmp( command, "Prev" ) )
	{
		if ( m_bMapStarting )
			return;

		ScrollSelectionPanels( SCROLL_RIGHT );
	}
	else if ( !stricmp( command, "Mode_Next" ) )
	{
		if ( m_bMapStarting )
			return;

		ScrollBonusSelection( SCROLL_LEFT );
	}
	else if ( !stricmp( command, "Mode_Prev" ) )
	{
		if ( m_bMapStarting )
			return;

		ScrollBonusSelection( SCROLL_RIGHT );
	}
	else if ( !Q_stricmp( command, "ReleaseModalWindow" ) )
	{
		vgui::surface()->RestrictPaintToSinglePanel(NULL);
	}
	else
	{
		BaseClass::OnCommand( command );
	}
}

void CNewGameDialog::PaintBackground()
{
	if ( !GameUI().IsConsoleUI() )
	{
		BaseClass::PaintBackground();
		return;
	}

	int wide, tall;
	GetSize( wide, tall );

	Color col = GetBgColor();
	DrawBox( 0, 0, wide, tall, col, 1.0f );

	int y = 0;
	if ( m_pChapterTitleLabels[0] )
	{
		// offset by title
		int titleX, titleY, titleWide, titleTall;
		m_pChapterTitleLabels[0]->GetBounds( titleX, titleY, titleWide, titleTall );	
		y += titleY + titleTall;
	}
	else
	{
		y = 8;
	}

	// draw an inset
	Color darkColor;
	darkColor.SetColor( 0.70f * (float)col.r(), 0.70f * (float)col.g(), 0.70f * (float)col.b(), col.a() );
	vgui::surface()->DrawSetColor( darkColor );
	vgui::surface()->DrawFilledRect( 8, y, wide - 8, tall - 8 );
}



/*//========= Copyright (c) 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#include "vsingleplayer.h"
#include "VFooterPanel.h"
#include "VGenericPanelList.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui/ISurface.h"
#include "vgui/IVGui.h"
#include "vgui/ilocalize.h"
#include "EngineInterface.h"
#include "FileSystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;
using namespace BaseModUI;

namespace BaseModUI
{

// sort function used in displaying chapter list
struct chapter_t
{
	char filename[32];
};
static int __cdecl ChapterSortFunc(const void *elem1, const void *elem2)
{
	chapter_t *c1 = (chapter_t *)elem1;
	chapter_t *c2 = (chapter_t *)elem2;

	// compare chapter number first
	static int chapterlen = strlen("chapter");
	if (atoi(c1->filename + chapterlen) > atoi(c2->filename + chapterlen))
		return 1;
	else if (atoi(c1->filename + chapterlen) < atoi(c2->filename + chapterlen))
		return -1;

	// compare length second (longer string show up later in the list, eg. chapter9 before chapter9a)
	if (strlen(c1->filename) > strlen(c2->filename))
		return 1;
	else if (strlen(c1->filename) < strlen(c2->filename))
		return -1;

	// compare strings third
	return strcmp(c1->filename, c2->filename);
}

class ChapterLabel : public vgui::Label
{
	DECLARE_CLASS_SIMPLE( ChapterLabel, vgui::Label );

public:
	ChapterLabel( vgui::Panel *pParent, const char *pPanelName ) : BaseClass( pParent, pPanelName, "" )
	{
		m_pDialog = dynamic_cast< CNewGameDialog * >( pParent );

		SetProportional( true );
		SetPaintBackgroundEnabled( true );

		SetMouseInputEnabled( false );
		SetKeyBoardInputEnabled( false );

		m_hChapterNumberFont = vgui::INVALID_FONT;
		m_hChapterNameFont = vgui::INVALID_FONT;

		m_nChapterIndex = 0;
	}

	void SetChapterIndex( int nChapterIndex )
	{
		m_nChapterIndex = nChapterIndex;
	}

protected:
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );

		m_hChapterNumberFont = pScheme->GetFont( "NewGameChapter", true );
		m_hChapterNameFont = pScheme->GetFont( "NewGameChapterName", true );
	}

	virtual void PaintBackground()
	{
		if ( !m_nChapterIndex )
			return;

		wchar_t *pChapterTitle = NULL;
		wchar_t chapterNumberString[256];
		chapterNumberString[0] = 0;

		bool bIsLocked = false;

		if ( m_nChapterIndex )
		{
			// sp chapter labels
			pChapterTitle = g_pVGuiLocalize->Find( CFmtStr( "#SP_PRESENCE_TEXT_CH%d", m_nChapterIndex  ) );
			bIsLocked = ( m_nChapterIndex ) > m_pDialog->GetNumAllowedChapters();
		}

		if ( !pChapterTitle )
		{
			pChapterTitle = L"";
		}

		V_wcsncpy( chapterNumberString, pChapterTitle, sizeof( chapterNumberString ) );
		wchar_t *pHeaderPrefix = wcsstr( chapterNumberString, L"\n" );
		if ( pHeaderPrefix )
		{
			*pHeaderPrefix = 0;
			V_wcsncpy( pHeaderPrefix, L" - ", sizeof( chapterNumberString ) - V_wcslen( chapterNumberString ) * sizeof( wchar_t ) );
		}

		if ( !bIsLocked )
		{
			pHeaderPrefix = wcsstr( pChapterTitle, L"\n" );
			if ( pHeaderPrefix )
			{
				pChapterTitle = pHeaderPrefix + 1;
			}
		}
		else
		{
			pChapterTitle =  g_pVGuiLocalize->Find( "#GameUI_Achievement_Locked" );
			if ( !pChapterTitle )
			{
				pChapterTitle = L"...";
			}
		}

		int x = 0;
		x += DrawText( x, 0, chapterNumberString, m_hChapterNumberFont, Color( 0, 0, 0, 255 ) );
		int yTitleOffset = 0;
		if ( IsOSX() )
			yTitleOffset -=  (( surface()->GetFontTall(m_hChapterNameFont) - surface()->GetFontTall(m_hChapterNumberFont) )/2 + 1) ;
		x += DrawText( x, yTitleOffset, pChapterTitle, m_hChapterNameFont, Color( 0, 0, 0, 255 ) );
	}

private:
	int	DrawText( int x, int y, const wchar_t *pString, vgui::HFont hFont, Color color )
	{
		int len = V_wcslen( pString );

		int textWide, textTall;
		surface()->GetTextSize( hFont, pString, textWide, textTall );

		vgui::surface()->DrawSetTextFont( hFont );
		vgui::surface()->DrawSetTextPos( x, y );
		vgui::surface()->DrawSetTextColor( color );
		vgui::surface()->DrawPrintText( pString, len );

		return textWide;
	}

	CNewGameDialog *m_pDialog;

	vgui::HFont	m_hChapterNumberFont;
	vgui::HFont	m_hChapterNameFont;

	int			m_nChapterIndex;
};

class ChapterListItem : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( ChapterListItem, vgui::EditablePanel );

public:
	ChapterListItem( vgui::Panel *pParent, const char *pPanelName ) : BaseClass( pParent, pPanelName ),
		m_pListCtrlr( ( GenericPanelList * )pParent )
	{
		m_pDialog = dynamic_cast< CNewGameDialog * >( m_pListCtrlr->GetParent() );

		SetProportional( true );
		SetPaintBackgroundEnabled( true );

		m_nChapterIndex = 0;
		m_hTextFont = vgui::INVALID_FONT;

		m_nTextOffsetY = 0;

		m_bSelected = false;
		m_bHasMouseover = false;
		m_bLocked = false;
	}

	void SetChapterIndex( int nIndex )
	{
		m_nChapterIndex = nIndex;
		if ( nIndex <= 0 )
			return;

		Label *pLabel = dynamic_cast< Label* >( FindChildByName( "LblChapterName" ) );
		if ( !pLabel )
			return;

		wchar_t chapterString[256];
		wchar_t *pChapterTitle = NULL;

		if ( m_nChapterIndex )
		{
			pChapterTitle = g_pVGuiLocalize->Find( CFmtStr( "#SP_PRESENCE_TEXT_CH%d", m_nChapterIndex ) );
			bool bIsLocked = ( m_nChapterIndex ) > m_pDialog->GetNumAllowedChapters();
			if ( bIsLocked )
			{
				// chapter is locked, hide title
				m_bLocked = true;

				V_wcsncpy( chapterString, pChapterTitle, sizeof( chapterString ) );
				wchar_t *pHeaderPrefix = wcsstr( chapterString, L"\n" );
				if ( pHeaderPrefix )
				{
					// truncate the title, want to preserve "Chapter ?"
					*pHeaderPrefix = 0;
					pChapterTitle = chapterString;
				}
			}
		}
		
		if ( !pChapterTitle )
		{
			pChapterTitle = L"";
		}

		wchar_t *pHeaderPrefix = wcsstr( pChapterTitle, L"\n" );
		if ( pHeaderPrefix )
		{
			pChapterTitle = pHeaderPrefix + 1;
		}

		pLabel->SetText( pChapterTitle );
	}

	bool IsSelected( void ) { return m_bSelected; }
	void SetSelected( bool bSelected ) { m_bSelected = bSelected; }

	bool HasMouseover( void ) { return m_bHasMouseover; }

	void SetHasMouseover( bool bHasMouseover )
	{
		if ( bHasMouseover )
		{
			for ( int i = 0; i < m_pListCtrlr->GetPanelItemCount(); i++ )
			{
				ChapterListItem *pItem = dynamic_cast< ChapterListItem* >( m_pListCtrlr->GetPanelItem( i ) );
				if ( pItem && pItem != this )
				{
					pItem->SetHasMouseover( false );
				}
			}
		}
		m_bHasMouseover = bHasMouseover; 
	}

	bool IsLocked( void ) { return m_bLocked; }

	int GetChapterIndex()
	{
		return m_nChapterIndex;
	}

	void OnKeyCodePressed( vgui::KeyCode code )
	{
		int iUserSlot = GetJoystickForCode( code );
		CBaseModPanel::GetSingleton().SetLastActiveUserId( iUserSlot );
		switch( GetBaseButtonCode( code ) )
		{
		case KEY_XBUTTON_A: 
		case KEY_ENTER: 
			ActivateSelectedItem();
			return;
		}

		BaseClass::OnKeyCodePressed( code );
	}

protected:
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );

		LoadControlSettings( "Resource/UI/BaseModUI/newgame_chapteritem.res" );

		m_hTextFont = pScheme->GetFont( "NewGameChapterName", true );

		m_TextColor = GetSchemeColor( "HybridButton.TextColor", pScheme );
		m_FocusColor = GetSchemeColor( "HybridButton.FocusColor", pScheme );
		m_CursorColor = GetSchemeColor( "HybridButton.CursorColor", pScheme );
		m_LockedColor = GetSchemeColor( "HybridButton.LockedColor", pScheme );
		m_MouseOverCursorColor = GetSchemeColor( "HybridButton.MouseOverCursorColor", pScheme );

		m_nTextOffsetY = vgui::scheme()->GetProportionalScaledValue( atoi( pScheme->GetResourceString( "NewGameDialog.TextOffsetY" ) ) );
	}

	virtual void PaintBackground()
	{
		bool bHasFocus = HasFocus() || IsSelected();
	
		int x, y, wide, tall;
		GetBounds( x, y, wide, tall );

		if ( bHasFocus )
		{
			surface()->DrawSetColor( m_CursorColor );
			surface()->DrawFilledRect( 0, 0, wide, tall );
		}
		else if ( HasMouseover() )
		{
			surface()->DrawSetColor( m_MouseOverCursorColor );
			surface()->DrawFilledRect( 0, 0, wide, tall );
		}

		DrawListItemLabel( dynamic_cast< vgui::Label * >( FindChildByName( "LblChapterName" ) ) );
	}

	virtual void OnCursorEntered()
	{
		SetHasMouseover( true ); 

		if ( IsPC() )
			return;

		if ( GetParent() )
			GetParent()->NavigateToChild( this );
		else
			NavigateTo();
	}

	virtual void OnCursorExited()
	{
		SetHasMouseover( false ); 
	}

	virtual void NavigateTo( void )
	{
		m_pListCtrlr->SelectPanelItemByPanel( this );
		SetHasMouseover( true );
		RequestFocus();
		BaseClass::NavigateTo();
	}

	virtual void NavigateFrom( void )
	{
		SetHasMouseover( false );
		BaseClass::NavigateFrom();
	}

	void OnMousePressed( vgui::MouseCode code )
	{
		if ( code == MOUSE_LEFT )
		{
			if ( GetParent() )
				GetParent()->NavigateToChild( this );
			else
				NavigateTo();
			return;
		}
		BaseClass::OnMousePressed( code );
	}

	void OnMouseDoublePressed( vgui::MouseCode code )
	{
		if ( code == MOUSE_LEFT )
		{
			ChapterListItem* pListItem = static_cast< ChapterListItem* >( m_pListCtrlr->GetSelectedPanelItem() );
			if ( pListItem )
			{
				OnKeyCodePressed( ButtonCodeToJoystickButtonCode( KEY_XBUTTON_A, CBaseModPanel::GetSingleton().GetLastActiveUserId() ) );
			}

			return;
		}

		BaseClass::OnMouseDoublePressed( code );
	}

	void PerformLayout()
	{
		BaseClass::PerformLayout();

		// set all our children (image panel and labels) to not accept mouse input so they
		// don't eat any mouse input and it all goes to us
		for ( int i = 0; i < GetChildCount(); i++ )
		{
			Panel *panel = GetChild( i );
			Assert( panel );
			panel->SetMouseInputEnabled( false );
		}
	}

private:
	void DrawListItemLabel( vgui::Label *pLabel )
	{
		if ( !pLabel )
			return;

		bool bHasFocus = HasFocus() || IsSelected();

		Color textColor = m_bLocked ? m_LockedColor : m_TextColor;
		if ( bHasFocus && !m_bLocked )
		{
			textColor = m_FocusColor;
		}

		int panelWide, panelTall;
		GetSize( panelWide, panelTall );

		int x, y, labelWide, labelTall;
		pLabel->GetBounds( x, y, labelWide, labelTall );

		wchar_t szUnicode[512];
		pLabel->GetText( szUnicode, sizeof( szUnicode ) );
		int len = V_wcslen( szUnicode );

		int textWide, textTall;
		surface()->GetTextSize( m_hTextFont, szUnicode, textWide, textTall );

		// vertical center
		y += ( labelTall - textTall ) / 2 + m_nTextOffsetY;

		vgui::surface()->DrawSetTextFont( m_hTextFont );
		vgui::surface()->DrawSetTextPos( x, y );
		vgui::surface()->DrawSetTextColor( textColor );
		vgui::surface()->DrawPrintText( szUnicode, len );
	}

	bool ActivateSelectedItem()
	{
		ChapterListItem* pListItem = static_cast< ChapterListItem* >( m_pListCtrlr->GetSelectedPanelItem() );
		if ( !pListItem || pListItem->IsLocked() )
			return false;

		int nChapterIndex = pListItem->GetChapterIndex();
		if ( nChapterIndex > 0 )
		{
			const char *pMapName = NULL;


			if ( nChapterIndex )
			{
				pMapName = "w0_alltest.bsp"/*CBaseModPanel::GetSingleton().ChapterToMapName( nChapterIndex )/;
			}

			if ( pMapName && pMapName[0] )
			{

				/*
				KeyValues *pSettings = new KeyValues( "FadeOutStartGame" );
				KeyValues::AutoDelete autodelete_pSettings( pSettings );
				pSettings->SetString( "map", pMapName );
				pSettings->SetString( "reason", m_pDialog->IsCommentaryDialog() ? "commentary" : "newgame" );
				CBaseModPanel::GetSingleton().OpenWindow( WT_FADEOUTSTARTGAME, m_pDialog, true, pSettings );/
				return true;
			}
		}

		return false;
	}

	CNewGameDialog		*m_pDialog;

	GenericPanelList	*m_pListCtrlr;
	vgui::HFont			m_hTextFont;
	int					m_nChapterIndex;

	Color				m_TextColor;
	Color				m_FocusColor;
	Color				m_DisabledColor;
	Color				m_CursorColor;
	Color				m_LockedColor;
	Color				m_MouseOverCursorColor;

	bool				m_bSelected;
	bool				m_bHasMouseover;
	bool				m_bLocked;

	int					m_nTextOffsetY;
};

CNewGameDialog::CNewGameDialog( vgui::Panel *pParent, const char *pPanelName, bool bIsCommentaryDialog ) : BaseClass( pParent, pPanelName, false, true )
{
	SetProportional( true );
	SetDeleteSelfOnClose( true );

	m_bIsCommentaryDialog = bIsCommentaryDialog;

	m_pChapterImage = NULL;

	m_nAllowedChapters = 0;

	m_nVignetteImageId = -1;
	m_nChapterImageId = -1;
	m_bDrawAsLocked = false;

	m_pChapterList = new GenericPanelList( this, "ChapterList", GenericPanelList::ISM_PERITEM | GenericPanelList::ISM_ALPHA_INVISIBLE );
	m_pChapterList->SetPaintBackgroundEnabled( false );	

	m_pChapterLabel = new ChapterLabel( this, "ChapterText" );

	const char *pDialogTitle = bIsCommentaryDialog ? "#L4D360UI_GameSettings_Commentary" : "#PORTAL2_NewGame";
	//SetDialogTitle( pDialogTitle );

	//SetFooterEnabled( true );

	// parse out the chapters off disk
	static const int MAX_CHAPTERS = 32;
	chapter_t chapters[MAX_CHAPTERS];

	char szFullFileName[MAX_PATH];
	int chapterIndex = 0;

	FileFindHandle_t findHandle = FILESYSTEM_INVALID_FIND_HANDLE;
	const char *fileName = "cfg/chapter*.cfg";
	fileName = g_pFullFileSystem->FindFirst( fileName, &findHandle );
	while ( fileName && chapterIndex < MAX_CHAPTERS )
	{
		// Only load chapter configs from the current mod's cfg dir
		// or else chapters appear that we don't want!
		Q_snprintf( szFullFileName, sizeof(szFullFileName), "cfg/%s", fileName );
		FileHandle_t f = g_pFullFileSystem->Open( szFullFileName, "rb", "MOD" );
		if ( f )
		{	
			// don't load chapter files that are empty, used in the demo
			if ( g_pFullFileSystem->Size(f) > 0	)
			{
				Q_strncpy(chapters[chapterIndex].filename, fileName, sizeof(chapters[chapterIndex].filename));
				++chapterIndex;
			}
			g_pFullFileSystem->Close( f );
		}
		fileName = g_pFullFileSystem->FindNext(findHandle);
	}
}

CNewGameDialog::~CNewGameDialog()
{
	delete m_pChapterList;
	delete m_pChapterLabel;
}
int CNewGameDialog::GetImageId( const char *pImageName )
{
	int nImageId = vgui::surface()->DrawGetTextureId( pImageName );
	if ( nImageId == -1 )
	{
		nImageId = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile( nImageId, pImageName, true, false );	
	}

	return nImageId;
}
void CNewGameDialog::OnCommand( char const *szCommand )
{
	if ( !Q_strcmp( szCommand, "Back" ) )
	{
		// Act as though 360 back button was pressed
		OnKeyCodePressed( ButtonCodeToJoystickButtonCode( KEY_XBUTTON_B, CBaseModPanel::GetSingleton().GetLastActiveUserId() ) );
		return;
	}

	BaseClass::OnCommand( szCommand );
}

void CNewGameDialog::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );
	
	m_pChapterImage = dynamic_cast< ImagePanel* >( FindChildByName( "ChapterImage" ) );

	m_nVignetteImageId = GetImageId( "vgui/chapters/vignette" );

	// determine allowed number of unlocked chapters

	m_nAllowedChapters = 0/*BaseModUI::CBaseModPanel::GetSingleton().GetChapterProgress()/;
	if ( !m_nAllowedChapters )
	{
		m_nAllowedChapters = 1;
	}
	int nNumChapters = 5/*BaseModUI::CBaseModPanel::GetSingleton().GetNumChapters()/;

	// the list will be all coop commentary maps and the sp maps 
	int nNumListItems = nNumChapters;

	// reserve a dead entry, chapters start at [1..]
	m_ChapterImages.AddToTail( -1 );

	// add the sp chapters
	int nNoSaveGameImageId = GetImageId( "vgui/no_save_game" );

	for ( int i = 0; i < nNumChapters; i++ )
	{
		// by default, chapter is still locked
		int nImageId = nNoSaveGameImageId;
		if ( i + 1 <= m_nAllowedChapters )
		{	
			// get the unlocked chapter images
			nImageId = GetImageId( CFmtStr( "vgui/chapters/chapter%d", i + 1 ) );
		}
		m_ChapterImages.AddToTail( nImageId );
	}
	
	for ( int i = 0; i < nNumListItems; i++ )
	{
		ChapterListItem *pItem = m_pChapterList->AddPanelItem< ChapterListItem >( "newgame_chapteritem" );
		if ( pItem )
		{
			pItem->SetChapterIndex( i + 1 );
		}
	}

	// the chapter image will get set based on the initial selection
	SetChapterImage( 0 );

	m_pChapterList->SetScrollBarVisible( true );

	if ( m_ActiveControl )
	{
		m_ActiveControl->NavigateFrom();
	}
	m_pChapterList->NavigateTo();

	m_pChapterList->SelectPanelItem( 0, GenericPanelList::SD_DOWN, true, false );

	UpdateFooter();
}

void CNewGameDialog::Activate()
{
	BaseClass::Activate();

	m_pChapterList->NavigateTo();

	UpdateFooter();
}

void CNewGameDialog::UpdateFooter()
{
	CBaseModFooterPanel *pFooter = BaseModUI::CBaseModPanel::GetSingleton().GetFooterPanel();
	if ( pFooter )
	{
		int visibleButtons = FB_BBUTTON;

		ChapterListItem *pListItem;
		pListItem = static_cast< ChapterListItem* >( m_pChapterList->GetSelectedPanelItem() );
		if ( pListItem && !pListItem->IsLocked() )
		{
			visibleButtons |= FB_ABUTTON;
		}

		pFooter->SetButtons( visibleButtons, FF_AB_ONLY, false );
		pFooter->SetButtonText( FB_ABUTTON, "#L4D360UI_Select" );
		pFooter->SetButtonText( FB_BBUTTON, "#L4D360UI_Back" );
	}
}

void CNewGameDialog::OnItemSelected( const char *pPanelName )
{
	if ( !m_bLayoutLoaded )
		return;

	ChapterListItem* pListItem = static_cast< ChapterListItem* >( m_pChapterList->GetSelectedPanelItem() );
	SetChapterImage( pListItem ? pListItem->GetChapterIndex() : 0, pListItem->IsLocked() );

	// Set active state
	for ( int i = 0; i < m_pChapterList->GetPanelItemCount(); i++ )
	{
		ChapterListItem *pItem = dynamic_cast< ChapterListItem* >( m_pChapterList->GetPanelItem( i ) );
		if ( pItem )
		{
			pItem->SetSelected( pItem == pListItem );
		}
	}

	m_ActiveControl = pListItem;

	UpdateFooter();
}

void CNewGameDialog::SetChapterImage( int nChapterIndex, bool bIsLocked )
{
	if ( m_pChapterLabel )
	{
		m_pChapterLabel->SetVisible( nChapterIndex != 0 );
	}

	if ( nChapterIndex )
	{
		m_pChapterLabel->SetChapterIndex( nChapterIndex );
	}

	m_nChapterImageId = m_ChapterImages[nChapterIndex];
	m_bDrawAsLocked = bIsLocked;
}

void CNewGameDialog::PaintBackground()
{
	BaseClass::PaintBackground();

	DrawChapterImage();
}

void CNewGameDialog::DrawChapterImage()
{
	if ( !m_pChapterImage || m_nChapterImageId == -1 )
		return;

	int x, y, wide, tall;
	m_pChapterImage->GetBounds( x, y, wide, tall );

	surface()->DrawSetColor( Color( 255, 255, 255, m_bDrawAsLocked ? IsX360() ? 240 : 200 : 255 ) );
	surface()->DrawSetTexture( m_nChapterImageId );
	surface()->DrawTexturedRect( x, y, x+wide, y+tall );
	surface()->DrawSetTexture( m_nVignetteImageId );
	surface()->DrawTexturedRect( x, y, x+wide, y+tall );
}

void CNewGameDialog::OnKeyCodePressed( vgui::KeyCode code )
{
	{
		// handle button presses by the footer
		ChapterListItem* pListItem = static_cast< ChapterListItem* >( m_pChapterList->GetSelectedPanelItem() );
		if ( pListItem )
		{
			if ( code == KEY_XBUTTON_A )
			{
				pListItem->OnKeyCodePressed( code );
				return;
			}
		}
	}

	BaseClass::OnKeyCodePressed( code );
}

}; // namespace BaseModUI
*/