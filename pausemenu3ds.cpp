#include "pausemenu3ds.h"

#include <vector>
#include <string>
#include <stdio.h>
#include <string.h>

#include <prism/wrapper.h>
#include <prism/input.h>
#include <prism/animation.h>
#include <prism/screeneffect.h>
#include <prism/system.h>

#include "fightscreen.h"
#include "fightui.h"
#include "gamelogic.h"
#include "titlescreen.h"
#include "stage.h"
#include "config.h"
#include "arcademode.h"
#include "freeplaymode.h"
#include "versusmode.h"
#include "trainingmode.h"
#include "survivalmode.h"
#include "watchmode.h"
#include "superwatchmode.h"
#include "specialpanels3ds.h"
#include "smoothtext3ds.h"
#include "playerdefinition.h"

#ifndef __3DS__
// The simulator (tests): the screen's shape kept here only.
static int gIsSimScreenStretched = 1;
static void setN3DSScreenStretched(int tIsStretched) { gIsSimScreenStretched = tIsStretched; }
static int isN3DSScreenStretched() { return gIsSimScreenStretched; }
#endif

#define PAUSE_MENU_Z 95
#define PAUSE_MENU_SPACING 14

typedef enum {
	PAUSE_MENU_CONTINUE,
	PAUSE_MENU_RESTART_ROUND,
	PAUSE_MENU_RESTART_FIGHT,
	PAUSE_MENU_SETTINGS,
	PAUSE_MENU_CHARACTER_SELECT,
	PAUSE_MENU_TITLE_SCREEN,

	// AJUSTES
	PAUSE_MENU_CPU_LEVEL,
	PAUSE_MENU_TIME,
	PAUSE_MENU_MUSIC,
	PAUSE_MENU_EFFECTS,
	PAUSE_MENU_SPEED,
	PAUSE_MENU_BARS,
	PAUSE_MENU_FPS,
	PAUSE_MENU_SCREEN,
	PAUSE_MENU_WIDE_STAGES,
	PAUSE_MENU_SPECIAL_PANELS,
	PAUSE_MENU_BACK,
} PauseMenuItem;

// The screen's shape (stretched over the whole top screen, or normal: the
// game's 4:3 with black bands at the sides), kept on the SD card. An older
// "wide" (more of the stage at the sides) is read as normal.
#define SCREEN_SETTINGS_PATH "sdmc:/3ds/dolmexica/screen.txt"

// How a stage wider than 4:3 (1280x720) is shown: whole with black bands
// above and below, as MUGEN 1.1 in a 4:3 window (the default), or cut at
// its sides to fill the screen.
#define WIDE_STAGES_SETTINGS_PATH "sdmc:/3ds/dolmexica/widestages.txt"

// The pause menu's settings (time, volumes, speed, bars, frame counter),
// "key value" lines; over mugen.cfg's where they are the same.
#define PAUSE_SETTINGS_PATH "sdmc:/3ds/dolmexica/ajustes.txt"

static const int TIME_CHOICES[] = { 99, 60, 30, -1 };   // -1: none
static const int SPEED_CHOICES[] = { -6, -3, 0, 3, 6 };   // MUGEN's GameSpeed
static const char* SPEED_NAMES[] = { "MUY LENTA", "LENTA", "NORMAL", "RÁPIDA", "MUY RÁPIDA" };

static struct {
	int mIsOpen;
	int mIsInSettings;
	std::vector<PauseMenuItem> mItems;
	std::vector<SmoothText> mLines;
	SmoothText mHeader;
	int mSelected;
	int mSelectedMain;   // to come back to from the settings
	AnimationHandlerElement* mBackground;

	int mAreBarsHidden;
	int mIsShowingFPS;
	SmoothText mFPSText;
	int mFPSUpdates;
	uint64_t mFPSStart;
} gPauseMenu3DSData;

