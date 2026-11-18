#include "titleinfo3ds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <set>
#include <algorithm>

#include <prism/animation.h>
#include <prism/input.h>
#include <prism/wrapper.h>
#include <prism/screeneffect.h>
#include <prism/file.h>
#include <prism/log.h>

#include "smoothtext3ds.h"
#include "characterautodetect.h"
#include "playerdefinition.h"
#include "gamelogic.h"
#include "fightscreen.h"
#include "stage.h"
#include "titlescreen.h"

#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)

#define TITLE_INFO_Z 80
#define TITLE_INFO_SCREEN 1

// A file of the port's own on the SD card (the simulator: its cache folder).
static std::string getDataPath(const char* tName) {
#ifdef __3DS__
	return std::string("sdmc:/3ds/dolmexica/") + tName;
#else
	const char* folder = getenv("N3DS_SIM_CACHE_DIR");
	return std::string(folder ? folder : "/tmp") + "/" + tName;
#endif
}

// --- The last fight ---

typedef struct {
	int mHas;
	std::string mDefinitions[2];
	std::string mStage;
	int mPalettes[2];
	int mLevels[2];   // 0: at the controls
	std::string mNames[2];
	std::string mStageName;
} LastFight;

static LastFight gLastFight;

static void saveLastFight() {
	FILE* file = fopen(getDataPath("ultimo.txt").c_str(), "w");
	if (!file) return;
	for (int i = 0; i < 2; i++) {
		fprintf(file, "def%d=%s\n", i, gLastFight.mDefinitions[i].c_str());
		fprintf(file, "pal%d=%d\n", i, gLastFight.mPalettes[i]);
		fprintf(file, "level%d=%d\n", i, gLastFight.mLevels[i]);
		fprintf(file, "name%d=%s\n", i, gLastFight.mNames[i].c_str());
	}
	fprintf(file, "stage=%s\n", gLastFight.mStage.c_str());
	fprintf(file, "stagename=%s\n", gLastFight.mStageName.c_str());
	fclose(file);
}

static void loadLastFight() {
	if (gLastFight.mHas) return;
	FILE* file = fopen(getDataPath("ultimo.txt").c_str(), "r");
	if (!file) return;
	char line[1200];
	LastFight fight;
	fight.mHas = 0;
	while (fgets(line, sizeof(line), file)) {
		std::string text(line);
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
		const size_t equal = text.find('=');
		if (equal == std::string::npos) continue;
		const std::string key = text.substr(0, equal), value = text.substr(equal + 1);
		for (int i = 0; i < 2; i++) {
			const std::string n = std::to_string(i);
			if (key == "def" + n) fight.mDefinitions[i] = value;
			else if (key == "pal" + n) fight.mPalettes[i] = atoi(value.c_str());
			else if (key == "level" + n) fight.mLevels[i] = atoi(value.c_str());
			else if (key == "name" + n) fight.mNames[i] = value;
		}
		if (key == "stage") fight.mStage = value;
		else if (key == "stagename") fight.mStageName = value;
	}
	fclose(file);
	fight.mHas = !fight.mDefinitions[0].empty() && !fight.mDefinitions[1].empty() && !fight.mStage.empty();
	if (fight.mHas && isFile(fight.mDefinitions[0].c_str()) && isFile(fight.mDefinitions[1].c_str())) gLastFight = fight;
}

void recordN3DSLastFight() {
	switch (getGameMode()) {
	case GAME_MODE_ARCADE: case GAME_MODE_FREE_PLAY: case GAME_MODE_VERSUS: case GAME_MODE_TRAINING:
	case GAME_MODE_SURVIVAL: case GAME_MODE_WATCH: case GAME_MODE_SUPER_WATCH:
		break;
	default:
		return;
	}
	char path[1024];
	for (int i = 0; i < 2; i++) {
		DreamPlayer* p = getRootPlayer(i);
		getPlayerDefinitionPath(path, i);
		gLastFight.mDefinitions[i] = path;
		gLastFight.mPalettes[i] = p->mPaletteNumber;
		gLastFight.mLevels[i] = getPlayerAILevel(p);
		gLastFight.mNames[i] = getPlayerDisplayName(p);
	}
	gLastFight.mStage = getDreamStageMugenDefinitionPath();
	const char* stageName = getDreamStageDisplayName();
	gLastFight.mStageName = (stageName && *stageName) ? stageName : getDreamStageName();
	gLastFight.mHas = 1;
	saveLastFight();
}

