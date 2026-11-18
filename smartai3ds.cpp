#include "smartai3ds.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include <prism/file.h>
#include <prism/log.h>
#include <prism/math.h>
#include <prism/mugendefreader.h>

#include "mugencommandhandler.h"
#include "mugencommandreader.h"
#include "specialpanels3ds.h"

#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)

enum { BUTTON_A, BUTTON_B, BUTTON_C, BUTTON_X, BUTTON_Y, BUTTON_Z, BUTTON_AMOUNT };

#define BUTTON_HOLD_FRAMES 3
#define BUTTON_RELEASE_FRAMES 3   // between two presses of any button
#define COMMAND_ACTIVE_FRAMES 2   // as ai.cpp's cheats (one seen after the command handler's)

// Distances in a 320 wide character's units (2002 Rugal's, Kung Fu Man's).
#define DISTANCE_CLOSE 45.0f
#define DISTANCE_MIDDLE 140.0f
#define DISTANCE_ANTI_AIR 90.0f

typedef struct {
	int mIsPrepared;
	int mIsPlaying;          // the character has no AI of its own
	std::vector<N3DSSpecialCommand> mSpecials;
	std::vector<N3DSSpecialCommand> mSupers;
	std::vector<int> mAntiAirs;    // of mSpecials: uppercut-like motions
	std::vector<int> mForwards;    // of mSpecials: thrown forward (fireball-like)

	int mDecisionTime;       // frames to the next choice (with control)
	int mHorizontal;         // held: -1 back, 0, 1 forward
	int mVertical;           // held: -1 down, 0, 1 up
	int mHoldTime;           // frames left holding it
	int mButton;             // pressed now, -1 none
	int mButtonTime;         // frames left holding it, then releasing
	int mGuardChosen;        // the other's attack near: -1 not to guard it, 1 to
	int mComboTried;         // this attack's hit followed (or not) already
	int mAirAttackDone;      // this jump's attack pressed
} SmartAI;

static struct {
	int mIsEnabled = 1;
	int mOnlyRootIndex = -1;
	SmartAI mAI[2];
} gSmartAI;

int isN3DSSmartAIEnabled() { return gSmartAI.mIsEnabled; }
void setN3DSSmartAIEnabled(int tIsEnabled) { gSmartAI.mIsEnabled = tIsEnabled; }
int isN3DSSmartAIPlaying(int tRootIndex) { return gSmartAI.mAI[tRootIndex & 1].mIsPlaying; }
void setN3DSSmartAIOnlyForTesting(int tRootIndex) { gSmartAI.mOnlyRootIndex = tRootIndex; }

// --- Whether the character has an AI of its own ---

static std::string toLowerText(const char* tData, size_t tLength) {
	std::string s(tData, tLength);
	for (auto& c : s) c = char(tolower((unsigned char)c));
	return s;
}

// "AILevel" (MUGEN 1.0's) or a helper reading the buttons (keyctrl = 1:
// Winane's AI helpers, 2002 Rugal's), out of comments.
static int doesFileShowOwnAI(const std::string& tPath) {
	if (!isFile(tPath.c_str())) return 0;
	Buffer buffer = fileToBuffer(tPath.c_str());
	const std::string text = toLowerText((const char*)buffer.mData, buffer.mLength);
	freeBuffer(buffer);
	size_t lineStart = 0;
	while (lineStart < text.size()) {
		size_t lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string::npos) lineEnd = text.size();
		std::string line = text.substr(lineStart, lineEnd - lineStart);
		lineStart = lineEnd + 1;
		const size_t comment = line.find(';');
		if (comment != std::string::npos) line.resize(comment);
		if (line.find("ailevel") != std::string::npos) return 1;
		const size_t key = line.find("keyctrl");
		if (key != std::string::npos) {
			size_t q = line.find('=', key);
			if (q == std::string::npos) continue;
			q++;
			while (q < line.size() && isspace((unsigned char)line[q])) q++;
			if (q < line.size() && line[q] == '1') return 1;
		}
	}
	return 0;
}

// Commands no one can enter (three steps or more within 2 frames): those
// that tell the computer (MUGEN's AI.Cheat sets them true), as 2002 Rugal's
// "cpu", Galactus's "AI1"... "AI40", Leilei's "cpu1".
static int hasImpossibleCommand(DreamPlayer* p) {
	for (auto& entry : p->mHeader->mFiles.mCommands.mCommands) {
		for (int i = 0; i < vector_size(&entry.second.mInputs); i++) {
			auto input = (DreamMugenCommandInput*)vector_get(&entry.second.mInputs, i);
			if (vector_size(&input->mInputSteps) >= 3 && input->mTime <= 2) return 1;
		}
	}
	return 0;
}

