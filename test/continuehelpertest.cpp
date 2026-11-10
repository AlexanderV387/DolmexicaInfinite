#include <gtest/gtest.h>

#include <string>
#include <cstdio>
#include <cstdlib>
#include <vector>

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

/* Helpers that go missing: the second player's helpers of the given IDs
 * (DOLMEXICA_WATCH_IDS=111,301,302,303) looked at each frame of the fight,
 * each time one goes or comes back written with its root's state.
 * DOLMEXICA_CHAR, DOLMEXICA_CHAR1, DOLMEXICA_STAGE as above; both fight on
 * their own for DOLMEXICA_FRAMES frames (default 7200), or DOLMEXICA_ARCADE=1. */
TEST_F(ContinueHelperTest, MissingHelpers) {
	const int frames = getContinueIntegerSetting("DOLMEXICA_FRAMES", 7200);
	const char* stageEnv = getenv("DOLMEXICA_STAGE");
	std::vector<int> ids;
	for (const char* q = getenv("DOLMEXICA_WATCH_IDS"); q && *q; ) {
		ids.push_back((int)strtol(q, (char**)&q, 10));
		while (*q == ',' || *q == ' ') q++;
	}

	initForAutomatedFightScreenTest();
	setPlayerDefinitionPath(0, getContinueCharDefPath("DOLMEXICA_CHAR1").c_str());
	setPlayerDefinitionPath(1, getContinueCharDefPath("DOLMEXICA_CHAR").c_str());
	setDreamStageMugenDefinition((getDolmexicaAssetFolder() + (stageEnv ? stageEnv : "stages/kfm.def")).c_str(), "");
	// DOLMEXICA_ARCADE=1: the arcade, no one at the first player's controls
	// (rounds won and lost, the next ones started).
	if (getContinueIntegerSetting("DOLMEXICA_ARCADE", 0)) setGameModeArcade();
	else setGameModeSuperWatch();
	const auto screen = getDreamFightScreenForTesting();
	initPrismWrapperScreenForDebug(screen);

	std::vector<int> present(ids.size(), -1);
	int round = getDreamRoundNumber();
	for (int frame = 0; frame < frames; frame++) {
		updatePrismWrapperScreenForDebugWithIterationsAndRender(1);
		if (getDreamRoundNumber() != round) {
			round = getDreamRoundNumber();
			printf("frame %d: round %d\n", frame, round);
		}
		if (getDreamRoundStateNumber() != 2) continue;
		DreamPlayer* root = getRootPlayer(1);
		for (size_t i = 0; i < ids.size(); i++) {
			const int isThere = getPlayerHelperAmountWithID(root, ids[i]) > 0;
			if (isThere == present[i]) continue;
			present[i] = isThere;
			printf("frame %d: helper %d %s (root state %d, time %d, previous state %d)\n", frame, ids[i], isThere ? "there" : "MISSING", getPlayerState(root), getPlayerTimeInState(root), getPlayerPreviousState(root));
		}
	}
	fflush(stdout);
	unloadPrismWrapperScreenForDebug();
}

static void printFacingTreeCB(void* tCaller, void* tData) {
	const int depth = *(int*)tCaller;
	DreamPlayer* helper = (DreamPlayer*)tData;
	if (helper->mIsDestroyed) return;
	printf("%*shelper %d: state %d, anim %d, facing %s, x %.0f\n", 2 * depth, "", helper->mID, getPlayerState(helper), getPlayerAnimationNumber(helper), getPlayerIsFacingRight(helper) ? "right" : "left", getPlayerPositionX(helper, getPlayerCoordinateP(helper)));
	int childDepth = depth + 1;
	list_map(&helper->mHelpers, printFacingTreeCB, &childDepth);
}

/* Jumped over: in the arcade (no one at the first player's controls), the
 * first player put behind the second at frame DOLMEXICA_BEHIND_AT (default
 * 420, -1 never: then DOLMEXICA_VERSUS=1 and PRISM_LINUX_SCRIPT to move it), and every DOLMEXICA_EVERY frames (default 30) the second player's
 * facing and its helpers' written, for DOLMEXICA_FRAMES frames (default 1200). */
