#include "ai.h"

#include <assert.h>

#include <prism/datastructures.h>
#include <prism/math.h>
#include <prism/stlutil.h>

#include "mugencommandhandler.h"
#include "gamelogic.h"
#include "config.h"
#include "mugenstagehandler.h"
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
#include "smartai3ds.h"
#endif

using namespace std;

// The computer's buttons, as MUGEN 1.1's (and IKEMEN GO's, input.go's
// AiInput) for a character without an AI of its own: it picks no moves,
// it presses buttons at random, the moves coming from the character's own
// commands. A random direction held up to a second, taken every 15 frames
// or so; each button pressed up to half a second, more often the higher the
// level. The engine's AI before (random commands set active, walking in,
// jumping when the other jumped near) left Chun Li HD standing still for
// seconds against herself, both jumping at once.
//
// The direction is not one of eight alike as in IKEMEN GO: MUGEN 1.1's
// computer (Kung Fu Man against Kung Fu Man at level 4, its "hold"
// commands counted by the other) holds forward 28-38% of the time, back
// 14-20%, up 13-15% and down 9-12%; here, with eight alike, 25% each: it
// crouched four times as much (12-20% of the time against 3-4%) and walked
// in less. The weights below give MUGEN's shares.
enum { AI_BUTTON_A, AI_BUTTON_B, AI_BUTTON_C, AI_BUTTON_X, AI_BUTTON_Y, AI_BUTTON_Z, AI_BUTTON_START, AI_BUTTON_AMOUNT };
enum { AI_DIRECTION_NONE, AI_DIRECTION_FORWARD, AI_DIRECTION_UP_FORWARD, AI_DIRECTION_UP, AI_DIRECTION_UP_BACK, AI_DIRECTION_BACK, AI_DIRECTION_DOWN_BACK, AI_DIRECTION_DOWN, AI_DIRECTION_DOWN_FORWARD, AI_DIRECTION_AMOUNT };

#define AI_GUARD_CHANCE 0.08f

// In hundredths, in the order above.
static const int gAIDirectionWeights[AI_DIRECTION_AMOUNT] = { 12, 40, 6, 13, 4, 10, 3, 8, 4 };

typedef struct {
	DreamPlayer* mPlayer;

	float mDifficultyFactor;
	int mRandomInputNow;
	int mRandomInputDuration;

	int mDirection;       // AI_DIRECTION_*, forward and back as the character faces
	int mDirectionTime;   // frames left holding it (0: none)
	int mIsGuarding;      // holding back while the other's attack is near
	int mButtonTimes[AI_BUTTON_AMOUNT];

	vector<string> mCommandNames; 
	vector<string> mCheatCommandNames;   // those of more than one step or key
} PlayerAI;

static struct {
	list<PlayerAI> mHandledPlayers; 

} gAI;

static void loadAIHandler(void* tData) {
	(void)tData;
	setProfilingSectionMarkerCurrentFunction();
	stl_new_list(gAI.mHandledPlayers);
}

static int unloadSingleHandledPlayer(void* tCaller, PlayerAI& tData) {
	(void)tCaller;
	PlayerAI* e = &tData;
	stl_delete_vector(e->mCommandNames);
	stl_delete_vector(e->mCheatCommandNames);
	return 1;
}

static void unloadAIHandler(void* tData) {
	(void)tData;
	setProfilingSectionMarkerCurrentFunction();
	stl_list_remove_predicate(gAI.mHandledPlayers, unloadSingleHandledPlayer);
	stl_delete_list(gAI.mHandledPlayers);
}

static void setRandomPlayerCommandActive(PlayerAI* e) {
	int i = randfromInteger(0, int(e->mCommandNames.size()) - 1);

	const string& name = e->mCommandNames[i];

	setDreamPlayerCommandActiveForAI(e->mPlayer->mCommandID, name.data(), 2);
}

// A button (or the direction) held for its time; once let go, pressed
// again with a chance of one in tChance a frame, for 1 to tTime frames.
static int jamAIInput(int* tTime, int tChance, int tTime2) {
	(*tTime)--;
	if (*tTime <= 0) {
		if (randfromInteger(1, tChance) == 1) {
			*tTime = randfromInteger(1, tTime2);
			return 1;
		}
		*tTime = 0;
	}
	return 0;
}

static int getRandomAIDirection() {
	int value = randfromInteger(0, 99);
	for (int i = 0; i < AI_DIRECTION_AMOUNT; i++) {
		if (value < gAIDirectionWeights[i]) return i;
		value -= gAIDirectionWeights[i];
	}
	return AI_DIRECTION_NONE;
}