static int hasOwnAI(DreamPlayer* p) {
	if (hasImpossibleCommand(p)) return 1;
	char defPath[1024], folder[1024], file[300];
	getPlayerDefinitionPath(defPath, p->mRootID);
	if (!isFile(defPath)) return 1;
	getPathToFile(folder, defPath);
	MugenDefScript script;
	loadMugenDefScript(&script, defPath);
	std::vector<std::string> keys = { "cmd", "cns", "st" };
	for (int i = 0; i <= 12; i++) keys.push_back("st" + std::to_string(i));
	int isOwn = 0;
	std::vector<std::string> read;
	for (const auto& key : keys) {
		getMugenDefStringOrDefault(file, &script, "files", key.c_str(), "");
		if (!*file || std::find(read.begin(), read.end(), std::string(file)) != read.end()) continue;
		read.push_back(file);
		if (doesFileShowOwnAI(std::string(folder) + file)) {
			isOwn = 1;
			break;
		}
	}
	unloadMugenDefScript(&script);
	return isOwn;
}

// --- Its specials ---

// Uppercut-like: forward, down, down-forward ("021"), back-charge up
// ("26": down then up), or the back one ("423").
static int isAntiAirMotion(const std::string& tMotion) {
	return tMotion.find("021") != std::string::npos || tMotion.find("423") != std::string::npos || tMotion == "26";
}

// Thrown forward: quarter circle forward ("210", not a half or double one
// ending elsewhere) or back-charge forward ("40").
static int isForwardMotion(const std::string& tMotion) {
	return tMotion == "210" || tMotion == "40" || tMotion == "43210";
}

void prepareN3DSSmartAI(DreamPlayer* p) {
	SmartAI* e = &gSmartAI.mAI[p->mRootID & 1];
	*e = SmartAI();
	e->mIsPrepared = 1;
	e->mButton = -1;
	e->mIsPlaying = !hasOwnAI(p);
	if (!e->mIsPlaying) {
#ifdef N3DS_TEXTURE_SIM
		printf("Improved AI: player %d has an AI of its own.\n", p->mRootID);
#endif
		logFormat("Improved AI: player %d has an AI of its own.", p->mRootID);
		return;
	}
	for (const auto& command : findN3DSSpecialCommands(p->mRootID)) {
		if (command.mIsSuper) {
			e->mSupers.push_back(command);
			continue;
		}
		if (isAntiAirMotion(command.mMotion)) e->mAntiAirs.push_back(int(e->mSpecials.size()));
		else if (isForwardMotion(command.mMotion)) e->mForwards.push_back(int(e->mSpecials.size()));
		e->mSpecials.push_back(command);
	}
#ifdef N3DS_TEXTURE_SIM
	printf("Improved AI: player %d plays with %d specials (%d anti-air, %d forward) and %d supers.\n", p->mRootID, int(e->mSpecials.size()), int(e->mAntiAirs.size()), int(e->mForwards.size()), int(e->mSupers.size()));
#endif
	logFormat("Improved AI: player %d plays with %d specials (%d anti-air, %d forward) and %d supers.", p->mRootID, int(e->mSpecials.size()), int(e->mAntiAirs.size()), int(e->mForwards.size()), int(e->mSupers.size()));
}

// --- Its play ---

static float getLevelFactor(DreamPlayer* p) {
	const int level = std::max(1, std::min(8, getPlayerAILevel(p)));
	return (level - 1) / 7.0f;
}

static int chance(float tProbability) {
	return randfrom(0, 1) < tProbability;
}

static void hold(SmartAI* e, int tHorizontal, int tVertical, int tFrames) {
	e->mHorizontal = tHorizontal;
	e->mVertical = tVertical;
	e->mHoldTime = tFrames;
}

static void press(SmartAI* e, int tButton) {
	if (e->mButton >= 0 || e->mButtonTime > 0) return;
	e->mButton = tButton;
	e->mButtonTime = BUTTON_HOLD_FRAMES;
}

static void doCommand(DreamPlayer* p, const N3DSSpecialCommand& tCommand) {
	setDreamPlayerCommandActiveForAI(p->mCommandID, tCommand.mName.c_str(), COMMAND_ACTIVE_FRAMES);
}

static int doRandomOf(DreamPlayer* p, SmartAI* e, const std::vector<int>& tIndices) {
	if (tIndices.empty()) return 0;
	doCommand(p, e->mSpecials[tIndices[randfromInteger(0, int(tIndices.size()) - 1)]]);
	return 1;
}