static void lastFightOverCB() {
	setNewScreen(getDreamTitleScreen());
}

int startN3DSLastFightIfWanted() {
	if (!gLastFight.mHas || !hasPressedYFlank()) return 0;
	logFormat("Title: the last fight again (%s vs %s).", gLastFight.mNames[0].c_str(), gLastFight.mNames[1].c_str());
	for (int i = 0; i < 2; i++) {
		setPlayerDefinitionPath(i, gLastFight.mDefinitions[i].c_str());
		// Over what the mode sets (playerdefinition's select screen choices).
		setPlayerSelectedPalette(i, gLastFight.mPalettes[i]);
		setPlayerSelectedAILevel(i, gLastFight.mLevels[i]);
	}
	char noMusic[2] = "";
	setDreamStageMugenDefinition(gLastFight.mStage.c_str(), noMusic);
	// Two at the controls: versus; one: free play; none: watch.
	if (!gLastFight.mLevels[0] && !gLastFight.mLevels[1]) setGameModeVersus();
	else if (!gLastFight.mLevels[0]) setGameModeFreePlay();
	else setGameModeWatch();
	startFightScreen(lastFightOverCB, lastFightOverCB);
	return 1;
}

// --- What the SD card holds, and what is new ---

static struct {
	int mIsContentShown = 1;
	int mHasCounted;
	int mCharacterAmount;
	int mStageAmount;
	std::vector<std::string> mNew;   // for this session

	std::vector<AnimationHandlerElement*> mBackdrops;
	SmoothText mModeText;
	SmoothText mContentText;
	SmoothText mLastFightText;
	std::string mShownModeKey;
	int mIsLoaded;
} gTitleInfo;

int isN3DSTitleContentShown() { return gTitleInfo.mIsContentShown; }

void setN3DSTitleContentShown(int tIsShown) {
	gTitleInfo.mIsContentShown = tIsShown;
	FILE* file = fopen(getDataPath("inicio.txt").c_str(), "w");
	if (!file) return;
	fprintf(file, "content %d\n", tIsShown);
	fclose(file);
}

void loadN3DSTitleInfoSetting() {
	FILE* file = fopen(getDataPath("inicio.txt").c_str(), "r");
	if (!file) return;
	int value;
	if (fscanf(file, "content %d", &value) == 1) gTitleInfo.mIsContentShown = value;
	fclose(file);
}

static std::string getStageShownName(const std::string& tPath) {
	std::string name = tPath.substr(tPath.find_last_of('/') + 1);
	if (name.size() > 4) name = name.substr(0, name.size() - 4);
	return name;
}

// Once a session: the counts, and what was not there the last time (the
// list of known ones on the SD card; the first time, nothing is "new").
static void countContent() {
	if (gTitleInfo.mHasCounted) return;
	gTitleInfo.mHasCounted = 1;
	std::vector<std::string> characters, stages;
	getAutoDetectedContent(&characters, &stages);
	gTitleInfo.mCharacterAmount = int(characters.size());
	gTitleInfo.mStageAmount = int(stages.size());

	std::set<std::string> known;
	const std::string knownPath = getDataPath("conocidos.txt");
	FILE* file = fopen(knownPath.c_str(), "r");
	const int hadKnown = file != NULL;
	if (file) {
		char line[1100];
		while (fgets(line, sizeof(line), file)) {
			std::string text(line);
			while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
			known.insert(text);
		}
		fclose(file);
	}
	file = fopen(knownPath.c_str(), "w");
	for (const auto& character : characters) {
		const std::string key = "c:" + character;
		if (hadKnown && !known.count(key)) gTitleInfo.mNew.push_back(character);
		if (file) fprintf(file, "%s\n", key.c_str());
	}
	for (const auto& stage : stages) {
		const std::string key = "s:" + stage;
		if (hadKnown && !known.count(key)) gTitleInfo.mNew.push_back(getStageShownName(stage));
		if (file) fprintf(file, "%s\n", key.c_str());
	}
	if (file) fclose(file);
}

// --- Drawing ---

static void addBackdrop(float tY, float tH) {
	const auto sz = getScreenSize();
	auto element = playOneFrameAnimationLoop(Vector3D(6, tY, TITLE_INFO_Z), getEmptyWhiteTextureReference());
	setAnimationScreen(element, TITLE_INFO_SCREEN);
	setAnimationSize(element, Vector3D(sz.x - 12, tH, 1), Vector3D(0, 0, 0));
	setAnimationColor(element, 0, 0, 0);
	setAnimationTransparency(element, 0.7f);
	gTitleInfo.mBackdrops.push_back(element);
}