static int getSpeedIndex() {
	const int speed = getGlobalGameSpeed();
	int best = 2;
	for (int i = 0; i < 5; i++) if (abs(SPEED_CHOICES[i] - speed) < abs(SPEED_CHOICES[best] - speed)) best = i;
	return best;
}

static void savePauseSettings() {
	FILE* file = fopen(PAUSE_SETTINGS_PATH, "w");
	if (!file) return;
	fprintf(file, "time %d\n", isGlobalTimerInfinite() ? -1 : getGlobalTimerDuration());
	fprintf(file, "music %d\n", getUnscaledGameWavVolume());
	fprintf(file, "effects %d\n", getUnscaledGameMidiVolume());
	fprintf(file, "speed %d\n", getGlobalGameSpeed());
	fprintf(file, "bars %d\n", !gPauseMenu3DSData.mAreBarsHidden);
	fprintf(file, "fps %d\n", gPauseMenu3DSData.mIsShowingFPS);
	fclose(file);
}

static void loadPauseSettings() {
	FILE* file = fopen(PAUSE_SETTINGS_PATH, "r");
	if (!file) return;
	char key[32];
	int value;
	while (fscanf(file, "%31s %d", key, &value) == 2) {
		if (!strcmp(key, "time")) {
			if (value < 0) setGlobalTimerInfinite();
			else if (value > 0) setGlobalTimerDuration(value);
		}
		else if (!strcmp(key, "music") && value >= 0 && value <= 100) setUnscaledGameWavVolume(value);
		else if (!strcmp(key, "effects") && value >= 0 && value <= 100) setUnscaledGameMidiVolume(value);
		else if (!strcmp(key, "speed") && value >= -9 && value <= 9) setGlobalGameSpeed(value);
		else if (!strcmp(key, "bars")) gPauseMenu3DSData.mAreBarsHidden = !value;
		else if (!strcmp(key, "fps")) gPauseMenu3DSData.mIsShowingFPS = value;
	}
	fclose(file);
}

void loadN3DSScreenSetting() {
	FILE* file = fopen(SCREEN_SETTINGS_PATH, "r");
	if (file) {
		char text[32] = { 0 };
		if (fgets(text, sizeof(text), file)) setN3DSScreenStretched(strncmp(text, "stretched", 9) == 0);
		fclose(file);
	}
	file = fopen(WIDE_STAGES_SETTINGS_PATH, "r");
	if (file) {
		char text[32] = { 0 };
		if (fgets(text, sizeof(text), file)) setDreamWideStageLetterboxed(strncmp(text, "cut", 3) != 0);
		fclose(file);
	}
	loadPauseSettings();
}

static void saveN3DSWideStagesSetting() {
	FILE* file = fopen(WIDE_STAGES_SETTINGS_PATH, "w");
	if (!file) return;
	fputs(isDreamWideStageLetterboxed() ? "bands\n" : "cut\n", file);
	fclose(file);
}

static void saveN3DSScreenSetting() {
	FILE* file = fopen(SCREEN_SETTINGS_PATH, "w");
	if (!file) return;
	fputs(isN3DSScreenStretched() ? "stretched\n" : "normal\n", file);
	fclose(file);
}

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

// The computer's level: the first computer side's (0 if none).
static int getComputerLevel() {
	for (int i = 0; i < 2; i++) {
		const int level = getPlayerAILevel(getRootPlayer(i));
		if (level) return level;
	}
	return 0;
}

