#include "watchpanel3ds.h"

#include <stdio.h>
#include <algorithm>
#include <deque>
#include <string>

#include <prism/animation.h>
#include <prism/mugenanimationhandler.h>
#include <prism/mugenanimationreader.h>
#include <prism/mugenspritefilereader.h>
#include <prism/texture.h>
#include <prism/wrapper.h>
#include <prism/screeneffect.h>

#include "playerdefinition.h"
#include "stage.h"
#include "gamelogic.h"
#include "mugenstatereader.h"
#include "smoothtext3ds.h"

#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)

#define WATCH_SCREEN 1
#define WATCH_Z 60
#define FACE_SIZE 40.0f
#define BAR_WIDTH 106.0f
#define LOG_LINES 4
#define COMBO_SHOWN_FRAMES 90
#define SUPER_POWER_DROP 500        // a super: this much power gone at once
#define SPECIAL_CHECK_FRAMES 4      // a special's entry, then whether its power went (a super)
#define PICTURE_RETRY_FRAMES 120    // the stage's first fight: its picture is taken a second in

enum { STAT_HITS, STAT_DAMAGE, STAT_SPECIALS, STAT_SUPERS, STAT_GUARDS, STAT_COMBO, STAT_AMOUNT };
static const char* STAT_NAMES[STAT_AMOUNT] = { "GOLPES", "DAÑO", "ESPECIALES", "SÚPERS", "BLOQUEOS", "COMBO MÁX." };

typedef struct {
	MugenAnimation* mFaceAnimation;
	MugenAnimationHandlerElement* mFace;
	AnimationHandlerElement* mLifeBack;
	AnimationHandlerElement* mLife;
	AnimationHandlerElement* mPowerBack;
	AnimationHandlerElement* mPower;
	SmoothText mName;
	SmoothText mLifeText;
	SmoothText mPowerText;
	SmoothText mRounds;
	SmoothText mCombo;
	SmoothText mStatTexts[STAT_AMOUNT];

	int mShownLife, mShownPower, mShownRounds, mShownCombo;
	int mShownStats[STAT_AMOUNT];
	int mStats[STAT_AMOUNT];

	int mPreviousState;
	int mPreviousPower;
	int mPreviousOtherLife;
	int mPreviousCombo;
	int mComboFrames;          // the combo's number shown this much longer
	int mSpecialCheckFrames;   // a special entered: frames to tell it from a super
	int mSpecialPower;         // the power as it was entered
} WatchSide;

static struct {
	int mIsActive;
	int mHasFonts;
	WatchSide mSides[2];
	TextureData mPictureTexture;
	Vector2DI mPictureSize;
	int mHasPicture;
	int mPictureFrames;
	AnimationHandlerElement* mPicture;
	AnimationHandlerElement* mShade;
	AnimationHandlerElement* mStatsBand;
	AnimationHandlerElement* mLogBand;
	SmoothText mStatLabels[STAT_AMOUNT];
	SmoothText mStatsTitle;
	SmoothText mLogTitle;
	std::deque<std::string> mLog;
	std::deque<int> mLogSides;
	std::deque<std::string> mLogPlays;   // each line without its count
	std::deque<int> mLogCounts;
	SmoothText mLogTexts[LOG_LINES];
} gWatchPanel;

static const Vector3D SIDE_COLORS[2] = { Vector3D(0.55, 0.8, 1), Vector3D(1, 0.6, 0.55) };

static Vector2D getScreen() {
	const auto sz = getScreenSize();
	return Vector2D(sz.x, sz.y);
}

static AnimationHandlerElement* addRectangle(float x, float y, float w, float h, float z, const Vector3D& tColor, float tAlpha) {
	auto e = playOneFrameAnimationLoop(Vector3D(x, y, z), getEmptyWhiteTextureReference());
	setAnimationScreen(e, WATCH_SCREEN);
	setAnimationSize(e, Vector3D(w, h, 1), Vector3D(0, 0, 0));
	setAnimationColor(e, float(tColor.x), float(tColor.y), float(tColor.z));
	setAnimationTransparency(e, tAlpha);
	return e;
}

