#include "fighttrace3ds.h"

// [3DS] A fight's last seconds, for a player who sees something wrong:
// every frame, each character's and helper's state, animation, position and
// the buttons held are kept (the last 5 seconds, in place: nothing taken
// from the heap). ZL writes them to marks.txt, what changed frame by frame;
// ZR writes the top screen to a .bmp.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <dirent.h>

#include <3ds.h>

#include <prism/log.h>

#include "playerdefinition.h"
#include "mugenexplod.h"
#include "gamelogic.h"
#include "mugenstatehandler.h"

namespace prism {
	uint32_t getN3DSKeysHeld();
	uint32_t getN3DSKeysDown();
	int saveN3DSScreenshot(const char* tPath);
}

#define TRACE_FRAMES 300
#define TRACE_ENTRIES 14
#define TRACE_FOLDER "sdmc:/3ds/dolmexica/"

typedef struct {
	int32_t mRoot;      // -1: none
	int32_t mID;        // the helper's id (0 for the character itself)
	int32_t mIsHelper;
	int32_t mState;
	int32_t mAnimation;
	int32_t mStep;
	int32_t mTime;
	int32_t mLife;
	int16_t mX, mY;
	int16_t mVelocityX10, mVelocityY10;   // tenths of a pixel a frame
	uint8_t mControl, mStateType, mMoveType, mIsHitPaused;
	uint8_t mIsInOthersStates, mIsBound;
} TraceEntry;

typedef struct {
	uint32_t mFrame;
	uint32_t mKeys;
	int32_t mExplods;
	int32_t mProjectiles;
	int32_t mPlayers;
	TraceEntry mEntries[TRACE_ENTRIES];
} TraceFrame;

static struct {
	TraceFrame mFrames[TRACE_FRAMES];
	int mAmount;
	int mNext;
	uint32_t mFrame;
	int mMarks;
} gFightTrace;

static void loadFightTrace(void* /*tData*/) {
	gFightTrace.mAmount = 0;
	gFightTrace.mNext = 0;
	gFightTrace.mFrame = 0;
}

static void recordFrame() {
	TraceFrame& f = gFightTrace.mFrames[gFightTrace.mNext];
	f.mFrame = gFightTrace.mFrame;
	f.mKeys = getN3DSKeysHeld();
	f.mExplods = getExplodAmount();
	f.mProjectiles = getPlayerRootProjectileAmount(getRootPlayer(0)) + getPlayerRootProjectileAmount(getRootPlayer(1));
	const int amount = getTotalPlayerAmount();
	f.mPlayers = amount;
	for (int i = 0; i < TRACE_ENTRIES; i++) {
		TraceEntry& e = f.mEntries[i];
		if (i >= amount) {
			e.mRoot = -1;
			continue;
		}
		DreamPlayer* p = getPlayerByIndex(i);
		e.mRoot = p->mRootID;
		e.mIsHelper = isPlayerHelper(p);
		e.mID = e.mIsHelper ? getPlayerID(p) : 0;
		e.mState = getPlayerState(p);
		e.mAnimation = getPlayerAnimationNumber(p);
		e.mStep = getPlayerAnimationStep(p);
		e.mTime = getPlayerTimeInState(p);
		e.mLife = getPlayerLife(p);
		const int coordinateP = getPlayerCoordinateP(p);
		e.mX = (int16_t)getPlayerPositionX(p, coordinateP);
		e.mY = (int16_t)getPlayerPositionY(p, coordinateP);
		e.mControl = (uint8_t)getPlayerControl(p);
		e.mStateType = (uint8_t)getPlayerStateType(p);
		e.mMoveType = (uint8_t)getPlayerStateMoveType(p);
		e.mIsHitPaused = (uint8_t)isPlayerHitPaused(p);
		e.mVelocityX10 = (int16_t)(getPlayerVelocityX(p, coordinateP) * 10);
		e.mVelocityY10 = (int16_t)(getPlayerVelocityY(p, coordinateP) * 10);
		e.mIsInOthersStates = (uint8_t)!isInOwnStateMachine(p->mRegisteredStateMachine);
		e.mIsBound = (uint8_t)isPlayerBound(p);
	}
	gFightTrace.mNext = (gFightTrace.mNext + 1) % TRACE_FRAMES;
	if (gFightTrace.mAmount < TRACE_FRAMES) gFightTrace.mAmount++;
}

