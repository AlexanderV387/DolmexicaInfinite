#include "ai.h"

#include <assert.h>

#include <prism/datastructures.h>
#include <prism/math.h>
#include <prism/stlutil.h>

#include "mugencommandhandler.h"
#include "gamelogic.h"
#include "config.h"
#include "mugenstagehandler.h"

using namespace std;

// The computer's buttons, as MUGEN 1.1's (and IKEMEN GO's, input.go's
// AiInput) for a character without an AI of its own: it picks no moves,
// it presses buttons at random, the moves coming from the character's own
// commands. A random direction (one of eight) held up to a second, taken
// every 15 frames or so; each button pressed up to half a second, more
// often the higher the level. The engine's AI before (random commands
// set active, walking in, jumping when the other jumped near) left Chun
// Li HD standing still for seconds against herself, both jumping at once.
enum { AI_BUTTON_A, AI_BUTTON_B, AI_BUTTON_C, AI_BUTTON_X, AI_BUTTON_Y, AI_BUTTON_Z, AI_BUTTON_START, AI_BUTTON_AMOUNT };

typedef struct {
	DreamPlayer* mPlayer;

	float mDifficultyFactor;
	int mRandomInputNow;
	int mRandomInputDuration;

	int mDirection;       // 0 up, then clockwise to 7 up-left
	int mDirectionTime;   // frames left holding it (0: none)
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

static void updateAIButtonJamming(PlayerAI* e) {
	const int i = e->mPlayer->mRootID;
	if (jamAIInput(&e->mDirectionTime, 15, 60)) e->mDirection = randfromInteger(0, 7);
	if (e->mDirectionTime) {
		const int d = e->mDirection;
		setDreamDirectionsActiveForPlayer(i, d == 7 || d == 0 || d == 1, d == 3 || d == 4 || d == 5, d == 5 || d == 6 || d == 7, d == 1 || d == 2 || d == 3);
	}

	const int level = getPlayerAILevel(e->mPlayer);
	const int chance = std::max(1, int((-11.25f * level + 165.0f) * 7.0f));
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
// commands of more than one step or key also come true with no buttons,
// each about once every 10 seconds at level 8 and every 30 at level 1
// (IKEMEN GO's char.go: one random command a frame, the closest it found
// to MUGEN). The engine's before: a random command every 1 to 7 frames at
// level 8, simple ones too.
static void updateAICheating(PlayerAI* e) {
	if (getGameMode() == GAME_MODE_OSU || !getArcadeAICheat() || e->mCheatCommandNames.empty()) return;
	const int level = std::max(1, std::min(8, getPlayerAILevel(e->mPlayer)));
	const float secondsBetween = 30.0f - (level - 1) * (20.0f / 7.0f);
	const float chance = e->mCheatCommandNames.size() / (60.0f * secondsBetween);
	if (randfrom(0, 1) >= chance) return;
	const string& name = e->mCheatCommandNames[randfromInteger(0, int(e->mCheatCommandNames.size()) - 1)];
	setDreamPlayerCommandActiveForAI(e->mPlayer->mCommandID, name.data(), 1);
}

static void updateSingleAI(void* /*tCaller*/, PlayerAI& tData) {
	if (getDreamRoundStateNumber() != 2) return;
	PlayerAI* e = &tData;
	if (!getPlayerAILevel(e->mPlayer)) return;

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
	e.mDirection = 0;
	e.mDirectionTime = 0;
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
