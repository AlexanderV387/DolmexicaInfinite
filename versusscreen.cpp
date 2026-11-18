#include "versusscreen.h"

#include <assert.h>

#include <prism/mugenanimationhandler.h>
#include <prism/mugendefreader.h>
#include <prism/screeneffect.h>
#include <prism/input.h>
#include <prism/timer.h>
#include <prism/mugentexthandler.h>
#include <prism/clipboardhandler.h>
#include <prism/log.h>

#include "mugensound.h"
#include "scriptbackground.h"
#include "titlescreen.h"
#include "playerdefinition.h"
#include "config.h"
#include "stage.h"
#include "mugenstagehandler.h"
#include "smoothtext3ds.h"
#include <vector>
#include <algorithm>
#include <prism/texture.h>
#include <prism/animation.h>

#define VERSUS_SCREEN_PLAYER_IMAGE_Z 50
#define VERSUS_SCREEN_PLAYER_TEXT_Z 51

#define VERSUS_SCREEN_MATCH_TEXT_Z 80

typedef struct {
	Position mPosition;
	int mIsFacingRight;
	Vector3D mScale;

	Position mNamePosition;
	Vector3DI mNameFont;

	MugenSpriteFile mSprites;
	char* mDisplayCharacterName;
	MugenAnimation* mAnimation;
	MugenAnimationHandlerElement* mAnimationElement;
	int mTextID;
} VersusPlayer;

typedef struct {
	int mTime;

	int mFadeInTime;
	int mFadeOutTime;

	VersusPlayer mPlayers[2];
} VersusHeader;

static struct {
	VersusHeader mHeader;

	MugenDefScript mScript;
	MugenSpriteFile mSprites;
	MugenAnimations mAnimations;

	Vector2DI mLocalCoord;

	int mHasMatchNumber;
	int mMatchNumber;
	int mMatchTextID;

	void(*mCB)();
} gVersusScreenData;

#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
// [3DS] Each big portrait (9000,1) fit into the box a standard one fills
// (120x140 times the motif's p1.scale / p2.scale, from p1.pos / p2.pos,
// toward the facing), at the bottom, in the middle, not stretched. Drawn
// as they come (the scale was read but never used), Sub-Zero MKD's and
// Felicia's went out of their frames and New Kyouki's (axis 0,-335) was
// drawn under the screen.
static void fitN3DSVersusPortrait(VersusPlayer* tPlayer) {
	MugenSpriteFileSprite* sprite = getMugenSpriteFileTextureReference(&tPlayer->mSprites, 9000, 1);
	if (!tPlayer->mAnimationElement || !sprite || sprite->mOriginalTextureSize.x <= 0 || sprite->mOriginalTextureSize.y <= 0) return;
	const float boxW = 120 * float(tPlayer->mScale.x > 0 ? tPlayer->mScale.x : 1);
	const float boxH = 140 * float(tPlayer->mScale.y > 0 ? tPlayer->mScale.y : 1);
	const float boxX = tPlayer->mIsFacingRight ? float(tPlayer->mPosition.x) : float(tPlayer->mPosition.x) - boxW;
	const float boxY = float(tPlayer->mPosition.y);
	const float w = float(sprite->mOriginalTextureSize.x), h = float(sprite->mOriginalTextureSize.y);
	const float scale = std::min(boxW / w, boxH / h);
	const float left = boxX + (boxW - w * scale) / 2, top = boxY + boxH - h * scale;
	const float axisX = tPlayer->mIsFacingRight ? float(sprite->mAxisOffset.x) : w - float(sprite->mAxisOffset.x);
	setMugenAnimationPosition(tPlayer->mAnimationElement, Vector3D(left + axisX * scale, top + float(sprite->mAxisOffset.y) * scale, VERSUS_SCREEN_PLAYER_IMAGE_Z));
	setMugenAnimationDrawScale(tPlayer->mAnimationElement, Vector2D(scale, scale));
}
#endif

