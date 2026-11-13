#include <prism/framerateselectscreen.h>
#include <prism/physics.h>
#include <prism/file.h>
#include <prism/drawing.h>
#include <prism/log.h>
#include <prism/wrapper.h>
#include <prism/system.h>
#include <prism/stagehandler.h>
#include <prism/logoscreen.h>
#include <prism/mugentexthandler.h>
#include <prism/clipboardhandler.h>
#include <prism/sound.h>
#include <prism/screeneffect.h>
#include <prism/profiling.h>
#include <prism/debug.h>
#include <prism/soundeffect.h>

#include "titlescreen.h"
#include "fightscreen.h"
#include "playerdefinition.h"
#include "dolmexicastoryscreen.h"
#include "stage.h"
#include "config.h"
#include "dolmexicadebug.h"
#include "initscreen.h"

char romdisk_buffer[1];
int romdisk_buffer_length;

// #define DEVELOP

#ifdef DREAMCAST
KOS_INIT_FLAGS(INIT_DEFAULT | INIT_MALLOCSTATS);

#endif

#ifdef VITA
// VitaSDK defaults the newlib heap to 32MB, not enough for large sprites like in stages (or Dream Fight story credits etc). 96MB seems to work, even with fragmentation.
extern "C" {
	unsigned int _newlib_heap_size_user = 96 * 1024 * 1024;
}
#endif

#ifdef __3DS__
#include "pausemenu3ds.h"
#include "gamelogic.h"
#include "characterselectscreen.h"
#endif

#ifdef __3DS__
// [3DS] libctru gives the textures' (linear) memory at most 32 MB by default
// and the rest to the heap. The heap gets 48 MB (the game's some 5, and the
// sprites' palette indices, compressed: 11 MB for a CvS character), the
// textures the rest (some 72 MB, made again when drawn when they run short).
// The stack: 512 KB, from the heap (libctru's 32 KB was about what reading a
// CvS character's formulas takes: a few more parentheses overflowed it, with
// Mai).
extern "C" {
	u32 __ctru_heap_size = 48 * 1024 * 1024;
	u32 __stacksize__ = 512 * 1024;
}
#endif

#ifdef __3DS__
// [3DS] A fight watched at once, again and again, with no menus (to run the
// game in an emulator): autostart.txt in the game's folder, four lines: the
// first fighter's .def and the second's (from the asset folder's chars/),
// the stage's .def (from the asset folder), the computer's level (1 to 8);
// or "select" alone, for watch mode's select screen. Without the file, the
// game as always.
#define N3DS_AUTOSTART_FILE "sdmc:/3ds/dolmexica/autostart.txt"

static struct {
	std::string mCharacters[2];
	std::string mStage;
	int mLevel = 8;
} gN3DSAutostart;

static void startN3DSAutostartFight();

static void n3dsAutostartFightFinishedCB() {
	startN3DSAutostartFight();
}

static void startN3DSAutostartFight() {
	for (int i = 0; i < 2; i++) {
		char path[1024];
		getCharacterSelectNamePath(gN3DSAutostart.mCharacters[i].c_str(), path);
		setPlayerDefinitionPath(i, path);
	}
	char noMusic[2] = "";
	setDreamStageMugenDefinition((getDolmexicaAssetFolder() + gN3DSAutostart.mStage).c_str(), noMusic);
	setDifficulty(gN3DSAutostart.mLevel);
	setGameModeWatch();
	startFightScreen(n3dsAutostartFightFinishedCB, n3dsAutostartFightFinishedCB);
}

static Screen gN3DSAutostartScreen;

static void loadN3DSAutostartSelectScreen();

// The fight chosen, then the select screen again.
static void n3dsAutostartSelectFinishedCB() {
	setGameModeWatch();
	startFightScreen(loadN3DSAutostartSelectScreen, loadN3DSAutostartSelectScreen);
}

static void loadN3DSAutostartSelectScreen() {
	setCharacterSelectScreenModeName("Watch Mode");
	setCharacterSelectOnePlayerSelectAll();
	setCharacterSelectStageActive();
	setCharacterSelectFinishedCB(n3dsAutostartSelectFinishedCB);
	setNewScreen(getCharacterSelectScreen());
}