static void setRectangleWidth(AnimationHandlerElement* e, float x, float w, float h) {
	const Position pos = *getAnimationPositionReference(e);
	setAnimationPosition(e, Vector3D(x, pos.y, pos.z));
	setAnimationSize(e, Vector3D(std::max(0.0f, w), h, 1), Vector3D(0, 0, 0));
}

// Left side (P1) from its edge, right side (P2) mirrored.
static float getSideX(int i, float tFromEdge, float tWidth) {
	return i == 0 ? tFromEdge : float(getScreen().x) - tFromEdge - tWidth;
}

// --- The stage's picture ---

static int isWatching() {
	return getPlayerAILevel(getRootPlayer(0)) && getPlayerAILevel(getRootPlayer(1));
}

static void loadPictureIfThere() {
	const char* stagePath = getDreamStageMugenDefinitionPath();
	if (gWatchPanel.mHasPicture || !stagePath || !*stagePath) return;
	gWatchPanel.mHasPicture = loadDreamStagePictureTexture(stagePath, &gWatchPanel.mPictureTexture, &gWatchPanel.mPictureSize);
}

static void showPictureIfThere() {
	loadPictureIfThere();
	if (!gWatchPanel.mHasPicture || gWatchPanel.mPicture) return;
	const Vector2DI size = gWatchPanel.mPictureSize;
	const auto sz = getScreen();
	const float scale = std::max(float(sz.x) / size.x, float(sz.y) / size.y);
	const float w = size.x * scale, h = size.y * scale;
	gWatchPanel.mPicture = playAnimationLoop(Vector3D((sz.x - w) / 2, (sz.y - h) / 2, WATCH_Z), &gWatchPanel.mPictureTexture, createOneFrameAnimation(), makeRectangle(0, 0, size.x - 1, size.y - 1));
	setAnimationScreen(gWatchPanel.mPicture, WATCH_SCREEN);
	setAnimationSize(gWatchPanel.mPicture, Vector3D(w, h, 1), Vector3D(0, 0, 0));
}

// Before the fighters' sprites: after them the texture memory kept for
// their sprites to come leaves no room for it (the bottom screen came up
// black behind the numbers).
void preloadN3DSWatchPanel() {
	gWatchPanel.mHasPicture = 0;
	if (isWatching()) loadPictureIfThere();
}

// --- Each side's header ---

static void addFace(int i, DreamPlayer* p) {
	WatchSide& side = gWatchPanel.mSides[i];
	MugenSpriteFile* sprites = &p->mHeader->mFiles.mSprites;
	MugenSpriteFileSprite* sprite = getMugenSpriteFileTextureReference(sprites, 9000, 0);
	if (!sprite || sprite->mOriginalTextureSize.x <= 0 || sprite->mOriginalTextureSize.y <= 0) return;
	const float w = float(sprite->mOriginalTextureSize.x), h = float(sprite->mOriginalTextureSize.y);
	const float scale = std::min(FACE_SIZE / w, FACE_SIZE / h);
	const float left = getSideX(i, 4, FACE_SIZE) + (FACE_SIZE - w * scale) / 2, top = 4 + (FACE_SIZE - h * scale) / 2;
	side.mFaceAnimation = createOneFrameMugenAnimationForSprite(9000, 0);
	side.mFace = addMugenAnimation(side.mFaceAnimation, sprites, Vector3D(left + sprite->mAxisOffset.x * scale, top + sprite->mAxisOffset.y * scale, WATCH_Z + 3));
	setMugenAnimationScreen(side.mFace, WATCH_SCREEN);
	setMugenAnimationDrawScale(side.mFace, Vector2D(scale, scale));
}