static int doRandomSpecial(DreamPlayer* p, SmartAI* e) {
	if (e->mSpecials.empty()) return 0;
	doCommand(p, e->mSpecials[randfromInteger(0, int(e->mSpecials.size()) - 1)]);
	return 1;
}

// A super it has the power for.
static int doSuperIfPowered(DreamPlayer* p, SmartAI* e) {
	std::vector<int> powered;
	for (int i = 0; i < int(e->mSupers.size()); i++) {
		if (getPlayerPower(p) >= std::max(1000, e->mSupers[i].mPower)) powered.push_back(i);
	}
	if (powered.empty()) return 0;
	doCommand(p, e->mSupers[powered[randfromInteger(0, int(powered.size()) - 1)]]);
	return 1;
}

// A light, middle or heavy punch or kick, light the most.
static int getRandomAttackButton() {
	const int isKick = randfromInteger(0, 1);
	const float r = randfrom(0, 1);
	const int strength = r < 0.45f ? 0 : r < 0.8f ? 1 : 2;
	return (isKick ? BUTTON_A : BUTTON_X) + strength;
}

static void applyInput(DreamPlayer* p, SmartAI* e) {
	const int i = p->mRootID;
	if (e->mHoldTime > 0) {
		e->mHoldTime--;
		if (e->mHorizontal || e->mVertical) {
			const int isForward = e->mHorizontal > 0, isBack = e->mHorizontal < 0;
			const int isFacingRight = getPlayerIsFacingRight(p);
			setDreamDirectionsActiveForPlayer(i, e->mVertical > 0, e->mVertical < 0, isFacingRight ? isBack : isForward, isFacingRight ? isForward : isBack);
		}
	}
	if (e->mButtonTime > 0) {
		e->mButtonTime--;
		if (e->mButton >= 0) {
			switch (e->mButton) {
			case BUTTON_A: setDreamButtonAActiveForPlayer(i); break;
			case BUTTON_B: setDreamButtonBActiveForPlayer(i); break;
			case BUTTON_C: setDreamButtonCActiveForPlayer(i); break;
			case BUTTON_X: setDreamButtonXActiveForPlayer(i); break;
			case BUTTON_Y: setDreamButtonYActiveForPlayer(i); break;
			default: setDreamButtonZActiveForPlayer(i); break;
			}
			if (!e->mButtonTime) {
				e->mButton = -1;
				e->mButtonTime = BUTTON_RELEASE_FRAMES;
			}
		}
	}
}

// The other's attack near: guard it (back, low if it crouches) as long as
// it is near, chosen once for each attack, the more likely the higher the
// level (a quarter of them at 1, four in five at 8).
static int updateGuarding(DreamPlayer* p, DreamPlayer* tOther, SmartAI* e, float tLevel) {
	const int state = getPlayerState(p);
	const int isGuardState = state >= 120 && state <= 155;
	if (!isPlayerInGuardDistance(p) || (!getPlayerControl(p) && !isGuardState)) {
		e->mGuardChosen = 0;
		return 0;
	}
	if (!e->mGuardChosen) e->mGuardChosen = chance(0.25f + 0.55f * tLevel) ? 1 : -1;
	if (e->mGuardChosen < 0) return 0;
	const int isLow = getPlayerStateType(tOther) == MUGEN_STATE_TYPE_CROUCHING;
	hold(e, -1, isLow ? -1 : 0, 1);
	e->mDecisionTime = 0;
	return 1;
}

// Its own attack hit: follow it with a special (or a super) as it lands,
// once an attack, as a person cancels.
static void updateCombo(DreamPlayer* p, DreamPlayer* tOther, SmartAI* e, float tLevel) {
	if (getPlayerStateMoveType(p) != MUGEN_STATE_MOVE_TYPE_ATTACK) {
		e->mComboTried = 0;
		return;
	}
	if (e->mComboTried || getPlayerMoveContactCounter(p) <= 0 || getPlayerStateMoveType(tOther) != MUGEN_STATE_MOVE_TYPE_BEING_HIT) return;
	e->mComboTried = 1;
	if (!chance(0.2f + 0.6f * tLevel)) return;
	if (chance(0.35f) && doSuperIfPowered(p, e)) return;
	if (doRandomSpecial(p, e)) return;
	press(e, randfromInteger(0, 1) ? BUTTON_Y : BUTTON_B);
}