static void loadPlayerAnimationsAndName(int i) {
	VersusPlayer* player = &gVersusScreenData.mHeader.mPlayers[i];

	char file[200];
	char path[1024];
	char scriptPath[1024];
	char name[100];
	char palettePath[1024];

	getPlayerDefinitionPath(scriptPath, i);
	getPathToFile(path, scriptPath);

	MugenDefScript script;
	loadMugenDefScript(&script, scriptPath);

	// The fight's palette: the one chosen on the select screen, else the
	// game mode's (the first if random).
	const int preferredPalette = getRootPlayer(i)->mPreferredPalette > 0 ? getRootPlayer(i)->mPreferredPalette - 1 : 0;
	const int selectedPalette = getPlayerSelectedPalette(i);
	sprintf(name, "pal%d", selectedPalette ? selectedPalette : getMugenDefDefaultPaletteNumber(&script, preferredPalette + 1));
	getMugenDefStringOrDefault(file, &script, "files", name, "");
	int hasPalettePath = strcmp("", file);
	sprintf(palettePath, "%s%s", path, file);
	if(hasPalettePath && !isFile(palettePath)){
		logErrorFormat("Unable to find palette file %s. Ignoring.", palettePath);
		hasPalettePath = 0;
	}

	getMugenDefStringOrDefault(file, &script, "files", "sprite", "");
	assert(strcmp("", file));
	sprintf(scriptPath, "%s%s", path, file);
	player->mSprites = loadMugenSpriteFilePortraits(scriptPath, hasPalettePath, palettePath);
	if (isMugenDefStringVariable(&script, "info", "displayname"))
	{
		player->mDisplayCharacterName = getAllocatedMugenDefStringVariable(&script, "info", "displayname");
	}
	else
	{
		player->mDisplayCharacterName = getAllocatedMugenDefStringVariable(&script, "info", "name");
	}

	unloadMugenDefScript(&script);

	Position pos = player->mPosition;
	pos.z = VERSUS_SCREEN_PLAYER_IMAGE_Z;
	player->mAnimation = createOneFrameMugenAnimationForSprite(9000, 1);
	player->mAnimationElement = addMugenAnimation(player->mAnimation, &player->mSprites, pos);
	setMugenAnimationFaceDirection(player->mAnimationElement, player->mIsFacingRight);
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	fitN3DSVersusPortrait(player);
#endif

	pos = player->mNamePosition;
	pos.z = VERSUS_SCREEN_PLAYER_TEXT_Z;
	player->mTextID = addMugenText(player->mDisplayCharacterName, pos, player->mNameFont.x);
	setMugenTextColor(player->mTextID, getMugenTextColorFromMugenTextColorIndex(player->mNameFont.y));
	setMugenTextAlignment(player->mTextID, getMugenTextAlignmentFromMugenAlignmentIndex(player->mNameFont.z));
	setMugenTextScale(player->mTextID, transformDreamCoordinates(1, gVersusScreenData.mLocalCoord.x, getScreenSize().x));
}

static void loadVersusPlayer(int i) {
	VersusPlayer* player = &gVersusScreenData.mHeader.mPlayers[i];

	char playerName[100];
	sprintf(playerName, "p%d", i + 1);

	char fullVariableName[200];

	sprintf(fullVariableName, "%s.pos", playerName);
	player->mPosition = getMugenDefVectorOrDefault(&gVersusScreenData.mScript, "vs screen", fullVariableName, Vector3D(0, 0, 0));
	player->mPosition = transformDreamCoordinatesVectorXY(player->mPosition, gVersusScreenData.mLocalCoord.x, getScreenSize().x);

	sprintf(fullVariableName, "%s.facing", playerName);
	int faceDirection = getMugenDefIntegerOrDefault(&gVersusScreenData.mScript, "vs screen", fullVariableName, 1);
	player->mIsFacingRight = faceDirection == 1;

	sprintf(fullVariableName, "%s.scale", playerName);
	player->mScale = getMugenDefVectorOrDefault(&gVersusScreenData.mScript, "vs screen", fullVariableName, Vector3D(1, 1, 1));
	player->mScale = transformDreamCoordinatesVectorXY(player->mScale, gVersusScreenData.mLocalCoord.x, getScreenSize().x);

	sprintf(fullVariableName, "%s.name.pos", playerName);
	player->mNamePosition = getMugenDefVectorOrDefault(&gVersusScreenData.mScript, "vs screen", fullVariableName, Vector3D(0, 0, 0));
	player->mNamePosition = transformDreamCoordinatesVectorXY(player->mNamePosition, gVersusScreenData.mLocalCoord.x, getScreenSize().x);

	sprintf(fullVariableName, "%s.name.font", playerName);
	player->mNameFont = getMugenDefVectorIOrDefault(&gVersusScreenData.mScript, "vs screen", fullVariableName, Vector3DI(-1, 0, 0));


	loadPlayerAnimationsAndName(i);
}

static void loadVersusHeader() {
	gVersusScreenData.mHeader.mTime = getMugenDefIntegerOrDefault(&gVersusScreenData.mScript, "vs screen", "time", 60);

	gVersusScreenData.mHeader.mFadeInTime = getMugenDefIntegerOrDefault(&gVersusScreenData.mScript, "vs screen", "fadein.time", 10);
	gVersusScreenData.mHeader.mFadeOutTime = getMugenDefIntegerOrDefault(&gVersusScreenData.mScript, "vs screen", "fadeout.time", 10);

	int i;
	for (i = 0; i < 2; i++) {
		loadVersusPlayer(i);
	}
}