static void showLife(int i, DreamPlayer* p) {
	WatchSide& side = gWatchPanel.mSides[i];
	const int life = std::max(0, getPlayerLife(p)), most = std::max(1, getPlayerLifeMax(p));
	if (life == side.mShownLife) return;
	side.mShownLife = life;
	const float fraction = std::min(1.0f, float(life) / most);
	const float w = BAR_WIDTH * fraction;
	setRectangleWidth(side.mLife, i == 0 ? 48 : getScreen().x - 48 - w, w, 10);
	setAnimationColor(side.mLife, fraction > 0.5f ? 0.3f + (1 - fraction) * 1.2f : 1.0f, fraction > 0.5f ? 0.9f : 0.3f + fraction * 1.2f, 0.25f);
	removeSmoothText(&side.mLifeText);
	const std::string text = std::to_string(life);
	const float x = i == 0 ? 48 : getScreen().x - 48 - getSmoothTextWidth(SMOOTH_TEXT_SMALL, text);
	addSmoothText(&side.mLifeText, SMOOTH_TEXT_SMALL, text, x, 30, WATCH_Z + 4, WATCH_SCREEN, Vector3D(1, 1, 1));
}

static void showPower(int i, DreamPlayer* p) {
	WatchSide& side = gWatchPanel.mSides[i];
	const int power = std::max(0, getPlayerPower(p)), most = std::max(1, getPlayerPowerMax(p));
	if (power == side.mShownPower) return;
	side.mShownPower = power;
	const float w = BAR_WIDTH * std::min(1.0f, float(power) / most);
	setRectangleWidth(side.mPower, i == 0 ? 48 : getScreen().x - 48 - w, w, 4);
	removeSmoothText(&side.mPowerText);
	const std::string text = "PODER " + std::to_string(power / 1000);
	const float x = i == 0 ? 48 + BAR_WIDTH - getSmoothTextWidth(SMOOTH_TEXT_SMALL, text) : getScreen().x - 48 - BAR_WIDTH;
	addSmoothText(&side.mPowerText, SMOOTH_TEXT_SMALL, text, x, 30, WATCH_Z + 4, WATCH_SCREEN, Vector3D(0.5, 0.8, 1));
}

static void showRounds(int i, DreamPlayer* p) {
	WatchSide& side = gWatchPanel.mSides[i];
	if (p->mRoundsWon == side.mShownRounds) return;
	side.mShownRounds = p->mRoundsWon;
	removeSmoothText(&side.mRounds);
	std::string text;
	for (int r = 0; r < p->mRoundsWon; r++) text += r ? " V" : "V";
	if (text.empty()) return;
	addSmoothTextCentered(&side.mRounds, SMOOTH_TEXT_SMALL, text, getSideX(i, 4, FACE_SIZE) + FACE_SIZE / 2, 46, WATCH_Z + 4, WATCH_SCREEN, Vector3D(1, 0.85, 0.2));
}

static void showCombo(int i, int tCombo) {
	WatchSide& side = gWatchPanel.mSides[i];
	if (tCombo == side.mShownCombo) return;
	side.mShownCombo = tCombo;
	removeSmoothText(&side.mCombo);
	if (tCombo < 2) return;
	const std::string text = std::to_string(tCombo) + " GOLPES";
	addSmoothTextCentered(&side.mCombo, SMOOTH_TEXT_LARGE, text, getScreen().x * (i == 0 ? 0.25f : 0.75f), 60, WATCH_Z + 4, WATCH_SCREEN, Vector3D(1, 0.65, 0.15));
}

// --- The numbers and the plays ---

#define STATS_TOP 82.0f
#define STAT_SPACING 13.0f
#define LOG_TOP 178.0f

static void showStat(int i, int tStat) {
	WatchSide& side = gWatchPanel.mSides[i];
	if (side.mStats[tStat] == side.mShownStats[tStat]) return;
	side.mShownStats[tStat] = side.mStats[tStat];
	removeSmoothText(&side.mStatTexts[tStat]);
	addSmoothTextCentered(&side.mStatTexts[tStat], SMOOTH_TEXT_SMALL, std::to_string(side.mStats[tStat]), getScreen().x * (i == 0 ? 0.17f : 0.83f), STATS_TOP + 14 + STAT_SPACING * tStat, WATCH_Z + 4, WATCH_SCREEN, SIDE_COLORS[i]);
}