// The text cut into lines that fit tWidth, at most tMost.
static std::vector<std::string> wrapText(int tFont, const std::string& tText, float tWidth, int tMost) {
	std::vector<std::string> lines;
	std::string line, word;
	auto flushWord = [&]() {
		if (word.empty()) return;
		const std::string tried = line.empty() ? word : line + " " + word;
		if (getSmoothTextWidth(tFont, tried) <= tWidth || line.empty()) line = tried;
		else {
			lines.push_back(line);
			line = word;
		}
		word.clear();
	};
	for (char c : tText) {
		if (c == ' ') flushWord();
		else word += c;
	}
	flushWord();
	if (!line.empty()) lines.push_back(line);
	if (int(lines.size()) > tMost) lines.resize(tMost);
	return lines;
}

static const char* getModeDescription(const std::string& tKey) {
	if (tKey == "menu.itemname.story") return "Sigue la historia de un personaje, combate tras combate.";
	if (tKey == "menu.itemname.arcade") return "Pelea contra la CPU, rival tras rival, hasta el final.";
	if (tKey == "menu.itemname.freeplay") return "Elige a tu personaje y a tu rival de la CPU, y pelea las veces que quieras.";
	if (tKey == "menu.itemname.versus") return "Dos jugadores, uno contra el otro.";
	if (tKey == "menu.itemname.netplay") return "Pelea en red contra otro jugador.";
	if (tKey == "menu.itemname.teamarcade" || tKey == "menu.itemname.teamversus" || tKey == "menu.itemname.teamcoop" || tKey == "menu.itemname.survivalcoop") return "Modo por equipos: todavía no existe en la 3DS; empieza el Arcade normal.";
	if (tKey == "menu.itemname.survival") return "Aguanta contra todos los rivales que puedas con una sola barra de vida.";
	if (tKey == "menu.itemname.training") return "Practica tus movimientos y combos sin límite de tiempo.";
	if (tKey == "menu.itemname.watch") return "Mira pelear a dos personajes controlados por la CPU.";
	if (tKey == "menu.itemname.superwatch") return "La CPU contra la CPU, una pelea tras otra.";
	if (tKey == "menu.itemname.randomwatch") return "La CPU contra la CPU con personajes y escenarios al azar.";
	if (tKey == "menu.itemname.osu") return "Pelea al ritmo de la música (modo osu! de Dolmexica).";
	if (tKey == "menu.itemname.options") return "Dificultad, vida, tiempo, velocidad, volumen y más.";
	if (tKey == "menu.itemname.credits") return "Quién hizo el juego y sus partes.";
	if (tKey == "menu.itemname.exit") return "Salir al menú de la 3DS.";
	return "";
}

static void showMode(const char* tModeKey, const char* tModeName) {
	removeSmoothText(&gTitleInfo.mModeText);
	const auto sz = getScreenSize();
	addSmoothText(&gTitleInfo.mModeText, SMOOTH_TEXT_LARGE, tModeName, 14, 12, TITLE_INFO_Z + 1, TITLE_INFO_SCREEN, Vector3D(1, 0.85, 0.3), sz.x - 14);
	const auto lines = wrapText(SMOOTH_TEXT_SMALL, getModeDescription(tModeKey), sz.x - 28, 3);
	for (size_t i = 0; i < lines.size(); i++) {
		addSmoothText(&gTitleInfo.mModeText, SMOOTH_TEXT_SMALL, lines[i], 14, 34 + 14 * float(i), TITLE_INFO_Z + 1, TITLE_INFO_SCREEN, Vector3D(1, 1, 1), sz.x - 14);
	}
}