static std::string getItemText(PauseMenuItem tItem) {
	char text[64];
	switch (tItem) {
	case PAUSE_MENU_CONTINUE: return "CONTINUAR";
	case PAUSE_MENU_RESTART_ROUND: return "REINICIAR ROUND";
	case PAUSE_MENU_RESTART_FIGHT: return "REINICIAR COMBATE";
	case PAUSE_MENU_SETTINGS: return "AJUSTES";
	case PAUSE_MENU_CHARACTER_SELECT: return "ELEGIR PERSONAJES";
	case PAUSE_MENU_TITLE_SCREEN: return "MENÚ PRINCIPAL";
	case PAUSE_MENU_CPU_LEVEL: snprintf(text, sizeof(text), "NIVEL CPU  < %d >", getComputerLevel()); return text;
	case PAUSE_MENU_TIME:
		if (isGlobalTimerInfinite()) return "TIEMPO  < SIN LÍMITE >";
		snprintf(text, sizeof(text), "TIEMPO  < %d >", getGlobalTimerDuration());
		return text;
	case PAUSE_MENU_MUSIC: snprintf(text, sizeof(text), "MÚSICA  < %d >", getUnscaledGameWavVolume()); return text;
	case PAUSE_MENU_EFFECTS: snprintf(text, sizeof(text), "EFECTOS  < %d >", getUnscaledGameMidiVolume()); return text;
	case PAUSE_MENU_SPEED: snprintf(text, sizeof(text), "VELOCIDAD  < %s >", SPEED_NAMES[getSpeedIndex()]); return text;
	case PAUSE_MENU_BARS: return gPauseMenu3DSData.mAreBarsHidden ? "BARRAS  < OCULTAS >" : "BARRAS  < VISIBLES >";
	case PAUSE_MENU_FPS: return gPauseMenu3DSData.mIsShowingFPS ? "MOSTRAR FPS  < SÍ >" : "MOSTRAR FPS  < NO >";
	case PAUSE_MENU_SCREEN: return isN3DSScreenStretched() ? "PANTALLA  < COMPLETA >" : "PANTALLA  < NORMAL 4:3 >";
	case PAUSE_MENU_WIDE_STAGES: return isDreamWideStageLetterboxed() ? "ESCENARIOS 16:9  < CON FRANJAS >" : "ESCENARIOS 16:9  < RECORTADOS >";
	case PAUSE_MENU_SPECIAL_PANELS: return getN3DSSpecialPanelAmount() == 0 ? "PANELES TÁCTILES  < NO >" : getN3DSSpecialPanelAmount() == 8 ? "PANELES TÁCTILES  < 8 >" : "PANELES TÁCTILES  < 4 >";
	default: return "VOLVER";
	}
}

static float getLineTop(int i) {
	const auto sz = getScreenSize();
	const int amount = int(gPauseMenu3DSData.mItems.size());
	return sz.y / 2.0f - PAUSE_MENU_SPACING * amount / 2.0f + 8 + PAUSE_MENU_SPACING * i;
}

static Vector3D getLineColor(int i) {
	return i == gPauseMenu3DSData.mSelected ? Vector3D(1, 1, 0.2) : Vector3D(1, 1, 1);
}

static void showLine(int i) {
	removeSmoothText(&gPauseMenu3DSData.mLines[i]);
	// The small letters: the top screen stretches the game's 4:3 wider, the
	// large ones looked coarse there.
	addSmoothTextCentered(&gPauseMenu3DSData.mLines[i], SMOOTH_TEXT_SMALL, getItemText(gPauseMenu3DSData.mItems[i]), getScreenSize().x / 2.0f, getLineTop(i), PAUSE_MENU_Z + 1, 0, getLineColor(i));
}

static void updateItemColors() {
	for (size_t i = 0; i < gPauseMenu3DSData.mLines.size(); i++) setSmoothTextColor(&gPauseMenu3DSData.mLines[i], getLineColor(int(i)));
}

static void removeLines() {
	for (auto& line : gPauseMenu3DSData.mLines) removeSmoothText(&line);
	gPauseMenu3DSData.mLines.clear();
	removeSmoothText(&gPauseMenu3DSData.mHeader);
}