static void loadN3DSAutostartScreen() {
	startN3DSAutostartFight();
}

static Screen* getN3DSAutostartScreenOrNull() {
	FILE* file = fopen(N3DS_AUTOSTART_FILE, "r");
	if (!file) return NULL;
	std::string lines[4];
	char line[512];
	for (int i = 0; i < 4 && fgets(line, sizeof(line), file); i++) {
		lines[i] = line;
		while (!lines[i].empty() && (lines[i].back() == '\n' || lines[i].back() == '\r' || lines[i].back() == ' ')) lines[i].pop_back();
	}
	fclose(file);
	// "select" alone: watch mode's select screen at once (to see the menus).
	if (lines[0] == "select") {
		gN3DSAutostartScreen = makeScreen(loadN3DSAutostartSelectScreen);
		return &gN3DSAutostartScreen;
	}
	if (lines[0].empty() || lines[1].empty() || lines[2].empty()) return NULL;
	gN3DSAutostart.mCharacters[0] = lines[0];
	gN3DSAutostart.mCharacters[1] = lines[1];
	gN3DSAutostart.mStage = lines[2];
	if (!lines[3].empty()) gN3DSAutostart.mLevel = atoi(lines[3].c_str());
	gN3DSAutostartScreen = makeScreen(loadN3DSAutostartScreen);
	return &gN3DSAutostartScreen;
}
#endif

void exitGame() {
	shutdownPrismWrapper();

#ifdef DEVELOP
	if (isOnDreamcast()) {
		abortSystem();
	}
	else {
		returnToMenu();
	}
#else
	returnToMenu();
#endif
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

#ifdef DEVELOP
	setDevelopMode();
	setMinimumLogType((isOnDreamcast() || isOnVita()) ? LOG_TYPE_NORMAL : LOG_TYPE_NORMAL);
#elif defined(__3DS__)
	// [3DS] The log's errors (bottom screen and the SD card): every line
	// costs time on both. The warnings go to compat-log.txt, each once.
	setMinimumLogType(LOG_TYPE_ERROR);
#else
	setMinimumLogType(LOG_TYPE_NONE);
#endif

	setGameName("DOLMEXICA INFINITE");
	setScreenSize(320, 240);

	if (!isOnDreamcast()) {
		setMugenSpriteFileReaderSubTextureSplit(8, 1024);
	}

	initPrismWrapperWithMugenFlags();
#ifdef __3DS__
	loadN3DSScreenSetting();
#endif
	loadMugenConfig();
	loadGlobalVariables(PrismSaveSlot::AMOUNT);
	setFont("$/rd/fonts/segoe.hdr", "$/rd/fonts/segoe.pkg");
	loadMugenSystemFonts();
	logg("Check framerate");
	FramerateSelectReturnType framerateReturnType = selectFramerate();
	if (framerateReturnType == FRAMERATE_SCREEN_RETURN_ABORT) {
		exitGame();
	}

	setMemoryHandlerCompressionActive();
	setSoundEffectCompression(1);
	initClipboardForGame();
	setScreenEffectZ(99);
	setMugenAnimationHandlerPixelCenter(Vector2D(0.0, 0.0));
	setScreenAfterWrapperLogoScreen(getInitScreen());
	
#ifdef DEVELOP	
	setUnscaledGameWavVolume(0);
	setUnscaledGameMidiVolume(0);

	if (isOnWindows())
	{
		//setDisplayedScreenSize(1356, 1017);
		//setDisplayedScreenSize(320, 240);
		//setScreenPosition(0, -1080); // TODO: move to user cfg
	}

	disableWrapperErrorRecovery();
	initDolmexicaDebug();
	setDebugMinusCheckEnabled(1);
#endif

#ifdef __3DS__
	if (Screen* autostart = getN3DSAutostartScreenOrNull()) {
		startScreenHandling(autostart);
		exitGame();
		return 0;
	}
#endif
	startScreenHandling(getInitScreen());
	
	exitGame();
	
	return 0;
}