// In the air near the other, coming down: its attack, once a jump.
static void updateAirAttack(DreamPlayer* p, float tDistance, SmartAI* e) {
	if (getPlayerStateType(p) != MUGEN_STATE_TYPE_AIR) {
		e->mAirAttackDone = 0;
		return;
	}
	if (e->mAirAttackDone || getPlayerStateMoveType(p) != MUGEN_STATE_MOVE_TYPE_IDLE || tDistance > 60) return;
	if (getPlayerVelocityY(p, getPlayerCoordinateP(p)) < 0 && tDistance > 30) return;
	e->mAirAttackDone = 1;
	press(e, randfromInteger(0, 1) ? BUTTON_C : BUTTON_Z);
}

// With control on the ground: what to do next.
static void chooseGroundAction(DreamPlayer* p, DreamPlayer* tOther, float tDistance, SmartAI* e, float tLevel) {
	const int isOtherInAir = getPlayerStateType(tOther) == MUGEN_STATE_TYPE_AIR;
	// A jump coming: stop it.
	if (isOtherInAir && tDistance < DISTANCE_ANTI_AIR && chance(0.2f + 0.5f * tLevel)) {
		if (doRandomOf(p, e, e->mAntiAirs)) return;
		hold(e, 0, -1, 8);
		press(e, BUTTON_Z);
		return;
	}
	// Supers with the power for them, near.
	if (tDistance < DISTANCE_CLOSE * 1.5f && !e->mSupers.empty() && chance(0.1f + 0.2f * tLevel) && doSuperIfPowered(p, e)) return;

	const float r = randfrom(0, 1);
	if (tDistance < DISTANCE_CLOSE) {
		if (r < 0.6f) {
			if (chance(0.35f)) hold(e, 0, -1, 10);
			press(e, getRandomAttackButton());
		}
		else if (r < 0.72f) {
			if (!doRandomSpecial(p, e)) press(e, getRandomAttackButton());
		}
		else if (r < 0.84f) hold(e, -1, 0, randfromInteger(8, 20));
		else hold(e, 0, -1, randfromInteger(6, 16));
	}
	else if (tDistance < DISTANCE_MIDDLE) {
		if (r < 0.45f) hold(e, 1, 0, randfromInteger(15, 35));
		else if (r < 0.6f) {
			if (!doRandomOf(p, e, e->mForwards)) hold(e, 1, 0, 20);
		}
		else if (r < 0.72f) hold(e, 1, 1, 6);
		else if (r < 0.84f) press(e, chance(0.5f) ? BUTTON_B : BUTTON_Y);
		else hold(e, 0, 0, randfromInteger(5, 15));
	}
	else {
		if (r < 0.7f) hold(e, 1, 0, randfromInteger(25, 50));
		else if (r < 0.85f) {
			if (!doRandomOf(p, e, e->mForwards)) hold(e, 1, 0, 30);
		}
		else hold(e, 1, 1, 6);
	}
}

int updateN3DSSmartAI(DreamPlayer* p) {
	SmartAI* e = &gSmartAI.mAI[p->mRootID & 1];
	if (!gSmartAI.mIsEnabled || !e->mIsPrepared || !e->mIsPlaying) return 0;
	if (gSmartAI.mOnlyRootIndex >= 0 && gSmartAI.mOnlyRootIndex != p->mRootID) return 0;
	DreamPlayer* other = getPlayerOtherPlayer(p);
	if (!other) return 0;
	const float level = getLevelFactor(p);
	const float distance = getPlayerDistanceToFrontOfOtherPlayerX(p, getPlayerCoordinateP(p)) * 320.0f / std::max(1, getPlayerCoordinateP(p));

	if (!updateGuarding(p, other, e, level)) {
		updateCombo(p, other, e, level);
		updateAirAttack(p, distance, e);
		if (getPlayerControl(p) && getPlayerStateType(p) != MUGEN_STATE_TYPE_AIR) {
			// Its reaction: 18 frames at level 1, 8 at 8, some more at times.
			if (--e->mDecisionTime <= 0) {
				const int reaction = int(18 - 10 * level);
				e->mDecisionTime = reaction + randfromInteger(0, reaction / 2);
				chooseGroundAction(p, other, distance, e, level);
			}
		}
	}
	applyInput(p, e);
	return 1;
}

#else

int isN3DSSmartAIEnabled() { return 0; }
void setN3DSSmartAIEnabled(int) {}
void prepareN3DSSmartAI(DreamPlayer*) {}
int updateN3DSSmartAI(DreamPlayer*) { return 0; }
int isN3DSSmartAIPlaying(int) { return 0; }
void setN3DSSmartAIOnlyForTesting(int) {}

#endif