// The main items, or the settings (AJUSTES).
static void showItems(int tIsSettings, int tSelected) {
	removeLines();
	gPauseMenu3DSData.mIsInSettings = tIsSettings;
	gPauseMenu3DSData.mItems.clear();
	if (!tIsSettings) {
		gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_CONTINUE);
		gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_RESTART_ROUND);
		gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_RESTART_FIGHT);
		gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_SETTINGS);
		if (getCharacterSelectStart()) gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_CHARACTER_SELECT);
		gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_TITLE_SCREEN);
	}
	else {
		if (getComputerLevel()) gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_CPU_LEVEL);
		if (getGameMode() != GAME_MODE_TRAINING) gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_TIME);
		gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_MUSIC);
		gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_EFFECTS);
		gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_SPEED);
		gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_BARS);
		gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_FPS);
		gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_SCREEN);
		gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_WIDE_STAGES);
		if (isPlayerHuman(getRootPlayer(0))) gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_SPECIAL_PANELS);
		gPauseMenu3DSData.mItems.push_back(PAUSE_MENU_BACK);
	}
	gPauseMenu3DSData.mSelected = std::min(tSelected, int(gPauseMenu3DSData.mItems.size()) - 1);
	addSmoothTextCentered(&gPauseMenu3DSData.mHeader, SMOOTH_TEXT_SMALL, tIsSettings ? "AJUSTES" : "PAUSA", getScreenSize().x / 2.0f, getLineTop(0) - 18, PAUSE_MENU_Z + 1, 0, Vector3D(0.6, 0.8, 1));
	gPauseMenu3DSData.mLines.resize(gPauseMenu3DSData.mItems.size());
	for (size_t i = 0; i < gPauseMenu3DSData.mItems.size(); i++) showLine(int(i));
}

static void openPauseMenu() {
	pauseWrapper();
	gPauseMenu3DSData.mIsOpen = 1;

	const auto sz = getScreenSize();
	const auto margin = getDreamScreenSideMargin(sz.x);
	gPauseMenu3DSData.mBackground = playOneFrameAnimationLoop(Vector3D(-margin, 0, PAUSE_MENU_Z), getEmptyWhiteTextureReference());
	setAnimationSize(gPauseMenu3DSData.mBackground, Vector3D(sz.x + 2 * margin, sz.y, 1), Vector3D(0, 0, 0));
	setAnimationColor(gPauseMenu3DSData.mBackground, 0, 0, 0);
	setAnimationTransparency(gPauseMenu3DSData.mBackground, 0.6f);
	showItems(0, 0);
}

static void closePauseMenu() {
	removeLines();
	removeHandledAnimation(gPauseMenu3DSData.mBackground);
	gPauseMenu3DSData.mBackground = NULL;
	gPauseMenu3DSData.mIsOpen = 0;
	resumeWrapper();
}