static std::string parseMatchTextString(const std::string& tText) {
	std::string s;
	for (size_t i = 0; i < tText.size(); i++) {
		if (tText[i] == '%' && i != tText.size() - 1 && tText[i + 1] == 'i') {
			s = s.append(std::to_string(gVersusScreenData.mMatchNumber));
			i++;
		}
		else {
			s.push_back(tText[i]);
		}
	}
	return s;
}

static void loadMatchText() {
	if (!gVersusScreenData.mHasMatchNumber) return;

	const auto text = getSTLMugenDefStringOrDefault(&gVersusScreenData.mScript, "vs screen", "match.text", "");
	auto offset = getMugenDefVectorOrDefault(&gVersusScreenData.mScript, "vs screen", "match.offset", Vector3D(0, 0, 0));
	offset = transformDreamCoordinatesVectorXY(offset, gVersusScreenData.mLocalCoord.x, getScreenSize().x);
	offset.z = VERSUS_SCREEN_MATCH_TEXT_Z;
	const auto font = getMugenDefVectorIOrDefault(&gVersusScreenData.mScript, "vs screen", "match.font", Vector3DI(-1, 0, 0));
	const auto parsedText = parseMatchTextString(text);

	gVersusScreenData.mMatchTextID = addMugenTextMugenStyle(parsedText.c_str(), offset, font);
	setMugenTextScale(gVersusScreenData.mMatchTextID, transformDreamCoordinates(1, gVersusScreenData.mLocalCoord.x, getScreenSize().x));
}

static void loadVersusMusic() {
	char* path = getAllocatedMugenDefStringOrDefault(&gVersusScreenData.mScript, "music", "vs.bgm", " ");
	int isLooping = getMugenDefIntegerOrDefault(&gVersusScreenData.mScript, "music", "vs.bgm.loop", 1);

	if (isMugenBGMMusicPath(path)) {
		playMugenBGMMusicPath(path, isLooping);
	}

	freeMemory(path);
}

#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
// [3DS] The bottom screen: the stage about to be fought in, its picture
// (taken in the first fight in it, stage.cpp; the select screen shows the
// same) over the whole screen, and its name; without a picture yet, the
// motif's background alone.
static struct {
	TextureData mTexture;
	int mHasTexture;
	AnimationHandlerElement* mPicture;
	AnimationHandlerElement* mBand;
	SmoothText mName;
	int mHasFonts;
} gN3DSVersusStage;

static std::string getN3DSStageShownName(const char* tDefinitionPath) {
	std::string name;
	if (!isFile(tDefinitionPath)) return name;
	MugenDefScript script;
	loadMugenDefScript(&script, tDefinitionPath);
	if (isMugenDefStringVariable(&script, "info", "displayname")) name = getSTLMugenDefStringVariable(&script, "info", "displayname");
	if (name.empty() && isMugenDefStringVariable(&script, "info", "name")) name = getSTLMugenDefStringVariable(&script, "info", "name");
	unloadMugenDefScript(&script);
	return name;
}

static void showN3DSVersusStage() {
	gN3DSVersusStage.mHasTexture = 0;
	gN3DSVersusStage.mPicture = NULL;
	gN3DSVersusStage.mBand = NULL;
	gN3DSVersusStage.mName.clear();
	gN3DSVersusStage.mHasFonts = 0;
	const char* stagePath = getDreamStageMugenDefinitionPath();
	if (!stagePath || !*stagePath) return;
	const auto sz = getScreenSize();

	Vector2DI size;
	if (loadDreamStagePictureTexture(stagePath, &gN3DSVersusStage.mTexture, &size)) {
		const int w = size.x, h = size.y;
		gN3DSVersusStage.mHasTexture = 1;
		// The whole screen covered, the picture's middle.
		const float scale = std::max(float(sz.x) / w, float(sz.y) / h);
		const float drawnW = w * scale, drawnH = h * scale;
		gN3DSVersusStage.mPicture = playAnimationLoop(Vector3D((sz.x - drawnW) / 2, (sz.y - drawnH) / 2, 70), &gN3DSVersusStage.mTexture, createOneFrameAnimation(), makeRectangle(0, 0, w - 1, h - 1));
		setAnimationScreen(gN3DSVersusStage.mPicture, 1);
		setAnimationSize(gN3DSVersusStage.mPicture, Vector3D(drawnW, drawnH, 1), Vector3D(0, 0, 0));
	}

	const std::string name = getN3DSStageShownName(stagePath);
	if (name.empty()) return;
	acquireSmoothTextFonts();
	gN3DSVersusStage.mHasFonts = 1;
	gN3DSVersusStage.mBand = playOneFrameAnimationLoop(Vector3D(0, sz.y - 24, 71), getEmptyWhiteTextureReference());
	setAnimationScreen(gN3DSVersusStage.mBand, 1);
	setAnimationSize(gN3DSVersusStage.mBand, Vector3D(sz.x, 24, 1), Vector3D(0, 0, 0));
	setAnimationColor(gN3DSVersusStage.mBand, 0, 0, 0);
	setAnimationTransparency(gN3DSVersusStage.mBand, 0.7f);
	addSmoothTextCentered(&gN3DSVersusStage.mName, SMOOTH_TEXT_LARGE, name, sz.x / 2.0f, sz.y - 22, 72, 1, Vector3D(1, 0.9, 0.5));
}