static void describeKeys(uint32_t tKeys, char* oText, size_t tSize) {
	static const struct { uint32_t mKey; const char* mName; } names[] = {
		{ KEY_UP, "up" }, { KEY_DOWN, "down" }, { KEY_LEFT, "left" }, { KEY_RIGHT, "right" },
		{ KEY_A, "A" }, { KEY_B, "B" }, { KEY_X, "X" }, { KEY_Y, "Y" }, { KEY_L, "L" }, { KEY_R, "R" },
		{ KEY_START, "START" }, { KEY_SELECT, "SELECT" },
	};
	oText[0] = '\0';
	for (const auto& name : names) {
		if (!(tKeys & name.mKey)) continue;
		if (oText[0]) strncat(oText, " ", tSize - strlen(oText) - 1);
		strncat(oText, name.mName, tSize - strlen(oText) - 1);
	}
	if (!oText[0]) snprintf(oText, tSize, "none");
}

static const char* describeStateType(int tType) {
	switch (tType) {
	case MUGEN_STATE_TYPE_STANDING: return "S";
	case MUGEN_STATE_TYPE_CROUCHING: return "C";
	case MUGEN_STATE_TYPE_AIR: return "A";
	case MUGEN_STATE_TYPE_LYING: return "L";
	default: return "?";
	}
}

static const char* describeMoveType(int tType) {
	switch (tType) {
	case MUGEN_STATE_MOVE_TYPE_IDLE: return "I";
	case MUGEN_STATE_MOVE_TYPE_ATTACK: return "A";
	case MUGEN_STATE_MOVE_TYPE_BEING_HIT: return "H";
	default: return "?";
	}
}

static const TraceEntry* findEntry(const TraceFrame& tFrame, const TraceEntry& tWanted) {
	for (int i = 0; i < TRACE_ENTRIES; i++) {
		const TraceEntry& e = tFrame.mEntries[i];
		if (e.mRoot < 0) break;
		if (e.mRoot == tWanted.mRoot && e.mIsHelper == tWanted.mIsHelper && e.mID == tWanted.mID) return &e;
	}
	return NULL;
}

static void writeEntry(FILE* tFile, int tAge, const TraceEntry& e, const char* tWhat) {
	fprintf(tFile, "  -%3d P%d %s%-6d %s state %d%s (time %d) anim %d step %d pos %d,%d vel %.1f,%.1f life %d ctrl %d %s/%s%s%s\n",
		tAge, e.mRoot + 1, e.mIsHelper ? "helper " : "", e.mIsHelper ? e.mID : 0, tWhat,
		e.mState, e.mIsInOthersStates ? " (the other's)" : "", e.mTime, e.mAnimation, e.mStep, e.mX, e.mY, e.mVelocityX10 / 10.0, e.mVelocityY10 / 10.0, e.mLife, e.mControl,
		describeStateType(e.mStateType), describeMoveType(e.mMoveType), e.mIsHitPaused ? " hit-paused" : "", e.mIsBound ? " bound" : "");
}