// Left (-1) or right (1) on a setting, or A (1).
static void changeSetting(PauseMenuItem tItem, int tDelta) {
	switch (tItem) {
	case PAUSE_MENU_CPU_LEVEL: {
		const int level = (getComputerLevel() - 1 + tDelta + 8) % 8 + 1;
		for (int i = 0; i < 2; i++) {
			if (getPlayerAILevel(getRootPlayer(i))) setPlayerSelectedAILevel(i, level);
		}
		break;
	}
	case PAUSE_MENU_TIME: {
		int index = 0;
		for (int i = 0; i < 4; i++) {
			if ((TIME_CHOICES[i] < 0) == !!isGlobalTimerInfinite() && (TIME_CHOICES[i] < 0 || TIME_CHOICES[i] == getGlobalTimerDuration())) index = i;
		}
		index = (index + tDelta + 4) % 4;
		if (TIME_CHOICES[index] < 0) setGlobalTimerInfinite();
		else setGlobalTimerDuration(TIME_CHOICES[index]);
		// The round's clock from the new time now.
		resetDreamTimer();
		setTimerFinite();
		break;
	}
	case PAUSE_MENU_MUSIC: setUnscaledGameWavVolume(std::max(0, std::min(100, getUnscaledGameWavVolume() + 10 * tDelta))); break;
	case PAUSE_MENU_EFFECTS: setUnscaledGameMidiVolume(std::max(0, std::min(100, getUnscaledGameMidiVolume() + 10 * tDelta))); break;
	case PAUSE_MENU_SPEED: {
		const int index = std::max(0, std::min(4, getSpeedIndex() + tDelta));
		setGlobalGameSpeed(SPEED_CHOICES[index]);
		applyFightScreenGameSpeed();
		break;
	}
	case PAUSE_MENU_BARS: gPauseMenu3DSData.mAreBarsHidden = !gPauseMenu3DSData.mAreBarsHidden; break;
	case PAUSE_MENU_FPS: gPauseMenu3DSData.mIsShowingFPS = !gPauseMenu3DSData.mIsShowingFPS; break;
	// Changed at once (the stage's edges as seen fit from the next fight).
	case PAUSE_MENU_SCREEN:
		setN3DSScreenStretched(!isN3DSScreenStretched());
		saveN3DSScreenSetting();
		break;
	// From the next fight (the stage is laid out as it is loaded).
	case PAUSE_MENU_WIDE_STAGES:
		setDreamWideStageLetterboxed(!isDreamWideStageLetterboxed());
		saveN3DSWideStagesSetting();
		break;
	// 4, 8, none: the panels change at once (specialpanels3ds.cpp).
	case PAUSE_MENU_SPECIAL_PANELS: {
		static const int amounts[] = { 4, 8, 0 };
		int index = 0;
		for (int i = 0; i < 3; i++) if (amounts[i] == getN3DSSpecialPanelAmount()) index = i;
		setN3DSSpecialPanelAmount(amounts[(index + tDelta + 3) % 3]);
		break;
	}
	default: return;
	}
	savePauseSettings();
	showLine(gPauseMenu3DSData.mSelected);
}

static void selectItem() {
	const auto item = gPauseMenu3DSData.mItems[gPauseMenu3DSData.mSelected];
	if (item == PAUSE_MENU_CONTINUE) {
		closePauseMenu();
		return;
	}
	// The round again (as MUGEN's F4: life and position as at its start),
	// or the fight from its first round (gamelogic.cpp's restartFight).
	if (item == PAUSE_MENU_RESTART_ROUND || item == PAUSE_MENU_RESTART_FIGHT) {
		closePauseMenu();
		if (item == PAUSE_MENU_RESTART_ROUND) resetRound();
		else restartFight();
		return;
	}
	if (item == PAUSE_MENU_SETTINGS) {
		gPauseMenu3DSData.mSelectedMain = gPauseMenu3DSData.mSelected;
		showItems(1, 0);
		return;
	}
	if (item == PAUSE_MENU_BACK) {
		showItems(0, gPauseMenu3DSData.mSelectedMain);
		return;
	}
	if (item >= PAUSE_MENU_CPU_LEVEL) {
		changeSetting(item, 1);
		return;
	}

	const auto characterSelectStart = getCharacterSelectStart();
	closePauseMenu();
	if (item == PAUSE_MENU_CHARACTER_SELECT) stopFightScreenToFunction(characterSelectStart);
	else stopFightScreenToFixedScreen(getDreamTitleScreen());
}