static void unloadN3DSVersusStage() {
	if (gN3DSVersusStage.mPicture) removeHandledAnimation(gN3DSVersusStage.mPicture);
	if (gN3DSVersusStage.mBand) removeHandledAnimation(gN3DSVersusStage.mBand);
	removeSmoothText(&gN3DSVersusStage.mName);
	if (gN3DSVersusStage.mHasTexture) unloadTexture(gN3DSVersusStage.mTexture);
	if (gN3DSVersusStage.mHasFonts) releaseSmoothTextFonts();
	gN3DSVersusStage.mPicture = NULL;
	gN3DSVersusStage.mBand = NULL;
	gN3DSVersusStage.mHasTexture = 0;
	gN3DSVersusStage.mHasFonts = 0;
}
#endif

static void screenTimeFinishedCB(void* tCaller);

static void loadVersusScreen() {
	std::string folder;
	const auto motifPath = getDolmexicaAssetFolder() + getMotifPath();
	loadMugenDefScript(&gVersusScreenData.mScript, motifPath);
	gVersusScreenData.mAnimations = loadMugenAnimationFile(motifPath);
	getPathToFile(folder, motifPath.c_str());

	auto text = getSTLMugenDefStringVariable(&gVersusScreenData.mScript, "files", "spr");
	text = findMugenSystemOrFightFilePath(text, folder);
	gVersusScreenData.mSprites = loadMugenSpriteFileWithoutPalette(text);

	gVersusScreenData.mLocalCoord = getMugenDefVectorIOrDefault(&gVersusScreenData.mScript, "info", "localcoord", Vector3DI(320, 240, 0)).xy();

	loadVersusHeader();
	loadScriptBackground(&gVersusScreenData.mScript, &gVersusScreenData.mSprites, &gVersusScreenData.mAnimations, "versusbgdef", "versusbg", gVersusScreenData.mLocalCoord);
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	setDreamMugenStageHandlerScreen(2);   // the motif's background below too
	showN3DSVersusStage();
#endif
	loadMatchText();
	loadVersusMusic();

	addFadeIn(gVersusScreenData.mHeader.mFadeInTime, NULL, NULL);
	addTimerCB(gVersusScreenData.mHeader.mTime, screenTimeFinishedCB, NULL);
}

static void unloadVersusScreen() {
#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)
	unloadN3DSVersusStage();
#endif
	unloadMugenDefScript(&gVersusScreenData.mScript);
	unloadMugenSpriteFile(&gVersusScreenData.mSprites);
	unloadMugenAnimationFile(&gVersusScreenData.mAnimations);

	int i;
	for (i = 0; i < 2; i++) {
		destroyMugenAnimation(gVersusScreenData.mHeader.mPlayers[i].mAnimation);
		unloadMugenSpriteFile(&gVersusScreenData.mHeader.mPlayers[i].mSprites);
		freeMemory(gVersusScreenData.mHeader.mPlayers[i].mDisplayCharacterName);
	}

	if (gVersusScreenData.mHasMatchNumber) {
		removeMugenText(gVersusScreenData.mMatchTextID);
	}
}

static void gotoNextScreenCB(void* /*tCaller*/) {
	gVersusScreenData.mCB();
}

static void screenTimeFinishedCB(void* /*tCaller*/) {
	addFadeOut(gVersusScreenData.mHeader.mFadeOutTime, gotoNextScreenCB, NULL);
}

static void updateVersusScreen() {
	if (hasPressedAFlank() || hasPressedStartFlank()) {
		screenTimeFinishedCB(NULL);
	}
}

static Screen gVersusScreen;

Screen* getVersusScreen() {
	gVersusScreen = makeScreen(loadVersusScreen, updateVersusScreen, NULL, unloadVersusScreen);
	return &gVersusScreen;
}

void setVersusScreenFinishedCB(void(*tCB)())
{
	gVersusScreenData.mCB = tCB;
}

void setVersusScreenMatchNumber(int tRound)
{
	gVersusScreenData.mMatchNumber = tRound;
	gVersusScreenData.mHasMatchNumber = 1;
}

void setVersusScreenNoMatchNumber()
{
	gVersusScreenData.mHasMatchNumber = 0;
}
