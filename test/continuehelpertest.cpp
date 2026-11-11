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
#include "mugenstagehandler.h"
#include "mugenassignment.h"
#include "mugenassignmentevaluator.h"
#include "stage.h"
#include "gamelogic.h"
#include "fightscreen.h"
#include "titlescreen.h"

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
		if (getenv("DOLMEXICA_PRINT_CAMERA") && frame % 50 == 0) printf("frame %d: camera %.1f,%.1f, round state %d, players y %.1f %.1f\n", frame, getDreamMugenStageHandlerCameraPositionReference()->x, getDreamMugenStageHandlerCameraPositionReference()->y, getDreamRoundStateNumber(), getPlayerPositionY(getRootPlayer(0), getDreamMugenStageHandlerCameraCoordinateP()), getPlayerPositionY(getRootPlayer(1), getDreamMugenStageHandlerCameraCoordinateP()));
		if (getDreamRoundStateNumber() != 2) continue;
		// DOLMEXICA_P1_LIFE: the first player's life set to it as each round's fight starts.
		static int lifeSetRound = -1;
		if (getenv("DOLMEXICA_P1_LIFE") && lifeSetRound != round) {
			lifeSetRound = round;
			setPlayerLife(getRootPlayer(0), getRootPlayer(0), atoi(getenv("DOLMEXICA_P1_LIFE")));
			printf("frame %d: first player's life set to %s\n", frame, getenv("DOLMEXICA_P1_LIFE"));
		}
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
	printf("%*shelper %d: state %d, time %d, hitdef %d, anim %d, animtime %d, facing %s, x %.0f, y %.0f\n", 2 * depth, "", helper->mID, getPlayerState(helper), getPlayerTimeInState(helper), isHitDataActive(helper), getPlayerAnimationNumber(helper), getPlayerAnimationTimeDeltaUntilFinished(helper), getPlayerIsFacingRight(helper) ? "right" : "left", getPlayerPositionX(helper, getPlayerCoordinateP(helper)), getPlayerPositionY(helper, getPlayerCoordinateP(helper)));
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
	// DOLMEXICA_WATCH=level: both at the computer's hands, at that AI level
	// (as MUGEN's -p1.ai and -p2.ai), one round.
	if (const int watchLevel = getContinueIntegerSetting("DOLMEXICA_WATCH", 0)) {
		setDifficulty(watchLevel);
		setGameModeWatch();
	}
	const auto screen = getDreamFightScreenForTesting();
	initPrismWrapperScreenForDebug(screen);

	for (int frame = 0; frame < frames; frame++) {
		// DOLMEXICA_CENTER_AT: the second player put in the middle and the
		// first one 100 to its left at that frame (the training's dummy, not
		// in a corner).
		if (frame == getContinueIntegerSetting("DOLMEXICA_CENTER_AT", -1)) {
			DreamPlayer* first = getRootPlayer(0);
			DreamPlayer* second = getRootPlayer(1);
			setPlayerPositionX(second, 0, getPlayerCoordinateP(second));
			setPlayerPositionX(first, -(float)getContinueIntegerSetting("DOLMEXICA_CENTER_GAP", 100) * getPlayerCoordinateP(first) / 320, getPlayerCoordinateP(first));
		}
		// DOLMEXICA_P2_CHANGE=frame:state: the second player changes into that
		// state at that frame (as a ChangeState of its -1 would).
		if (const char* change = getenv("DOLMEXICA_P2_CHANGE")) {
			if (frame == atoi(change) && strchr(change, ':')) {
				DreamPlayer* second = getRootPlayer(1);
				printf("frame %d: second player from state %d (ctrl %d, facing %s) into %d\n", frame, getPlayerState(second), getPlayerControl(second), getPlayerIsFacingRight(second) ? "right" : "left", atoi(strchr(change, ':') + 1));
				changePlayerState(second, atoi(strchr(change, ':') + 1));
			}
		}
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
		// DOLMEXICA_EVAL="id|expression|expression...": each expression
		// evaluated on that helper of the first player (id 0: the player)
		// every frame from DOLMEXICA_EVAL_FROM on.
		if (const char* eval = getenv("DOLMEXICA_EVAL")) {
			if (frame >= getContinueIntegerSetting("DOLMEXICA_EVAL_FROM", 0)) {
				std::string all(eval);
				const int id = atoi(all.c_str());
				DreamPlayer* target = id ? getPlayerHelperOrNullIfNonexistant(getRootPlayer(0), id) : getRootPlayer(0);
				std::string line;
				size_t start = all.find('|');
				while (target && start != std::string::npos) {
					size_t end = all.find('|', start + 1);
					std::string expression = all.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
					DreamMugenAssignment* assignment = parseDreamMugenAssignmentFromString(expression.c_str());
					line += " [" + expression + "] = " + std::to_string(evaluateDreamAssignmentAndReturnAsInteger(&assignment, target));
					start = end;
				}
				if (target) printf("frame %d: eval%s\n", frame, line.c_str());
			}
		}
		if (frame % every) continue;
		DreamPlayer* root = getRootPlayer(getenv("DOLMEXICA_TREE_P1") ? 0 : 1);
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

#ifdef N3DS_TEXTURE_SIM
extern "C" void n3dssimMarkAllocations();
extern "C" void n3dssimDumpAllocations(size_t tMinimumTotal);
namespace prism {
	void getN3DSTextureCountsForTesting(size_t* oResident, size_t* oStreamed, int* oOwnPaletteReferences, size_t* oOwnPalettes);
	extern int gN3DSTexturesAlive;
}
static void printN3DSTextureCounts(const char* tMoment) {
	size_t resident, streamed, ownPalettes;
	int ownPaletteReferences;
	prism::getN3DSTextureCountsForTesting(&resident, &streamed, &ownPaletteReferences, &ownPalettes);
	printf("%s: %d textures, %zu with texels, %zu streamed, own palettes %zu with %d references\n", tMoment, prism::gN3DSTexturesAlive, resident, streamed, ownPalettes, ownPaletteReferences);
}
#endif

/* Fights one after another in one run, as when playing (only the select
 * screen between them left out): DOLMEXICA_FIGHTS="p1/p2,p1/p2,...",
 * DOLMEXICA_FRAMES frames each (default 1800). On the 3DS simulation with
 * N3DS_SIM_TRACK=1, the blocks each fight leaves on the heap are written:
 * what is left in its middle breaks the 3DS's heap into pieces. */
TEST_F(ContinueHelperTest, SeveralFights) {
	const int frames = getContinueIntegerSetting("DOLMEXICA_FRAMES", 1800);
	const char* fightsEnv = getenv("DOLMEXICA_FIGHTS");
	std::string fights = fightsEnv ? fightsEnv : "kfm/kfm";
	size_t start = 0;
	while (start < fights.size()) {
		size_t end = fights.find(',', start);
		if (end == std::string::npos) end = fights.size();
		const std::string fight = fights.substr(start, end - start);
		start = end + 1;
		const size_t slash = fight.find('/');
		const std::string first = fight.substr(0, slash), second = fight.substr(slash + 1);
		printf("== %s vs %s\n", first.c_str(), second.c_str());
		fflush(stdout);
#ifdef N3DS_TEXTURE_SIM
		printN3DSTextureCounts("before the fight");
		n3dssimMarkAllocations();
#endif
		initForAutomatedFightScreenTest();
		setPlayerDefinitionPath(0, (getDolmexicaAssetFolder() + "chars/" + first + "/" + first + ".def").c_str());
		setPlayerDefinitionPath(1, (getDolmexicaAssetFolder() + "chars/" + second + "/" + second + ".def").c_str());
		setDreamStageMugenDefinition((getDolmexicaAssetFolder() + "stages/kfm.def").c_str(), "");
		setGameModeSuperWatch();
		const auto screen = getDreamFightScreenForTesting();
		initPrismWrapperScreenForDebug(screen);
		updatePrismWrapperScreenForDebugWithIterationsAndRender(frames);
		unloadPrismWrapperScreenForDebug();
#ifdef N3DS_TEXTURE_SIM
		printN3DSTextureCounts("after the fight");
		n3dssimDumpAllocations(4096);
#endif
		fflush(stderr);
	}
}

/* Juggling: the first player (DOLMEXICA_CHAR1, the second one idle) put next
 * to the second at frame 300, its airjuggle set to DOLMEXICA_AIRJUGGLE
 * (default 5), its buttons from PRISM_LINUX_SCRIPT; each frame the second's
 * state changes, it is written with the points left against the first. */
TEST_F(ContinueHelperTest, Juggle) {
	const int frames = getContinueIntegerSetting("DOLMEXICA_FRAMES", 700);
	const int airjuggle = getContinueIntegerSetting("DOLMEXICA_AIRJUGGLE", 5);
	initForAutomatedFightScreenTest();
	setPlayerDefinitionPath(0, getContinueCharDefPath("DOLMEXICA_CHAR1").c_str());
	setPlayerDefinitionPath(1, getContinueCharDefPath("DOLMEXICA_CHAR").c_str());
	setDreamStageMugenDefinition((getDolmexicaAssetFolder() + "stages/kfm.def").c_str(), "");
	setGameModeVersus();
	const auto screen = getDreamFightScreenForTesting();
	initPrismWrapperScreenForDebug(screen);
	int lastState = -1, lastFirstState = -1;
	for (int frame = 0; frame < frames; frame++) {
		DreamPlayer* first = getRootPlayer(0);
		DreamPlayer* second = getRootPlayer(1);
		if (frame == 300) {
			first->mHeader->mFiles.mConstants.mHeader.mAirJugglePoints = airjuggle;
			const int coordinateP = getPlayerCoordinateP(first);
			const float x2 = getPlayerPositionX(second, getPlayerCoordinateP(second)) * coordinateP / getPlayerCoordinateP(second);
			setPlayerPositionX(first, x2 - 50.0f, coordinateP);
		}
		updatePrismWrapperScreenForDebugWithIterationsAndRender(1);
		if (frame < 300) continue;
		// DOLMEXICA_COMMANDS ("y,QCF_y"): the first player's commands active each frame.
		if (const char* commands = getenv("DOLMEXICA_COMMANDS")) {
			std::string line;
			for (const char* q = commands; *q; ) {
				const char* end = strchr(q, ',');
				const std::string name = end ? std::string(q, end - q) : std::string(q);
				if (isPlayerCommandActive(first, name.c_str())) line += " " + name;
				q = end ? end + 1 : q + strlen(q);
			}
			if (!line.empty()) printf("frame %d: state %d, type %d, time %d, ctrl %d, paused %d, commands%s\n", frame, getPlayerState(first), (int)getPlayerStateType(first), getPlayerTimeInState(first), getPlayerControl(first), isPlayerPaused(first), line.c_str());
		}
		if (getPlayerState(second) == lastState && getPlayerState(first) == lastFirstState) continue;
		lastState = getPlayerState(second);
		lastFirstState = getPlayerState(first);
		printf("frame %d: first state %d, second state %d, y %.0f, life %d, juggle left %d\n", frame, lastFirstState, lastState, getPlayerPositionY(second, getPlayerCoordinateP(second)), getPlayerLife(second), getPlayerJuggleLeftForTesting(second, first));
	}
	fflush(stdout);
	unloadPrismWrapperScreenForDebug();
}

/* Screen tour: the game from its title screen, player 1's buttons from
 * PRISM_LINUX_SCRIPT, for DOLMEXICA_FRAMES frames (default 3000), drawn
 * (the simulator's N3DS_SIM_SHOT takes pictures): for looking at every
 * screen on the 3DS's wider top screen. */
TEST_F(ContinueHelperTest, ScreenTour) {
	const int frames = getContinueIntegerSetting("DOLMEXICA_FRAMES", 3000);
	initPrismWrapperScreenForDebug(getDreamTitleScreen());
	for (int frame = 0; frame < frames; frame++) {
		updatePrismWrapperScreenForDebugWithIterationsAndRender(1);
		if (switchPrismWrapperScreenForDebugIfNeeded()) printf("frame %d: next screen\n", frame);
	}
	unloadPrismWrapperScreenForDebug();
}