static void showLog() {
	for (int l = 0; l < LOG_LINES; l++) {
		removeSmoothText(&gWatchPanel.mLogTexts[l]);
		if (l >= int(gWatchPanel.mLog.size())) continue;
		addSmoothText(&gWatchPanel.mLogTexts[l], SMOOTH_TEXT_SMALL, gWatchPanel.mLog[l], 10, LOG_TOP + 14 + 12 * l, WATCH_Z + 4, WATCH_SCREEN, SIDE_COLORS[gWatchPanel.mLogSides[l]], getScreen().x - 10);
	}
}

// The same play again by the same side: counted on its line ("x3").
static void addPlay(int i, const std::string& tWhat) {
	const std::string line = std::string(getPlayerDisplayName(getRootPlayer(i))) + ": " + tWhat;
	if (!gWatchPanel.mLog.empty() && gWatchPanel.mLogSides.front() == i && gWatchPanel.mLogPlays.front() == line) {
		gWatchPanel.mLogCounts.front()++;
		gWatchPanel.mLog.front() = line + " x" + std::to_string(gWatchPanel.mLogCounts.front());
		showLog();
		return;
	}
	gWatchPanel.mLog.push_front(line);
	gWatchPanel.mLogPlays.push_front(line);
	gWatchPanel.mLogCounts.push_front(1);
	gWatchPanel.mLogSides.push_front(i);
	while (int(gWatchPanel.mLog.size()) > LOG_LINES) {
		gWatchPanel.mLog.pop_back();
		gWatchPanel.mLogPlays.pop_back();
		gWatchPanel.mLogCounts.pop_back();
		gWatchPanel.mLogSides.pop_back();
	}
	showLog();
}

static int isGuardHitState(int tState) {
	return tState == 150 || tState == 152 || tState == 154;
}

// A special: an attack state of 1000 or more (not the common 5000s of being
// hit) entered from a normal one or none.
static int isSpecialState(DreamPlayer* p, int tState) {
	return tState >= 1000 && !(tState >= 5000 && tState < 6000) && getPlayerStateMoveType(p) == MUGEN_STATE_MOVE_TYPE_ATTACK;
}

static void updateSide(int i) {
	WatchSide& side = gWatchPanel.mSides[i];
	DreamPlayer* p = getRootPlayer(i);
	DreamPlayer* other = getRootPlayer(i ^ 1);
	showLife(i, p);
	showPower(i, p);
	showRounds(i, p);

	// Damage: the other's life lost.
	const int otherLife = getPlayerLife(other);
	if (otherLife < side.mPreviousOtherLife) side.mStats[STAT_DAMAGE] += side.mPreviousOtherLife - otherLife;
	if (otherLife <= 0 && side.mPreviousOtherLife > 0) addPlay(i, "¡KO!");
	side.mPreviousOtherLife = otherLife;

	// Hits and combos, as the bars count them.
	const int combo = p->mDisplayedComboCounter;
	if (combo > side.mPreviousCombo) {
		side.mStats[STAT_HITS] += combo - side.mPreviousCombo;
		side.mStats[STAT_COMBO] = std::max(side.mStats[STAT_COMBO], combo);
		side.mComboFrames = COMBO_SHOWN_FRAMES;
		showCombo(i, combo);
	}
	else if (combo < side.mPreviousCombo) {
		if (side.mPreviousCombo >= 3) addPlay(i, "COMBO DE " + std::to_string(side.mPreviousCombo) + " GOLPES");
	}
	side.mPreviousCombo = combo;
	if (side.mComboFrames > 0 && --side.mComboFrames == 0) showCombo(i, 0);

	// Specials, supers (its power spent at once) and guards.
	const int state = getPlayerState(p);
	const int power = getPlayerPower(p);
	if (state != side.mPreviousState) {
		if (isGuardHitState(state)) side.mStats[STAT_GUARDS]++;
		if (isSpecialState(p, state) && !(side.mPreviousState >= 1000 && side.mPreviousState < 5000) && !side.mSpecialCheckFrames) {
			side.mSpecialCheckFrames = SPECIAL_CHECK_FRAMES;
			side.mSpecialPower = side.mPreviousPower;
		}
	}
	if (side.mSpecialCheckFrames && --side.mSpecialCheckFrames == 0) {
		if (side.mSpecialPower - power >= SUPER_POWER_DROP) {
			side.mStats[STAT_SUPERS]++;
			addPlay(i, "SÚPER (PODER " + std::to_string((side.mSpecialPower - power + 999) / 1000) + ")");
		}
		else {
			side.mStats[STAT_SPECIALS]++;
			addPlay(i, "ESPECIAL");
		}
	}
	else if (!side.mSpecialCheckFrames && side.mPreviousPower - power >= SUPER_POWER_DROP) {
		side.mStats[STAT_SUPERS]++;
		addPlay(i, "SÚPER (PODER " + std::to_string((side.mPreviousPower - power + 999) / 1000) + ")");
	}
	side.mPreviousState = state;
	side.mPreviousPower = power;

	for (int s = 0; s < STAT_AMOUNT; s++) showStat(i, s);
}