// MUGEN 1.1's computer guards on purpose: with the other's attack near and
// control (or already guarding) it holds back until the attack is no longer
// near. Kung Fu Man against Kung Fu Man: 13 to 32 guards a round (0.3-0.5%
// of the frames) held 21-30 frames each, 7-15% of the time guarding, at
// level 1 as at 4 and 8; here, holding back only by chance, 6-10 guards of
// 12 frames, 1.3-2.4% of the time. A chance a frame, near the attack, to
// start.
static void updateAIGuarding(PlayerAI* e) {
	DreamPlayer* p = e->mPlayer;
	const int state = getPlayerState(p);
	const int isGuardState = state >= 120 && state <= 155;
	if (!isPlayerInGuardDistance(p) || (!getPlayerControl(p) && !isGuardState)) {
		e->mIsGuarding = 0;
		return;
	}
	if (e->mIsGuarding) return;
	if (randfrom(0, 1) < AI_GUARD_CHANCE) e->mIsGuarding = 1;
}

static void updateAIButtonJamming(PlayerAI* e) {
	const int i = e->mPlayer->mRootID;
	if (jamAIInput(&e->mDirectionTime, 15, 60)) e->mDirection = getRandomAIDirection();
	updateAIGuarding(e);
	int direction = e->mDirectionTime ? e->mDirection : AI_DIRECTION_NONE;
	if (e->mIsGuarding) {
		const int isDown = direction == AI_DIRECTION_DOWN || direction == AI_DIRECTION_DOWN_BACK || direction == AI_DIRECTION_DOWN_FORWARD;
		direction = isDown ? AI_DIRECTION_DOWN_BACK : AI_DIRECTION_BACK;
	}
	if (direction != AI_DIRECTION_NONE) {
		const int isUp = direction == AI_DIRECTION_UP || direction == AI_DIRECTION_UP_FORWARD || direction == AI_DIRECTION_UP_BACK;
		const int isDown = direction == AI_DIRECTION_DOWN || direction == AI_DIRECTION_DOWN_FORWARD || direction == AI_DIRECTION_DOWN_BACK;
		const int isForward = direction == AI_DIRECTION_FORWARD || direction == AI_DIRECTION_UP_FORWARD || direction == AI_DIRECTION_DOWN_FORWARD;
		const int isBack = direction == AI_DIRECTION_BACK || direction == AI_DIRECTION_UP_BACK || direction == AI_DIRECTION_DOWN_BACK;
		const int isFacingRight = getPlayerIsFacingRight(e->mPlayer);
		setDreamDirectionsActiveForPlayer(i, isUp, isDown, isFacingRight ? isBack : isForward, isFacingRight ? isForward : isBack);
	}

	// Each button pressed with a chance of 1 in (40 - level) * 7.5 a frame
	// (1 in 270 at level 4): MUGEN 1.1's Kung Fu Man against Kung Fu Man
	// starts 4.6-5.5 normal attacks (states 200 to 640, from buttons) every
	// 10 seconds, at level 1 as at 4 and 8, 25-30% of the time attacking.
	// IKEMEN GO's (165 - 11.25 level) * 7 (1 in 805 at level 4) gave 1.6-2.6,
	// and half as many at level 1 as at 8.
	const int level = std::max(1, std::min(8, getPlayerAILevel(e->mPlayer)));
	const int chance = int((40 - level) * 7.5f);
	for (int b = AI_BUTTON_A; b <= AI_BUTTON_Z; b++) jamAIInput(&e->mButtonTimes[b], chance, 30);
	jamAIInput(&e->mButtonTimes[AI_BUTTON_START], 3600, 30);   // start less often

	if (e->mButtonTimes[AI_BUTTON_A]) setDreamButtonAActiveForPlayer(i);
	if (e->mButtonTimes[AI_BUTTON_B]) setDreamButtonBActiveForPlayer(i);
	if (e->mButtonTimes[AI_BUTTON_C]) setDreamButtonCActiveForPlayer(i);
	if (e->mButtonTimes[AI_BUTTON_X]) setDreamButtonXActiveForPlayer(i);
	if (e->mButtonTimes[AI_BUTTON_Y]) setDreamButtonYActiveForPlayer(i);
	if (e->mButtonTimes[AI_BUTTON_Z]) setDreamButtonZActiveForPlayer(i);
	if (e->mButtonTimes[AI_BUTTON_START]) setDreamButtonStartActiveForPlayer(i);
}