TEST_F(ContinueHelperTest, JumpedOver) {
	const int frames = getContinueIntegerSetting("DOLMEXICA_FRAMES", 1200);
	const int behindAt = getContinueIntegerSetting("DOLMEXICA_BEHIND_AT", 420);
	const int every = getContinueIntegerSetting("DOLMEXICA_EVERY", 30);
	const char* stageEnv = getenv("DOLMEXICA_STAGE");

	initForAutomatedFightScreenTest();
	setPlayerDefinitionPath(0, getContinueCharDefPath("DOLMEXICA_CHAR1").c_str());
	setPlayerDefinitionPath(1, getContinueCharDefPath("DOLMEXICA_CHAR").c_str());
	setDreamStageMugenDefinition((getDolmexicaAssetFolder() + (stageEnv ? stageEnv : "stages/kfm.def")).c_str(), "");
	// DOLMEXICA_VERSUS=1: both at the controls, the second idle (the first
	// one's buttons from PRISM_LINUX_SCRIPT, see prism's linux/input_linux.cpp).
	if (getContinueIntegerSetting("DOLMEXICA_VERSUS", 0)) setGameModeVersus();
	else setGameModeArcade();
	const auto screen = getDreamFightScreenForTesting();
	initPrismWrapperScreenForDebug(screen);

	for (int frame = 0; frame < frames; frame++) {
		if (frame == behindAt) {
			DreamPlayer* first = getRootPlayer(0);
			DreamPlayer* second = getRootPlayer(1);
			const int coordinateP = getPlayerCoordinateP(first);
			const float x2 = getPlayerPositionX(second, getPlayerCoordinateP(second)) * coordinateP / getPlayerCoordinateP(second);
			const float behind = getPlayerIsFacingRight(second) ? -60.0f : 60.0f;
			setPlayerPositionX(first, x2 + behind, coordinateP);
			printf("frame %d: first player put behind the second\n", frame);
		}
		updatePrismWrapperScreenForDebugWithIterationsAndRender(1);
		if (frame % every) continue;
		DreamPlayer* root = getRootPlayer(1);
		DreamPlayer* first = getRootPlayer(0);
		printf("frame %d: root state %d, anim %d, facing %s, x %.0f; first player state %d, facing %s, x %.0f, y %.0f\n", frame, getPlayerState(root), getPlayerAnimationNumber(root), getPlayerIsFacingRight(root) ? "right" : "left", getPlayerPositionX(root, getPlayerCoordinateP(root)), getPlayerState(first), getPlayerIsFacingRight(first) ? "right" : "left", getPlayerPositionX(first, getPlayerCoordinateP(first)), getPlayerPositionY(first, getPlayerCoordinateP(first)));
		int depth = 1;
		list_map(&root->mHelpers, printFacingTreeCB, &depth);
	}
	fflush(stdout);
	unloadPrismWrapperScreenForDebug();
}

static void collectUniqueIDsCB(void* tCaller, void* tData) {
	auto ids = (std::vector<int>*)tCaller;
	DreamPlayer* helper = (DreamPlayer*)tData;
	if (!helper->mIsDestroyed) ids->push_back(getPlayerUniqueID(helper));
	list_map(&helper->mHelpers, collectUniqueIDsCB, tCaller);
}

/* MUGEN's player IDs: 56 and 57 for the two players, a new one for each
 * helper, and PlayerID finding any player, the other one's too. */
TEST_F(ContinueHelperTest, UniqueIDs) {
	initForAutomatedFightScreenTest();
	setPlayerDefinitionPath(0, getContinueCharDefPath("DOLMEXICA_CHAR1").c_str());
	setPlayerDefinitionPath(1, getContinueCharDefPath("DOLMEXICA_CHAR").c_str());
	setDreamStageMugenDefinition((getDolmexicaAssetFolder() + "stages/kfm.def").c_str(), "");
	setGameModeSuperWatch();
	const auto screen = getDreamFightScreenForTesting();
	initPrismWrapperScreenForDebug(screen);
	updatePrismWrapperScreenForDebugWithIterations(getContinueIntegerSetting("DOLMEXICA_FRAMES", 600));

	DreamPlayer* first = getRootPlayer(0);
	DreamPlayer* second = getRootPlayer(1);
	EXPECT_EQ(getPlayerUniqueID(first), 56);
	EXPECT_EQ(getPlayerUniqueID(second), 57);
	EXPECT_TRUE(getPlayerByIDOrNullIfNonexistant(first, 57) == second);
	EXPECT_TRUE(getPlayerByIDOrNullIfNonexistant(second, 56) == first);
	EXPECT_TRUE(getPlayerByIDOrNullIfNonexistant(first, 0) == NULL);

	std::vector<int> ids;
	list_map(&second->mHelpers, collectUniqueIDsCB, &ids);
	printf("second player's helpers: %d, their IDs:", (int)ids.size());
	for (const auto id : ids) {
		printf(" %d", id);
		EXPECT_TRUE(id > 57);
		EXPECT_TRUE(getPlayerByIDOrNullIfNonexistant(first, id) != NULL);
	}
	printf("\n");
	fflush(stdout);
	unloadPrismWrapperScreenForDebug();
}
