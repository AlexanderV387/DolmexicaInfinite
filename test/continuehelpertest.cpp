#include <gtest/gtest.h>

#include <string>
#include <cstdio>
#include <cstdlib>

#include <prism/wrapper.h>
#include <prism/datastructures.h>

#include "commontestfunctionality.h"

#include "config.h"
#include "playerdefinition.h"
#include "stage.h"
#include "gamelogic.h"
#include "fightscreen.h"

/* The fight started over as at "Continue?" (arcade and free play), and each
 * root's helpers written as a tree before and after it.
 * Configuration via environment:
 *   DOLMEXICA_CHAR=Galactus        second player (default kfm)
 *   DOLMEXICA_CHAR1=kfm            first player (default kfm)
 *   DOLMEXICA_STAGE=stages/kfm.def stage def path (default kfm)
 *   DOLMEXICA_FRAMES=900           frames before and after the restart
 *   DOLMEXICA_RESTARTS=1           how many restarts */

class ContinueHelperTest : public ::testing::Test {
protected:
	void SetUp() override {
		setupTestForScreenTestInAssetsFolder();
	}

	void TearDown() override {
		tearDownTestForScreenTestInAssetsFolder();
	}
};

static std::string getContinueCharDefPath(const char* tEnvironmentName) {
	const char* charEnv = getenv(tEnvironmentName);
	const std::string name = charEnv ? charEnv : "kfm";
	return getDolmexicaAssetFolder() + "chars/" + name + "/" + name + ".def";
}

static int getContinueIntegerSetting(const char* tEnvironmentName, int tDefault) {
	const char* env = getenv(tEnvironmentName);
	return env ? atoi(env) : tDefault;
}

static void printHelperTreeCB(void* tCaller, void* tData) {
	const int depth = *(int*)tCaller;
	DreamPlayer* helper = (DreamPlayer*)tData;
	// A destroyed one's animation is gone already: not read.
	if (helper->mIsDestroyed) printf("%*shelper %d: state %d, destroyed\n", 2 * depth, "", helper->mID, getPlayerState(helper));
	else printf("%*shelper %d: state %d, anim %d\n", 2 * depth, "", helper->mID, getPlayerState(helper), getPlayerAnimationNumber(helper));
	int childDepth = depth + 1;
	list_map(&helper->mHelpers, printHelperTreeCB, &childDepth);
}

static void printHelperTrees(const char* tMoment) {
	for (int i = 0; i < 2; i++) {
		DreamPlayer* root = getRootPlayer(i);
		printf("-- %s, root %d: state %d, %d helpers alive (NumHelper)\n", tMoment, i, getPlayerState(root), getPlayerHelperAmount(root));
		int depth = 1;
		list_map(&root->mHelpers, printHelperTreeCB, &depth);
	}
	fflush(stdout);
}

TEST_F(ContinueHelperTest, HelpersAfterContinue) {
	const int frames = getContinueIntegerSetting("DOLMEXICA_FRAMES", 900);
	const int restarts = getContinueIntegerSetting("DOLMEXICA_RESTARTS", 1);
	const char* stageEnv = getenv("DOLMEXICA_STAGE");

	initForAutomatedFightScreenTest();
	setPlayerDefinitionPath(0, getContinueCharDefPath("DOLMEXICA_CHAR1").c_str());
	setPlayerDefinitionPath(1, getContinueCharDefPath("DOLMEXICA_CHAR").c_str());
	setDreamStageMugenDefinition((getDolmexicaAssetFolder() + (stageEnv ? stageEnv : "stages/kfm.def")).c_str(), "");
	// With DOLMEXICA_AT_MATCH_END=N, the arcade: the first player (no one at
	// the controls) loses the match, "Continue?" is shown for N frames, then
	// the fight starts over. Else both fight on their own, and the fight
	// starts over in its middle.
	const int framesAtMatchEnd = getContinueIntegerSetting("DOLMEXICA_AT_MATCH_END", -1);
	if (framesAtMatchEnd >= 0) setGameModeArcade();
	else setGameModeSuperWatch();
	const auto screen = getDreamFightScreenForTesting();
	initPrismWrapperScreenForDebug(screen);

	int frame = 0;
	updatePrismWrapperScreenForDebugWithIterationsAndRender(frames);
	frame += frames;
	printHelperTrees("before the restart");
	for (int i = 0; i < restarts; i++) {
		if (framesAtMatchEnd >= 0) {
			// DOLMEXICA_P1_LIFE: the first player's life kept at most this,
			// so that the second wins soon (as when losing to a boss).
			const int firstPlayerLife = getContinueIntegerSetting("DOLMEXICA_P1_LIFE", -1);
			static const int MAXIMUM_MATCH_FRAMES = 60 * 60 * 10;
			for (int j = 0; j < MAXIMUM_MATCH_FRAMES && !isDreamMatchOver(); j++, frame++) {
				DreamPlayer* firstPlayer = getRootPlayer(0);
				if (firstPlayerLife > 0 && getPlayerLife(firstPlayer) > firstPlayerLife) setPlayerLife(firstPlayer, firstPlayer, firstPlayerLife);
				updatePrismWrapperScreenForDebugWithIterationsAndRender(1);
			}
			printf("match over at frame %d (winner %d)\n", frame, getDreamMatchWinnerIndex());
			updatePrismWrapperScreenForDebugWithIterationsAndRender(framesAtMatchEnd);
			frame += framesAtMatchEnd;
			printHelperTrees("at the match's end");
		}
		printf("restart at frame %d\n", frame);
		restartFightAsContinueForTesting();
		printHelperTrees("right after the restart");
		updatePrismWrapperScreenForDebugWithIterationsAndRender(1);
		printHelperTrees("1 frame after the restart");
		updatePrismWrapperScreenForDebugWithIterationsAndRender(frames);
		printHelperTrees("after the restart");
	}

	unloadPrismWrapperScreenForDebug();
}