// What changed from one frame to the next, oldest first (the number is how
// many frames before the mark).
static void writeMark() {
	gFightTrace.mMarks++;
	FILE* file = fopen(TRACE_FOLDER "marks.txt", "a");
	if (!file) return;
	// Written to the SD in a few large pieces, not one per line.
	static char buffer[64 * 1024];
	setvbuf(file, buffer, _IOFBF, sizeof(buffer));
	const auto info = mallinfo();
	DreamPlayer* p1 = getRootPlayer(0);
	DreamPlayer* p2 = getRootPlayer(1);
	fprintf(file, "\n=== mark %d: %s vs %s, round %d, fight frame %u, heap %d KB used, %d explods\n",
		gFightTrace.mMarks, getPlayerDisplayName(p1), getPlayerDisplayName(p2), getDreamRoundNumber(), (unsigned)gFightTrace.mFrame,
		(int)(info.uordblks / 1024), getExplodAmount());
	fprintf(file, "  (-N: frames before the mark; state types S/C/A/L, move types I/A/H)\n");

	const int start = (gFightTrace.mNext - gFightTrace.mAmount + TRACE_FRAMES) % TRACE_FRAMES;
	const TraceFrame* previous = NULL;
	for (int k = 0; k < gFightTrace.mAmount; k++) {
		const TraceFrame& f = gFightTrace.mFrames[(start + k) % TRACE_FRAMES];
		const int age = gFightTrace.mAmount - 1 - k;
		if (!previous || previous->mKeys != f.mKeys) {
			char keys[96];
			describeKeys(f.mKeys, keys, sizeof(keys));
			fprintf(file, "  -%3d buttons: %s\n", age, keys);
		}
		if (!previous || f.mExplods != previous->mExplods || f.mProjectiles != previous->mProjectiles) {
			fprintf(file, "  -%3d explods %d, projectiles %d\n", age, (int)f.mExplods, (int)f.mProjectiles);
		}
		for (int i = 0; i < TRACE_ENTRIES; i++) {
			const TraceEntry& e = f.mEntries[i];
			if (e.mRoot < 0) break;
			const TraceEntry* before = previous ? findEntry(*previous, e) : NULL;
			if (!before) {
				writeEntry(file, age, e, previous ? "appears:" : "");
				continue;
			}
			const int moved = abs(e.mX - before->mX) > 24 || abs(e.mY - before->mY) > 24;
			if (before->mState != e.mState || before->mAnimation != e.mAnimation || before->mControl != e.mControl
				|| before->mStateType != e.mStateType || before->mMoveType != e.mMoveType || before->mIsHitPaused != e.mIsHitPaused
				|| (before->mLife != e.mLife && !e.mIsHelper) || moved) {
				// Only a new step of the same animation: not written (most of
				// the lines, telling little).
				if (!(before->mState == e.mState && before->mAnimation == e.mAnimation && before->mControl == e.mControl && !moved
					&& before->mStateType == e.mStateType && before->mMoveType == e.mMoveType && before->mIsHitPaused == e.mIsHitPaused
					&& (before->mLife == e.mLife || e.mIsHelper))) {
					writeEntry(file, age, e, moved ? "jumps:" : "");
				}
			}
		}
		if (previous) {
			for (int i = 0; i < TRACE_ENTRIES; i++) {
				const TraceEntry& e = previous->mEntries[i];
				if (e.mRoot < 0) break;
				if (!findEntry(f, e)) fprintf(file, "  -%3d P%d %s%d gone\n", age, e.mRoot + 1, e.mIsHelper ? "helper " : "", e.mIsHelper ? e.mID : 0);
			}
		}
		previous = &f;
	}
	fclose(file);
	printf("\nMarca %d guardada en marks.txt\n", gFightTrace.mMarks);
}

// The next free screenshot number: the folder is read once (one pass over
// its names). Looking for it by opening screenshot-001.bmp, -002... one by
// one froze the game for 89 s on the console with many screenshots there.
static int findNextScreenshotNumber() {
	int highest = 0;
	DIR* folder = opendir(TRACE_FOLDER);
	if (!folder) return 1;
	while (struct dirent* entry = readdir(folder)) {
		int number;
		if (sscanf(entry->d_name, "screenshot-%d.bmp", &number) == 1 && number > highest) highest = number;
	}
	closedir(folder);
	return highest + 1;
}

static void takeScreenshot() {
	static int nextNumber = 0;
	if (!nextNumber) nextNumber = findNextScreenshotNumber();
	char path[96];
	snprintf(path, sizeof(path), TRACE_FOLDER "screenshot-%03d.bmp", nextNumber);
	if (saveN3DSScreenshot(path)) printf("\nCaptura guardada: screenshot-%03d.bmp\n", nextNumber);
	nextNumber++;
}

static void updateFightTrace(void* /*tData*/) {
	gFightTrace.mFrame++;
	recordFrame();
	const uint32_t down = getN3DSKeysDown();
	if (down & KEY_ZL) writeMark();
	if (down & KEY_ZR) takeScreenshot();
}

ActorBlueprint getFightTrace3DS() {
	return makeActorBlueprint(loadFightTrace, NULL, updateFightTrace);
}