// The frame counter: the game's frames a second at its speed (60 is full
// speed; fewer, the console falls behind), at the top right.
static void updateFPS() {
	if (!gPauseMenu3DSData.mIsShowingFPS) {
		removeSmoothText(&gPauseMenu3DSData.mFPSText);
		gPauseMenu3DSData.mFPSUpdates = 0;
		return;
	}
	const uint64_t now = getUnixTimestampMilliseconds();
	if (!gPauseMenu3DSData.mFPSUpdates) gPauseMenu3DSData.mFPSStart = now;
	gPauseMenu3DSData.mFPSUpdates++;
	const uint64_t elapsed = now - gPauseMenu3DSData.mFPSStart;
	if (elapsed < 500 && !gPauseMenu3DSData.mFPSText.empty()) return;
	if (elapsed < 500) return;
	const int speed = getGlobalGameSpeed();
	const float speedFactor = speed < 0 ? 1 - 0.75f * (-speed / 9.0f) : 1 + speed / 9.0f;
	const float fps = (gPauseMenu3DSData.mFPSUpdates - 1) * 1000.0f / elapsed / speedFactor;
	char text[32];
	snprintf(text, sizeof(text), "%.0f FPS", fps);
	removeSmoothText(&gPauseMenu3DSData.mFPSText);
	const auto sz = getScreenSize();
	const float margin = getDreamScreenSideMargin(sz.x);
	addSmoothText(&gPauseMenu3DSData.mFPSText, SMOOTH_TEXT_SMALL, text, sz.x + margin - 4 - getSmoothTextWidth(SMOOTH_TEXT_SMALL, text), 2, PAUSE_MENU_Z + 2, 0, fps >= 57 ? Vector3D(0.5, 1, 0.5) : fps >= 40 ? Vector3D(1, 1, 0.4) : Vector3D(1, 0.45, 0.4));
	gPauseMenu3DSData.mFPSUpdates = 1;
	gPauseMenu3DSData.mFPSStart = now;
}

static void loadPauseMenu(void* /*tData*/) {
	gPauseMenu3DSData.mIsOpen = 0;
	gPauseMenu3DSData.mIsInSettings = 0;
	gPauseMenu3DSData.mItems.clear();
	gPauseMenu3DSData.mLines.clear();
	gPauseMenu3DSData.mHeader.clear();
	gPauseMenu3DSData.mFPSText.clear();
	gPauseMenu3DSData.mFPSUpdates = 0;
	gPauseMenu3DSData.mBackground = NULL;
	acquireSmoothTextFonts();
}

static void unloadPauseMenu(void* /*tData*/) {
	removeLines();
	removeSmoothText(&gPauseMenu3DSData.mFPSText);
	releaseSmoothTextFonts();
}

static void updatePauseMenu(void* /*tData*/) {
	if (gPauseMenu3DSData.mAreBarsHidden) setDreamBarInvisibleForOneFrame();
	updateFPS();

	if (!gPauseMenu3DSData.mIsOpen) {
		// Only while fighting: START skips the intro and the win pose too.
		if (getDreamRoundStateNumber() == 2 /* ROUND_STATE_FIGHT */ && !isWrapperPaused() && hasPressedStartFlankSingle(0)) {
			openPauseMenu();
		}
		return;
	}

	const int amount = int(gPauseMenu3DSData.mItems.size());
	const auto item = gPauseMenu3DSData.mItems[gPauseMenu3DSData.mSelected];
	if (hasPressedUpFlankSingle(0)) {
		gPauseMenu3DSData.mSelected = (gPauseMenu3DSData.mSelected + amount - 1) % amount;
		updateItemColors();
	}
	else if (hasPressedDownFlankSingle(0)) {
		gPauseMenu3DSData.mSelected = (gPauseMenu3DSData.mSelected + 1) % amount;
		updateItemColors();
	}
	else if (item >= PAUSE_MENU_CPU_LEVEL && item != PAUSE_MENU_BACK && (hasPressedLeftFlankSingle(0) || hasPressedRightFlankSingle(0))) {
		changeSetting(item, hasPressedLeftFlankSingle(0) ? -1 : 1);
	}
	else if (hasPressedAFlankSingle(0)) {
		selectItem();
	}
	else if (hasPressedBFlankSingle(0)) {
		if (gPauseMenu3DSData.mIsInSettings) showItems(0, gPauseMenu3DSData.mSelectedMain);
		else closePauseMenu();
	}
	else if (hasPressedStartFlankSingle(0)) {
		closePauseMenu();
	}
}

ActorBlueprint getPauseMenu3DS() {
	return makeActorBlueprint(loadPauseMenu, unloadPauseMenu, updatePauseMenu);
}
