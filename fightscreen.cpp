#include "fightscreen.h"

#include <stdio.h>

#include <prism/input.h>
#include <prism/stagehandler.h>
#include <prism/collisionhandler.h>
#include <prism/system.h>
#include <prism/mugenanimationreader.h>
#include <prism/mugenanimationhandler.h>
#include <prism/mugentexthandler.h>
#include <prism/clipboardhandler.h>
#include <prism/memorystack.h>
#include <prism/debug.h>
#include <prism/netplay.h>
#include <prism/log.h>

#include "stage.h"
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
namespace prism {
	void setN3DSKeptIndicesSmaller(int tIsSmaller);   // 3ds/texture_3ds.cpp
}
#endif
#include "mugencommandreader.h"
#include "mugenstatereader.h"
#include "playerdefinition.h"
#include "mugencommandhandler.h"
#include "mugenstatehandler.h"
#include "collision.h"
#include "fightui.h"
#include "mugenexplod.h"
#include "gamelogic.h"
#include "config.h"
#include "playerhitdata.h"
#include "titlescreen.h"
#include "projectile.h"
#include "ai.h"
#include "titlescreen.h"
#include "mugenassignmentevaluator.h"
#include "mugenstatecontrollers.h"
#include "mugenanimationutilities.h"
#include "fightdebug.h"
#include "fightresultdisplay.h"
#include "dolmexicadebug.h"
#include "osuhandler.h"
#include "mugensound.h"
#include "pausecontrollers.h"
#include "trainingmodemenu.h"
#include "storyhelper.h"
#include "dolmexicastoryscreen.h"
#include "fightnetplay.h"
#include "loadprofile.h"
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
#include "pausemenu3ds.h"
#endif
#ifdef __3DS__
#include "fighttrace3ds.h"
#endif
#include "specialpanels3ds.h"
#include "watchpanel3ds.h"
#include "titleinfo3ds.h"
#include "smoothtext3ds.h"

#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
namespace prism {
	int pinN3DSStreamedTextures(size_t tKeepFree, size_t* oPinnedBytes);   // 3ds/texture_3ds.cpp
}
#endif

static struct {
	void(*mWinCB)();
	void(*mLoseCB)();
	MemoryStack mMemoryStack;
} gFightScreenData;

static void setFightScreenGameSpeed() {
	if (isDebugOverridingTimeDilatation()) return;

	int gameSpeed = getGlobalGameSpeed();
	if (gameSpeed < 0) {
		float baseFactor = (-gameSpeed) / 9.0f;
		float speedFactor = 1 - 0.75f * baseFactor;
		setWrapperTimeDilatation(speedFactor * getConfigGameSpeedTimeFactor());
	}
	else if (gameSpeed > 0) {
		float baseFactor = gameSpeed / 9.0f;
		float speedFactor = 1 + baseFactor;
		setWrapperTimeDilatation(speedFactor * getConfigGameSpeedTimeFactor());
	}
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	// Back to normal too (the 3DS's pause menu changes the speed in a
	// fight: from fast to normal it stayed fast).
	else setWrapperTimeDilatation(getConfigGameSpeedTimeFactor());
#endif
}

void applyFightScreenGameSpeed() {
	setFightScreenGameSpeed();
}

extern int gDebugAssignmentAmount;
extern int gDebugStateControllerAmount;
namespace prism {
	extern int gDebugStringMapAmount;
}
extern int gPruneAmount;

static void exitFightScreenCB(void* tCaller);