static void showContent(float tY) {
	const auto sz = getScreenSize();
	char text[120];
	snprintf(text, sizeof(text), "%d personajes  ·  %d escenarios", gTitleInfo.mCharacterAmount, gTitleInfo.mStageAmount);
	addSmoothText(&gTitleInfo.mContentText, SMOOTH_TEXT_SMALL, text, 14, tY + 4, TITLE_INFO_Z + 1, TITLE_INFO_SCREEN, Vector3D(0.75, 0.9, 1), sz.x - 14);
	if (gTitleInfo.mNew.empty()) return;
	std::string news = gTitleInfo.mNew.size() == 1 ? "Nuevo: " : "Nuevos: ";
	for (size_t i = 0; i < gTitleInfo.mNew.size(); i++) news += (i ? ", " : "") + gTitleInfo.mNew[i];
	if (getSmoothTextWidth(SMOOTH_TEXT_SMALL, news) > sz.x - 28) {
		// Cut, with how many more.
		std::string cut = news;
		const std::string more = "...";
		while (!cut.empty() && getSmoothTextWidth(SMOOTH_TEXT_SMALL, cut + more) > sz.x - 28) cut.pop_back();
		news = cut + more;
	}
	addSmoothText(&gTitleInfo.mContentText, SMOOTH_TEXT_SMALL, news, 14, tY + 18, TITLE_INFO_Z + 1, TITLE_INFO_SCREEN, Vector3D(1, 1, 0.4), sz.x - 14);
}

static void showLastFight(float tY) {
	const auto sz = getScreenSize();
	addSmoothText(&gTitleInfo.mLastFightText, SMOOTH_TEXT_SMALL, "Último combate", 14, tY + 4, TITLE_INFO_Z + 1, TITLE_INFO_SCREEN, Vector3D(0.75, 0.9, 1), sz.x - 14);
	const std::string fight = gLastFight.mNames[0] + " vs " + gLastFight.mNames[1];
	addSmoothText(&gTitleInfo.mLastFightText, SMOOTH_TEXT_LARGE, fight, 14, tY + 17, TITLE_INFO_Z + 1, TITLE_INFO_SCREEN, Vector3D(1, 1, 1), sz.x - 14);
	const std::string where = "en " + gLastFight.mStageName;
	const std::string hint = "Y: repetir";
	const float hintX = sz.x - 14 - getSmoothTextWidth(SMOOTH_TEXT_SMALL, hint);
	addSmoothText(&gTitleInfo.mLastFightText, SMOOTH_TEXT_SMALL, where, 14, tY + 37, TITLE_INFO_Z + 1, TITLE_INFO_SCREEN, Vector3D(0.85, 0.85, 0.85), hintX - 8);
	addSmoothText(&gTitleInfo.mLastFightText, SMOOTH_TEXT_SMALL, hint, hintX, tY + 37, TITLE_INFO_Z + 1, TITLE_INFO_SCREEN, Vector3D(1, 0.85, 0.3));
}

void loadN3DSTitleInfo() {
	acquireSmoothTextFonts();
	gTitleInfo.mIsLoaded = 1;
	gTitleInfo.mBackdrops.clear();
	gTitleInfo.mModeText.clear();
	gTitleInfo.mContentText.clear();
	gTitleInfo.mLastFightText.clear();
	gTitleInfo.mShownModeKey = "";
	countContent();
	loadLastFight();

	const auto sz = getScreenSize();
	addBackdrop(6, 82);   // the mode
	float y = float(sz.y) - 6;
	if (gLastFight.mHas) {
		y -= 54;
		addBackdrop(y, 54);
		showLastFight(y);
		y -= 6;
	}
	if (gTitleInfo.mIsContentShown) {
		const float h = gTitleInfo.mNew.empty() ? 22.0f : 36.0f;
		y -= h;
		addBackdrop(y, h);
		showContent(y);
	}
}

void updateN3DSTitleInfo(const char* tModeKey, const char* tModeName) {
	if (!gTitleInfo.mIsLoaded || gTitleInfo.mShownModeKey == tModeKey) return;
	gTitleInfo.mShownModeKey = tModeKey;
	showMode(tModeKey, tModeName);
}

void unloadN3DSTitleInfo() {
	if (!gTitleInfo.mIsLoaded) return;
	for (auto element : gTitleInfo.mBackdrops) removeHandledAnimation(element);
	gTitleInfo.mBackdrops.clear();
	removeSmoothText(&gTitleInfo.mModeText);
	removeSmoothText(&gTitleInfo.mContentText);
	removeSmoothText(&gTitleInfo.mLastFightText);
	releaseSmoothTextFonts();
	gTitleInfo.mIsLoaded = 0;
}

#else

void loadN3DSTitleInfo() {}
void updateN3DSTitleInfo(const char*, const char*) {}
void unloadN3DSTitleInfo() {}
int startN3DSLastFightIfWanted() { return 0; }
void recordN3DSLastFight() {}
int isN3DSTitleContentShown() { return 1; }
void setN3DSTitleContentShown(int) {}
void loadN3DSTitleInfoSetting() {}

#endif