// --- The actor ---

static void loadSide(int i) {
	WatchSide& side = gWatchPanel.mSides[i];
	side = WatchSide();
	DreamPlayer* p = getRootPlayer(i);
	const float barX = getSideX(i, 48, BAR_WIDTH);
	addFace(i, p);
	addSmoothText(&side.mName, SMOOTH_TEXT_SMALL, getPlayerDisplayName(p), i == 0 ? 48 : getScreen().x - 48 - std::min(BAR_WIDTH, getSmoothTextWidth(SMOOTH_TEXT_SMALL, getPlayerDisplayName(p))), 4, WATCH_Z + 4, WATCH_SCREEN, SIDE_COLORS[i], i == 0 ? 48 + BAR_WIDTH : getScreen().x - 48);
	side.mLifeBack = addRectangle(barX - 1, 17, BAR_WIDTH + 2, 12, WATCH_Z + 2, Vector3D(0.35, 0, 0), 0.9f);
	side.mLife = addRectangle(barX, 18, BAR_WIDTH, 10, WATCH_Z + 3, Vector3D(0.3, 0.9, 0.25), 1);
	side.mPowerBack = addRectangle(barX - 1, 42, BAR_WIDTH + 2, 6, WATCH_Z + 2, Vector3D(0, 0, 0.3), 0.9f);
	side.mPower = addRectangle(barX, 43, 0, 4, WATCH_Z + 3, Vector3D(0.3, 0.6, 1), 1);
	side.mShownLife = side.mShownPower = side.mShownRounds = -1;
	side.mShownCombo = 0;
	for (int s = 0; s < STAT_AMOUNT; s++) side.mShownStats[s] = -1;
	side.mPreviousState = getPlayerState(p);
	side.mPreviousPower = getPlayerPower(p);
	side.mPreviousOtherLife = getPlayerLife(getRootPlayer(i ^ 1));
}

