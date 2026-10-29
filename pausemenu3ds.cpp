#include "pausemenu3ds.h"

#include <vector>

#include <prism/wrapper.h>
#include <prism/input.h>
#include <prism/animation.h>
#include <prism/screeneffect.h>
#include <prism/mugentexthandler.h>
#include <prism/system.h>

#include "fightscreen.h"
#include "gamelogic.h"
#include "titlescreen.h"
#include "stage.h"
#include "arcademode.h"
#include "freeplaymode.h"
#include "versusmode.h"
#include "trainingmode.h"
#include "survivalmode.h"
#include "watchmode.h"
#include "superwatchmode.h"

#define PAUSE_MENU_Z 95

typedef enum {
	PAUSE_MENU_CONTINUE,
	PAUSE_MENU_CHARACTER_SELECT,
	PAUSE_MENU_TITLE_SCREEN,
} PauseMenuItem;

static struct {
	int mIsOpen;
	std::vector<PauseMenuItem> mItems;
	std::vector<int> mTextIDs;
	int mSelected;
	AnimationHandlerElement* mBackground;
} gPauseMenu3DSData;

// The mode's character select, for the modes that have one.
static void (*getCharacterSelectStart())() {
	switch (getGameMode()) {
	case GAME_MODE_ARCADE: return startArcadeMode;
	case GAME_MODE_FREE_PLAY: return startFreePlayMode;
	case GAME_MODE_VERSUS: return startVersusMode;
	case GAME_MODE_TRAINING: return startTrainingMode;
	case GAME_MODE_SURVIVAL: return startSurvivalMode;
	case GAME_MODE_WATCH: return startWatchMode;
	case GAME_MODE_SUPER_WATCH: return startSuperWatchMode;
	default: return NULL;
	}
}

static const char* getItemText(PauseMenuItem tItem) {
	switch (tItem) {
	case PAUSE_MENU_CONTINUE: return "CONTINUAR";
	case PAUSE_MENU_CHARACTER_SELECT: return "ELEGIR PERSONAJES";
	default: return "MENU PRINCIPAL";
	}
}

static void updateItemColors() {
	for (size_t i = 0; i < gPauseMenu3DSData.mTextIDs.size(); i++) {
		if ((int)i == gPauseMenu3DSData.mSelected) setMugenTextColorRGB(gPauseMenu3DSData.mTextIDs[i], 1, 1, 0);
		else setMugenTextColorRGB(gPauseMenu3DSData.mTextIDs[i], 1, 1, 1);
	}
}

static void openPauseMenu() {
	pauseWrapper();
	gPauseMenu3DSData.mIsOpen = 1;
	gPauseMenu3DSData.mSelected = 0;

	const auto sz = getScreenSize();
	const auto margin = getDreamScreenSideMargin(sz.x);
	gPauseMenu3DSData.mBackground = playOneFrameAnimationLoop(Vector3D(-margin, 0, PAUSE_MENU_Z), getEmptyWhiteTextureReference());
	setAnimationSize(gPauseMenu3DSData.mBackground, Vector3D(sz.x + 2 * margin, sz.y, 1), Vector3D(0, 0, 0));
	setAnimationColor(gPauseMenu3DSData.mBackground, 0, 0, 0);
	setAnimationTransparency(gPauseMenu3DSData.mBackground, 0.6f);

	gPauseMenu3DSData.mItems.clear();
	gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_CONTINUE);
	if (getCharacterSelectStart()) gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_CHARACTER_SELECT);
	gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_TITLE_SCREEN);

	// The fight's name font (font3 in Elecbyte's fight.def), else the
	// debug font, twice its size.
	const int font = hasMugenFont(3) ? 3 : -1;
	const float spacing = 18;
	const float startY = sz.y / 2.0f - spacing * (gPauseMenu3DSData.mItems.size() - 1) / 2.0f + 4;
	gPauseMenu3DSData.mTextIDs.clear();
	for (size_t i = 0; i < gPauseMenu3DSData.mItems.size(); i++) {
		const int id = addMugenTextMugenStyle(getItemText(gPauseMenu3DSData.mItems[i]), Vector3D(sz.x / 2.0f, startY + spacing * i, PAUSE_MENU_Z + 1), Vector3DI(font, 0, 0));
		if (font == -1) setMugenTextScale(id, 2);
		gPauseMenu3DSData.mTextIDs.push_back(id);
	}
	updateItemColors();
}

static void closePauseMenu() {
	for (const auto id : gPauseMenu3DSData.mTextIDs) removeMugenText(id);
	gPauseMenu3DSData.mTextIDs.clear();
	removeHandledAnimation(gPauseMenu3DSData.mBackground);
	gPauseMenu3DSData.mBackground = NULL;
	gPauseMenu3DSData.mIsOpen = 0;
	resumeWrapper();
}

static void selectItem() {
	const auto item = gPauseMenu3DSData.mItems[gPauseMenu3DSData.mSelected];
	if (item == PAUSE_MENU_CONTINUE) {
		closePauseMenu();
		return;
	}

	const auto characterSelectStart = getCharacterSelectStart();
	closePauseMenu();
	if (item == PAUSE_MENU_CHARACTER_SELECT) stopFightScreenToFunction(characterSelectStart);
	else stopFightScreenToFixedScreen(getDreamTitleScreen());
}

static void loadPauseMenu(void* /*tData*/) {
	gPauseMenu3DSData.mIsOpen = 0;
	gPauseMenu3DSData.mItems.clear();
	gPauseMenu3DSData.mTextIDs.clear();
	gPauseMenu3DSData.mBackground = NULL;
}

static void updatePauseMenu(void* /*tData*/) {
	if (!gPauseMenu3DSData.mIsOpen) {
		// Only while fighting: START skips the intro and the win pose too.
		if (getDreamRoundStateNumber() == 2 /* ROUND_STATE_FIGHT */ && !isWrapperPaused() && hasPressedStartFlankSingle(0)) {
			openPauseMenu();
		}
		return;
	}

	const int amount = int(gPauseMenu3DSData.mItems.size());
	if (hasPressedUpFlankSingle(0)) {
		gPauseMenu3DSData.mSelected = (gPauseMenu3DSData.mSelected + amount - 1) % amount;
		updateItemColors();
	}
	else if (hasPressedDownFlankSingle(0)) {
		gPauseMenu3DSData.mSelected = (gPauseMenu3DSData.mSelected + 1) % amount;
		updateItemColors();
	}
	else if (hasPressedAFlankSingle(0)) {
		selectItem();
	}
	else if (hasPressedBFlankSingle(0) || hasPressedStartFlankSingle(0)) {
		closePauseMenu();
	}
}

ActorBlueprint getPauseMenu3DS() {
	return makeActorBlueprint(loadPauseMenu, NULL, updatePauseMenu);
}