// MUGEN 1.1's AI.Cheat (mugen.cfg, on by default): the computer's
// commands of more than one step or key also come true with no buttons.
// Each about once every 90 seconds: MUGEN 1.1's Kung Fu Man against Kung Fu
// Man does 0.4-1.2 specials (states 1000 to 1220, his 19 commands of more
// than one step or key) every 10 seconds, at level 4 as at 8. IKEMEN GO's
// guess (char.go: once every 10 seconds at level 8, every 30 at level 1)
// gave 3.3-5.7, 2002 Rugal (41 such commands) attacking twice as much as
// in MUGEN. The engine's before: a random command every 1 to 7 frames at
// level 8, simple ones too.
//
// Set active for 2 frames: the AI's actor runs before the command handler,
// which takes a frame off before the characters read it. With 1 (pruebas
// 104 to 109) a cheated command was over before any -1 state saw it: no
// character's specials came from it, 2002 Rugal (whose moves come from his
// commands) stood still.
#define AI_CHEAT_SECONDS_BETWEEN 90.0f

static void updateAICheating(PlayerAI* e) {
	if (getGameMode() == GAME_MODE_OSU || !getArcadeAICheat() || e->mCheatCommandNames.empty()) return;
	const float chance = e->mCheatCommandNames.size() / (60.0f * AI_CHEAT_SECONDS_BETWEEN);
	if (randfrom(0, 1) >= chance) return;
	const string& name = e->mCheatCommandNames[randfromInteger(0, int(e->mCheatCommandNames.size()) - 1)];
	setDreamPlayerCommandActiveForAI(e->mPlayer->mCommandID, name.data(), 2);
}

static void updateSingleAI(void* /*tCaller*/, PlayerAI& tData) {
	if (getDreamRoundStateNumber() != 2) return;
	PlayerAI* e = &tData;
	if (!getPlayerAILevel(e->mPlayer)) return;
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	// [3DS] The improved AI for a character with none of its own.
	if (updateN3DSSmartAI(e->mPlayer)) return;
#endif

	updateAIButtonJamming(e);
	updateAICheating(e);
}

static void updateAIHandler(void* tData) {
	(void)tData;
	setProfilingSectionMarkerCurrentFunction();
	stl_list_map(gAI.mHandledPlayers, updateSingleAI);
}

static void insertSingleCommandName(vector<string>* tCaller, const string& tKey, DreamMugenCommand& tData) {
	(void)tData;
	tCaller->push_back(tKey);
}

static int isCheatableCommand(DreamMugenCommand& tCommand) {
	for (int i = 0; i < vector_size(&tCommand.mInputs); i++) {
		const auto input = (DreamMugenCommandInput*)vector_get(&tCommand.mInputs, i);
		if (vector_size(&input->mInputSteps) > 1) return 1;
		if (vector_size(&input->mInputSteps) == 1 && ((DreamMugenCommandInputStep*)vector_get(&input->mInputSteps, 0))->mType == MUGEN_COMMAND_INPUT_STEP_TYPE_MULTIPLE) return 1;
	}
	return 0;
}

void setDreamAIActive(DreamPlayer * p)
{
	PlayerAI e;
	e.mPlayer = p;
	e.mRandomInputNow = 0;
	e.mRandomInputDuration = 20;
	e.mDirection = AI_DIRECTION_NONE;
	e.mDirectionTime = 0;
	e.mIsGuarding = 0;
	for (int b = 0; b < AI_BUTTON_AMOUNT; b++) e.mButtonTimes[b] = 0;
	e.mCommandNames.clear();
	e.mDifficultyFactor = (getPlayerAILevel(p) - 1) / 7.0f;

	DreamMugenCommands* commands = &p->mHeader->mFiles.mCommands;
	stl_string_map_map(commands->mCommands, insertSingleCommandName, &e.mCommandNames);
	e.mCheatCommandNames.clear();
	for (const auto& name : e.mCommandNames) {
		if (isCheatableCommand(commands->mCommands[name])) e.mCheatCommandNames.push_back(name);
	}

	gAI.mHandledPlayers.push_back(e);
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	prepareN3DSSmartAI(p);
#endif
}

typedef struct {
	int i;
	PlayerAI* mFound;
} FindAICaller;

static void findSameID(FindAICaller* tCaller, PlayerAI& tData) {
	PlayerAI* e = &tData;

	if (e->mPlayer->mRootID == tCaller->i) {
		tCaller->mFound = e;
	}
}

static PlayerAI* getAIFromPlayerID(int i) {
	FindAICaller caller;
	caller.i = i;
	caller.mFound = nullptr;

	stl_list_map(gAI.mHandledPlayers, findSameID, &caller);
	if (!caller.mFound) return NULL;
	else return caller.mFound;
}

void activateRandomAICommand(int i) {
	PlayerAI* e = getAIFromPlayerID(i);
	if (!e) return;
	setRandomPlayerCommandActive(e);
}

ActorBlueprint getDreamAIHandler() {
	return makeActorBlueprint(loadAIHandler, unloadAIHandler, updateAIHandler);
}