static void loadWatchPanel(void* /*tData*/) {
	gWatchPanel.mIsActive = 0;
	gWatchPanel.mPicture = NULL;
	if (!isWatching()) {
		if (gWatchPanel.mHasPicture) unloadTexture(gWatchPanel.mPictureTexture);
		gWatchPanel.mHasPicture = 0;
		return;
	}
	gWatchPanel.mIsActive = 1;
	acquireSmoothTextFonts();
	gWatchPanel.mHasFonts = 1;
	gWatchPanel.mPictureFrames = 0;
	gWatchPanel.mLog.clear();
	gWatchPanel.mLogSides.clear();
	gWatchPanel.mLogPlays.clear();
	gWatchPanel.mLogCounts.clear();
	showPictureIfThere();

	const auto sz = getScreen();
	gWatchPanel.mShade = addRectangle(0, 0, sz.x, sz.y, WATCH_Z + 1, Vector3D(0, 0, 0), 0.45f);
	gWatchPanel.mStatsBand = addRectangle(4, STATS_TOP, sz.x - 8, 14 + STAT_SPACING * STAT_AMOUNT + 2, WATCH_Z + 2, Vector3D(0, 0, 0), 0.55f);
	gWatchPanel.mLogBand = addRectangle(4, LOG_TOP, sz.x - 8, 14 + 12 * LOG_LINES + 4, WATCH_Z + 2, Vector3D(0, 0, 0), 0.55f);
	addSmoothTextCentered(&gWatchPanel.mStatsTitle, SMOOTH_TEXT_SMALL, "ESTADÍSTICAS", sz.x / 2, STATS_TOP + 1, WATCH_Z + 4, WATCH_SCREEN, Vector3D(1, 0.9, 0.5));
	for (int s = 0; s < STAT_AMOUNT; s++) {
		addSmoothTextCentered(&gWatchPanel.mStatLabels[s], SMOOTH_TEXT_SMALL, STAT_NAMES[s], sz.x / 2, STATS_TOP + 14 + STAT_SPACING * s, WATCH_Z + 4, WATCH_SCREEN, Vector3D(0.85, 0.85, 0.85));
	}
	addSmoothTextCentered(&gWatchPanel.mLogTitle, SMOOTH_TEXT_SMALL, "ÚLTIMAS JUGADAS", sz.x / 2, LOG_TOP + 1, WATCH_Z + 4, WATCH_SCREEN, Vector3D(1, 0.9, 0.5));
	for (int i = 0; i < 2; i++) loadSide(i);
}

static void unloadWatchPanel(void* /*tData*/) {
	if (!gWatchPanel.mIsActive) return;
	for (auto& side : gWatchPanel.mSides) {
		if (side.mFace) removeMugenAnimation(side.mFace);
		if (side.mFaceAnimation) destroyMugenAnimation(side.mFaceAnimation);
		for (auto e : { side.mLifeBack, side.mLife, side.mPowerBack, side.mPower }) if (e) removeHandledAnimation(e);
		for (auto text : { &side.mName, &side.mLifeText, &side.mPowerText, &side.mRounds, &side.mCombo }) removeSmoothText(text);
		for (auto& text : side.mStatTexts) removeSmoothText(&text);
		side = WatchSide();
	}
	for (auto e : { gWatchPanel.mPicture, gWatchPanel.mShade, gWatchPanel.mStatsBand, gWatchPanel.mLogBand }) if (e) removeHandledAnimation(e);
	for (auto& text : gWatchPanel.mStatLabels) removeSmoothText(&text);
	for (auto& text : gWatchPanel.mLogTexts) removeSmoothText(&text);
	removeSmoothText(&gWatchPanel.mStatsTitle);
	removeSmoothText(&gWatchPanel.mLogTitle);
	if (gWatchPanel.mHasPicture) unloadTexture(gWatchPanel.mPictureTexture);
	if (gWatchPanel.mHasFonts) releaseSmoothTextFonts();
	gWatchPanel.mHasPicture = gWatchPanel.mHasFonts = gWatchPanel.mIsActive = 0;
}

static void updateWatchPanel(void* /*tData*/) {
	if (!gWatchPanel.mIsActive) return;
	// The stage's first fight: its picture comes a second in.
	if (!gWatchPanel.mHasPicture && ++gWatchPanel.mPictureFrames % PICTURE_RETRY_FRAMES == 0) showPictureIfThere();
	for (int i = 0; i < 2; i++) updateSide(i);
}

ActorBlueprint getWatchPanel3DS() {
	return makeActorBlueprint(loadWatchPanel, unloadWatchPanel, updateWatchPanel);
}

#else

ActorBlueprint getWatchPanel3DS() {
	return makeActorBlueprint(NULL, NULL, NULL);
}

void preloadN3DSWatchPanel() {}

#endif