static void loadFightScreen() {
	setWrapperBetweenScreensCB(exitFightScreenCB, NULL);
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	// The pause menu's and touch panels' letters before the characters'
	// sprites: after New Kyouki's, no texture memory was left for them (the
	// menu came up without its words).
	acquireSmoothTextFonts();
	preloadN3DSWatchPanel();
#endif

	logMemoryState();
	logg("create mem stack");
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	// [3DS] Its 3 MB taken once, in the first fight (the heap still in one
	// piece), and kept for all the next ones: asked for again each fight,
	// after many the heap had 47 MB free but not 3 MB in one piece (Freddy
	// Krueger vs Art the Clown ended the game). Outside the screen's blocks
	// (freed when it ends), so not given back. What does not fit in it goes
	// outside it, as before.
	{
		static void* block = NULL;
		static uint32_t size = 1024 * 1024 * 3;
		while (!block) {
			block = malloc(size);
			if (block || size <= 64 * 1024) break;
			size /= 2;
		}
		if (block) {
			gFightScreenData.mMemoryStack.mAddress = block;
			gFightScreenData.mMemoryStack.mSize = size;
			gFightScreenData.mMemoryStack.mOffset = 0;
			gFightScreenData.mMemoryStack.mAmount = 0;
		}
		else gFightScreenData.mMemoryStack = createMemoryStack(1024 * 1024 * 3);
	}
#else
	gFightScreenData.mMemoryStack = createMemoryStack(1024 * 1024 * 3);
#endif
	
	logMemoryState();
	logg("init evaluators");
	
	gDebugAssignmentAmount = 0;
	gDebugStateControllerAmount = 0;
	gDebugStringMapAmount = 0;
	gPruneAmount = 0;

	setupDreamGameCollisions();
	setupDreamAssignmentReader(&gFightScreenData.mMemoryStack);
	setupDreamAssignmentEvaluator();
	setupDreamMugenStateControllerHandler(&gFightScreenData.mMemoryStack);
	
	logMemoryState();
	logg("init custom handlers");

	instantiateActor(getMugenAnimationUtilityHandler());
	instantiateActor(getDreamAIHandler());
	instantiateActor(getProjectileHandler());
	instantiateActor(getDolmexicaSoundHandler());

	instantiateActor(getDreamMugenCommandHandler());
	instantiateActor(getPreStateMachinePlayersBlueprint());
	if (getGameMode() == GAME_MODE_NETPLAY) {
		instantiateActor(getFightNetplayBlueprint());
	}
	instantiateActor(getDreamMugenStateHandler());
	if (isMugenDebugActive()) {
		int actorID = instantiateActor(getFightDebug());
		setActorUnpausable(actorID);
	}
	
	logMemoryState();
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	// [3DS] A heavy fight's sprites kept smaller (zstd): more of them in the heap.
	setN3DSKeptIndicesSmaller(isN3DSHeavyFight());
#ifndef __3DS__
	if (getenv("N3DS_SIM_INDEX_RUNS")) setN3DSKeptIndicesSmaller(0);
#endif
#endif
	logg("init stage");

	instantiateActor(getDreamStageBP());

	logMemoryState();
	logg("init players");

	loadPlayers(&gFightScreenData.mMemoryStack);
	
	if (hasStoryHelper()) {
		instantiateActor(getDolmexicaStoryActor());
	}

	instantiateActor(getDreamFightUIBP());
	instantiateActor(getDreamGameLogic());

	instantiateActor(getFightResultDisplay());
	
	logMemoryState();
	logg("shrinking memory stack");
	resizeMemoryStackToCurrentSize(&gFightScreenData.mMemoryStack);
	logMemoryState();
	shutdownDreamAssignmentReader();

	logg("[Fightscreen] Load player sprites");
	loadPlayerSprites();
	logg("[Fightscreen] Set ui faces");
	setUIFaces();
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	recordN3DSLastFight();
#endif

	logg("[Fightscreen] Play stage music");
	playDreamStageMusic();
	if (getGameMode() == GAME_MODE_OSU) {
		instantiateActor(getOsuHandler());
	}

	logg("[Fightscreen] Init post-load actors");
	instantiateActor(getPauseControllerHandler());
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	setActorUnpausable(instantiateActor(getPauseMenu3DS()));
#endif
#ifdef __3DS__
	instantiateActor(getFightTrace3DS());
#endif
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	instantiateActor(getSpecialPanels3DS());
	instantiateActor(getWatchPanel3DS());
#endif
	instantiateActor(getPostStateMachinePlayersBlueprint());
	instantiateActor(getDreamExplodHandler());

	if (isInDevelopMode()) {
		instantiateActor(getDolmexicaDebug());
	}

	if (getGameMode() == GAME_MODE_TRAINING) {
		int actorID = instantiateActor(getTrainingModeMenu());
		setActorUnpausable(actorID);
	}

	setFightScreenGameSpeed();

	logg("[Fightscreen] Set initial player states");
	changePlayerState(getRootPlayer(0), 5900);
	changePlayerState(getRootPlayer(1), 5900);
	setPlayerStatemachineToUpdateAgain(getRootPlayer(0));
	setPlayerStatemachineToUpdateAgain(getRootPlayer(1));
	logg("[Fightscreen] Fight screen loaded");
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	// [3DS] The heap left over after loading: streamed sprites read now and
	// kept, the biggest first (a full-screen X-ray image read from the card
	// in the fight came in flashes), 7 MB still left for the fight.
	pinN3DSStreamedTextures(7 * 1024 * 1024, NULL);
#endif

	

	logMemoryState();

	logFormat("assignments: %d", gDebugAssignmentAmount);
	logFormat("controllers: %d", gDebugStateControllerAmount);
	logFormat("maps: %d", gDebugStringMapAmount);
	logFormat("memory blocks: %d", getAllocatedMemoryBlockAmount());
	logFormat("memory stack used: %d", (int)gFightScreenData.mMemoryStack.mOffset);

	// [3DS] The fight's warnings in the compatibility log, under the fight.
	char compatName[400];
	snprintf(compatName, sizeof(compatName), "fight: %s vs %s, stage %s", getPlayerName(getRootPlayer(0)), getPlayerName(getRootPlayer(1)), getDreamStageName());
	beginCompatSection(compatName);
	addCompatLine("loaded: heap %d KB used, textures %d KB free", getCompatHeapUsedKB(), getCompatTextureFreeKB());
}

static void unloadFightScreen() {
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	setN3DSKeptIndicesSmaller(0);
	releaseSmoothTextFonts();
#endif
	endCompatSection();
	unloadPlayers();
	resetGameMode();
	shutdownDreamMugenStateControllerHandler();
	shutdownDreamAssignmentEvaluator();
}

static void drawFightScreen() {
	drawPlayers();
}

static Screen gDreamFightScreen;

static Screen* getDreamFightScreen() {
	gDreamFightScreen = makeScreen(loadFightScreen, NULL, drawFightScreen, unloadFightScreen);
	return &gDreamFightScreen;
}

static void loadFightFonts(void* tCaller) {
	(void)tCaller;
	loadMugenFightFonts();
}

static void exitFightScreenCB(void* /*tCaller*/) {
	if (!isDebugOverridingTimeDilatation()) {
		setWrapperTimeDilatation(getConfigGameSpeedTimeFactor());
	}
	unloadMugenFonts();
	loadMugenSystemFonts();
}

void startFightScreen(void(*tWinCB)(), void(*tLoseCB)()) {
	gFightScreenData.mWinCB = tWinCB;
	gFightScreenData.mLoseCB = tLoseCB;
	setWrapperBetweenScreensCB(loadFightFonts, NULL);
	setNewScreen(getDreamFightScreen());
}

void reloadFightScreen()
{
	startFightScreen(gFightScreenData.mWinCB, gFightScreenData.mLoseCB);
}

void stopFightScreenWin() {
	setWrapperBetweenScreensCB(exitFightScreenCB, NULL);
	if (!gFightScreenData.mWinCB) return;

	gFightScreenData.mWinCB();
}

void stopFightScreenLose()
{
	setWrapperBetweenScreensCB(exitFightScreenCB, NULL);
	if (!gFightScreenData.mLoseCB) {
		setNewScreen(getDreamTitleScreen());
		return;
	}

	gFightScreenData.mLoseCB();
}

void stopFightScreenToFunction(void(*tFunc)()) {
	setWrapperBetweenScreensCB(exitFightScreenCB, NULL);
	tFunc();
}

void stopFightScreenToFixedScreen(Screen* tNextScreen) {
	setWrapperBetweenScreensCB(exitFightScreenCB, NULL);
	setNewScreen(tNextScreen);
}

Screen* getDreamFightScreenForTesting() {
	return getDreamFightScreen();
}