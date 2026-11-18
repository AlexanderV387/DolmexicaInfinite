#include "specialpanels3ds.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <string>
#include <vector>
#include <algorithm>

#include <prism/animation.h>
#include <prism/input.h>
#include <prism/mugentexthandler.h>
#include <prism/mugendefreader.h>
#include <prism/texture.h>
#include <prism/file.h>
#include <prism/wrapper.h>
#include <prism/log.h>
#include <prism/screeneffect.h>

#include "playerdefinition.h"
#include "mugencommandhandler.h"
#include "mugencommandreader.h"
#include "gamelogic.h"
#include "smoothtext3ds.h"

#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)

#define PANEL_Z 70
#define PANEL_SCREEN 1
#define PANEL_MARGIN 6.0f
#define PANEL_TOUCH_BUFFER_TIME 3   // the command true this many frames (2: one seen, after the command handler's)

#ifdef __3DS__
#define PANEL_SETTINGS_PATH "sdmc:/3ds/dolmexica/panels.txt"
#endif

typedef struct {
	std::string mCommandName;
	std::string mLabel;
	int mIsSuper;
	std::vector<int> mArrows;      // ARROW_*, -1 none
	std::vector<std::string> mAfterArrows;   // per step: what follows its arrow ("" none)
} PanelCommand;

typedef struct {
	AnimationHandlerElement* mBackground;
	SmoothText mElements;   // its letters and arrows
	GeoRectangle2D mRectangle;
	int mFlashFrames;
} Panel;

enum { ARROW_RIGHT, ARROW_DOWN_RIGHT, ARROW_DOWN, ARROW_DOWN_LEFT, ARROW_LEFT, ARROW_UP_LEFT, ARROW_UP, ARROW_UP_RIGHT, ARROW_AMOUNT };

static struct {
	int mAmount = 4;
	int mIsActive;
	int mShownAmount;
	std::vector<PanelCommand> mCommands;
	std::vector<Panel> mPanels;
	TextureData mArrowTextures[3][ARROW_AMOUNT];   // large, middle, small
	int mHasArrowTextures;
	int mHasFonts;
	int mWasTouching;
	int mTestTouch;
} gSpecialPanels;

int getN3DSSpecialPanelAmount() { return gSpecialPanels.mAmount; }

void setN3DSSpecialPanelAmount(int tAmount) {
	gSpecialPanels.mAmount = tAmount;
#ifdef __3DS__
	FILE* file = fopen(PANEL_SETTINGS_PATH, "w");
	if (!file) return;
	fprintf(file, "%d\n", tAmount);
	fclose(file);
#endif
}

void loadN3DSSpecialPanelSetting() {
#ifdef __3DS__
	FILE* file = fopen(PANEL_SETTINGS_PATH, "r");
	if (!file) return;
	int amount = 4;
	if (fscanf(file, "%d", &amount) == 1 && (amount == 0 || amount == 4 || amount == 8)) gSpecialPanels.mAmount = amount;
	fclose(file);
#else
	if (const char* amount = getenv("DOLMEXICA_PANELS")) gSpecialPanels.mAmount = atoi(amount);
#endif
}

// --- Which commands: read from the .cmd's [Statedef -1] ---

typedef struct {
	std::vector<std::string> mCommands;
	int mIsChangeState;
	int mPowerAsked;   // the most "power >= N" in its triggers asks, 0 none
} ControllerBlock;

static std::string toLower(std::string s) {
	for (auto& c : s) c = char(tolower((unsigned char)c));
	return s;
}

// command = "name" (not !=) in a trigger line.
static void collectCommandsInLine(const std::string& tLine, std::vector<std::string>* oCommands) {
	const std::string lower = toLower(tLine);
	size_t at = 0;
	while ((at = lower.find("command", at)) != std::string::npos) {
		size_t q = at + 7;
		while (q < lower.size() && isspace((unsigned char)lower[q])) q++;
		if (q < lower.size() && lower[q] == '=') {
			q++;
			while (q < lower.size() && isspace((unsigned char)lower[q])) q++;
			if (q < lower.size() && lower[q] == '"') {
				const size_t end = tLine.find('"', q + 1);
				if (end != std::string::npos) oCommands->push_back(tLine.substr(q + 1, end - q - 1));
			}
		}
		at += 7;
	}
}

// The power a line asks for: power >= N or power > N (0 none).
static int getLinePowerAsked(const std::string& tLine) {
	const std::string lower = toLower(tLine);
	int most = 0;
	size_t at = 0;
	while ((at = lower.find("power", at)) != std::string::npos) {
		const int isWord = (at == 0 || !isalnum((unsigned char)lower[at - 1])) && (at + 5 >= lower.size() || !isalnum((unsigned char)lower[at + 5]));
		size_t q = at + 5;
		at += 5;
		if (!isWord) continue;
		while (q < lower.size() && isspace((unsigned char)lower[q])) q++;
		if (q >= lower.size() || lower[q] != '>') continue;
		q++;
		if (q < lower.size() && lower[q] == '=') q++;
		most = std::max(most, atoi(lower.c_str() + q));
	}
	return most;
}

static std::vector<ControllerBlock> readMinusOneChangeStates(const std::string& tPath) {
	std::vector<ControllerBlock> blocks;
	// Through prism's files: the .def's name and the file's can differ in
	// case (Chun Li HD's JM_Chunli.cmd is JM_chunli.cmd).
	if (!isFile(tPath.c_str())) return blocks;
	Buffer buffer = fileToBuffer(tPath.c_str());
	const std::string text((const char*)buffer.mData, buffer.mLength);
	freeBuffer(buffer);
	int isInMinusOne = 0;
	size_t lineStart = 0;
	while (lineStart < text.size()) {
		size_t lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string::npos) lineEnd = text.size();
		std::string line = text.substr(lineStart, lineEnd - lineStart);
		lineStart = lineEnd + 1;
		const size_t comment = line.find(';');
		if (comment != std::string::npos) line = line.substr(0, comment);
		size_t first = 0;
		while (first < line.size() && isspace((unsigned char)line[first])) first++;
		if (first >= line.size()) continue;
		line = line.substr(first);
		if (line[0] == '[') {
			const std::string header = toLower(line);
			if (header.compare(0, 9, "[statedef") == 0) {
				isInMinusOne = atoi(header.c_str() + 9) == -1 && header.find("-1") != std::string::npos;
			}
			else if (isInMinusOne && header.compare(0, 6, "[state") == 0) {
				blocks.push_back(ControllerBlock{ {}, 0, 0 });
			}
			continue;
		}
		if (!isInMinusOne || blocks.empty()) continue;
		const std::string lower = toLower(line);
		ControllerBlock& block = blocks.back();
		if (lower.compare(0, 4, "type") == 0 && lower.find("changestate") != std::string::npos && lower.find("target") == std::string::npos && lower.find("self") == std::string::npos) block.mIsChangeState = 1;
		if (lower.compare(0, 7, "trigger") == 0) {
			collectCommandsInLine(line, &block.mCommands);
			block.mPowerAsked = std::max(block.mPowerAsked, getLinePowerAsked(line));
		}
	}
	return blocks;
}

// --- How a command is shown ---

static int getArrowForTarget(DreamMugenCommandInputStepTarget tTarget) {
	switch (tTarget) {
	case MUGEN_COMMAND_INPUT_STEP_TARGET_FORWARD: case MUGEN_COMMAND_INPUT_STEP_TARGET_MULTI_FORWARD: return ARROW_RIGHT;
	case MUGEN_COMMAND_INPUT_STEP_TARGET_BACKWARD: case MUGEN_COMMAND_INPUT_STEP_TARGET_MULTI_BACKWARD: return ARROW_LEFT;
	case MUGEN_COMMAND_INPUT_STEP_TARGET_UP: case MUGEN_COMMAND_INPUT_STEP_TARGET_MULTI_UP: return ARROW_UP;
	case MUGEN_COMMAND_INPUT_STEP_TARGET_DOWN: case MUGEN_COMMAND_INPUT_STEP_TARGET_MULTI_DOWN: return ARROW_DOWN;
	case MUGEN_COMMAND_INPUT_STEP_TARGET_DOWN_FORWARD: return ARROW_DOWN_RIGHT;
	case MUGEN_COMMAND_INPUT_STEP_TARGET_DOWN_BACKWARD: return ARROW_DOWN_LEFT;
	case MUGEN_COMMAND_INPUT_STEP_TARGET_UP_FORWARD: return ARROW_UP_RIGHT;
	case MUGEN_COMMAND_INPUT_STEP_TARGET_UP_BACKWARD: return ARROW_UP_LEFT;
	default: return -1;
	}
}

// MUGEN's buttons as the 3DS's (mugencommandhandler.cpp: c is R, z is L,
// s is SELECT).
static const char* getButtonName(DreamMugenCommandInputStepTarget tTarget) {
	switch (tTarget) {
	case MUGEN_COMMAND_INPUT_STEP_TARGET_A: return "A";
	case MUGEN_COMMAND_INPUT_STEP_TARGET_B: return "B";
	case MUGEN_COMMAND_INPUT_STEP_TARGET_C: return "R";
	case MUGEN_COMMAND_INPUT_STEP_TARGET_X: return "X";
	case MUGEN_COMMAND_INPUT_STEP_TARGET_Y: return "Y";
	case MUGEN_COMMAND_INPUT_STEP_TARGET_Z: return "L";
	case MUGEN_COMMAND_INPUT_STEP_TARGET_START: return "SEL";
	default: return NULL;
	}
}

// Its first [Command] with a direction and a button: its arrows and
// buttons; 0 if it has none such (a dash, a single button, or more than
// 10 steps: those made only for the computer, as 2002 Rugal's "cpu").
static int describeCommand(DreamMugenCommand& tCommand, PanelCommand* oPanel, std::string* oMotion) {
	for (int i = 0; i < vector_size(&tCommand.mInputs); i++) {
		auto input = (DreamMugenCommandInput*)vector_get(&tCommand.mInputs, i);
		const int stepAmount = vector_size(&input->mInputSteps);
		if (stepAmount < 2 && !(stepAmount == 1 && ((DreamMugenCommandInputStep*)vector_get(&input->mInputSteps, 0))->mType == MUGEN_COMMAND_INPUT_STEP_TYPE_MULTIPLE)) continue;
		if (stepAmount > 10) continue;
		PanelCommand panel;
		std::string motion;
		int hasButton = 0, hasDirection = 0;
		for (int s = 0; s < stepAmount; s++) {
			auto step = (DreamMugenCommandInputStep*)vector_get(&input->mInputSteps, s);
			std::vector<DreamMugenCommandInputStep*> parts;
			if (step->mType == MUGEN_COMMAND_INPUT_STEP_TYPE_MULTIPLE) {
				auto data = (DreamMugenCommandInputStepMultipleTargetData*)step->mData;
				for (int k = 0; k < vector_size(&data->mSubSteps); k++) parts.push_back((DreamMugenCommandInputStep*)vector_get(&data->mSubSteps, k));
			}
			else parts.push_back(step);
			int arrow = -1;
			std::string buttons;
			for (auto part : parts) {
				const int partArrow = getArrowForTarget(part->mTarget);
				if (partArrow >= 0) arrow = partArrow;
				else if (const char* name = getButtonName(part->mTarget)) {
					if (!buttons.empty()) buttons += "+";
					buttons += name;
				}
			}
			if (arrow >= 0) {
				hasDirection = 1;
				motion += char('0' + arrow);
				panel.mArrows.push_back(arrow);
				panel.mAfterArrows.push_back(buttons.empty() ? "" : "+" + buttons);
			}
			else if (!buttons.empty()) {
				hasButton = 1;
				if (!panel.mArrows.empty() && panel.mAfterArrows.back().empty()) panel.mAfterArrows.back() = "+" + buttons;
				else {
					panel.mArrows.push_back(-1);
					panel.mAfterArrows.push_back(buttons);
				}
			}
			if (arrow >= 0 && !buttons.empty()) hasButton = 1;
		}
		if (!hasButton || !hasDirection) continue;
		*oPanel = panel;
		*oMotion = motion;
		return 1;
	}
	return 0;
}

// Plain letters (not cp932's bytes), with a word in it (not "07" or "S03").
static int isReadableName(const std::string& tName) {
	int letters = 0;
	for (unsigned char c : tName) {
		if (c < 32 || c > 126) return 0;
		if (isalpha(c)) letters++;
	}
	return letters >= 3 && !(tName.size() <= 4 && letters < 3);
}

static std::string getShortName(const std::string& tName) {
	const size_t most = 18;
	if (tName.size() <= most) return tName;
	return tName.substr(0, most - 1) + ".";
}

// The specials and supers (1000 power or more asked), in the .cmd's order,
// one for each motion: the variant asking the least power (Kung Fu Man's
// palm with x, not its 330 power x+y listed first) and the first button.
// Half the panels for supers if it has as many.
static void findPanelCommands() {
	gSpecialPanels.mCommands.clear();
	DreamPlayer* p = getRootPlayer(0);
	char defPath[1024], folder[1024], file[300];
	getPlayerDefinitionPath(defPath, 0);
	if (!isFile(defPath)) return;
	getPathToFile(folder, defPath);
	MugenDefScript script;
	loadMugenDefScript(&script, defPath);
	getMugenDefStringOrDefault(file, &script, "files", "cmd", "");
	unloadMugenDefScript(&script);
	if (!*file) return;
	const auto blocks = readMinusOneChangeStates(std::string(folder) + file);

	auto& commands = p->mHeader->mFiles.mCommands.mCommands;
	std::vector<PanelCommand> found;
	std::vector<std::string> motions;
	std::vector<int> powers;
	std::vector<std::string> namesSeen;
	for (const auto& block : blocks) {
		if (!block.mIsChangeState) continue;
		for (const auto& name : block.mCommands) {
			if (std::find(namesSeen.begin(), namesSeen.end(), name) != namesSeen.end()) continue;
			namesSeen.push_back(name);
			// Finishers only after a round is won (Sub-Zero MKD's fatalities).
			if (toLower(name).find("fatal") != std::string::npos) continue;
			auto it = commands.find(name);
			if (it == commands.end()) continue;
			PanelCommand panel;
			std::string motion;
			if (!describeCommand(it->second, &panel, &motion)) continue;
			panel.mCommandName = name;
			panel.mIsSuper = block.mPowerAsked >= 1000;
			const auto seen = std::find(motions.begin(), motions.end(), motion);
			if (seen == motions.end()) {
				found.push_back(panel);
				motions.push_back(motion);
				powers.push_back(block.mPowerAsked);
			}
			else if (block.mPowerAsked < powers[seen - motions.begin()]) {
				found[seen - motions.begin()] = panel;
				powers[seen - motions.begin()] = block.mPowerAsked;
			}
		}
	}
	std::vector<PanelCommand> specials, supers;
	for (auto& panel : found) (panel.mIsSuper ? supers : specials).push_back(panel);
	for (size_t i = 0; i < specials.size(); i++) specials[i].mLabel = isReadableName(specials[i].mCommandName) ? getShortName(specials[i].mCommandName) : "ESPECIAL " + std::to_string(i + 1);
	for (size_t i = 0; i < supers.size(); i++) supers[i].mLabel = isReadableName(supers[i].mCommandName) ? getShortName(supers[i].mCommandName) : "SUPER " + std::to_string(i + 1);

	const size_t amount = size_t(gSpecialPanels.mShownAmount);
	size_t superAmount = std::min(supers.size(), amount / 2);
	size_t specialAmount = std::min(specials.size(), amount - superAmount);
	superAmount = std::min(supers.size(), amount - specialAmount);
	for (size_t i = 0; i < specialAmount; i++) gSpecialPanels.mCommands.push_back(specials[i]);
	for (size_t i = 0; i < superAmount; i++) gSpecialPanels.mCommands.push_back(supers[i]);
	logFormat("Special panels: %d specials and %d supers found, %d shown.", int(specials.size()), int(supers.size()), int(gSpecialPanels.mCommands.size()));
}

// --- Drawing ---
// Smooth letters (smoothtext3ds.cpp) and arrows made at the sizes drawn,
// anti-aliased: MUGEN's debug font scaled up read badly.

static const int PANEL_ARROW_SIZES[3] = { 20, 16, 13 };

static void unloadArrowTextures() {
	if (!gSpecialPanels.mHasArrowTextures) return;
	for (int k = 0; k < 3; k++) for (int a = 0; a < ARROW_AMOUNT; a++) unloadTexture(gSpecialPanels.mArrowTextures[k][a]);
	gSpecialPanels.mHasArrowTextures = 0;
}

// White arrows pointing their ways at the sizes drawn, each pixel's
// coverage from 4x4 samples (smooth edges).
static void makeArrowTextures() {
	if (gSpecialPanels.mHasArrowTextures) return;
	for (int k = 0; k < 3; k++) {
		const int size = PANEL_ARROW_SIZES[k];
		const float half = size / 2.0f, reach = size * 0.45f, shaft = size * 0.11f;
		std::vector<uint8_t> pixels(size_t(size) * size * 4);
		for (int a = 0; a < ARROW_AMOUNT; a++) {
			const float angle = a * float(M_PI) / 4;   // clockwise from right, y down
			const float cs = cosf(angle), sn = sinf(angle);
			for (int y = 0; y < size; y++) {
				for (int x = 0; x < size; x++) {
					int inside = 0;
					for (int sy = 0; sy < 4; sy++) {
						for (int sx = 0; sx < 4; sx++) {
							// Into the arrow's frame: u along it, v across.
							const float px = x + (sx + 0.5f) / 4 - half, py = y + (sy + 0.5f) / 4 - half;
							const float u = px * cs + py * sn, v = -px * sn + py * cs;
							const int isShaft = u >= -reach && u <= 0.1f * size && fabsf(v) <= shaft;
							const int isHead = u >= -0.05f * size && u <= reach && fabsf(v) <= (reach - u) * 0.95f;
							inside += isShaft || isHead;
						}
					}
					uint8_t* q = &pixels[(size_t(y) * size + x) * 4];
					q[0] = q[1] = q[2] = 255;
					q[3] = uint8_t(inside * 255 / 16);
				}
			}
			gSpecialPanels.mArrowTextures[k][a] = loadTextureFromARGB32Buffer(makeBuffer(pixels.data(), uint32_t(pixels.size())), size, size);
		}
	}
	gSpecialPanels.mHasArrowTextures = 1;
}

// The arrows and buttons' row width, with arrows of size index tSize.
static float getPanelRowWidth(const PanelCommand& tCommand, int tSize, int tFont) {
	float width = 0;
	for (size_t s = 0; s < tCommand.mArrows.size(); s++) {
		if (tCommand.mArrows[s] >= 0) width += PANEL_ARROW_SIZES[tSize] + (tSize < 2 ? 1 : 0);
		if (!tCommand.mAfterArrows[s].empty()) width += getSmoothTextWidth(tFont, tCommand.mAfterArrows[s]) + 2;
	}
	return width;
}

static void removePanels() {
	for (auto& panel : gSpecialPanels.mPanels) {
		if (panel.mBackground) removeHandledAnimation(panel.mBackground);
		removeSmoothText(&panel.mElements);
	}
	gSpecialPanels.mPanels.clear();
}

static void setPanelColor(Panel& tPanel, const PanelCommand& tCommand) {
	if (tPanel.mFlashFrames > 0) setAnimationColor(tPanel.mBackground, 0.9f, 0.9f, 0.9f);
	else if (tCommand.mIsSuper) setAnimationColor(tPanel.mBackground, 0.45f, 0.3f, 0.05f);
	else setAnimationColor(tPanel.mBackground, 0.08f, 0.15f, 0.4f);
}

static void addPanels() {
	removePanels();
	if (gSpecialPanels.mCommands.empty()) return;
	const auto sz = getScreenSize();
	const int isLarge = gSpecialPanels.mShownAmount <= 4;
	const int rows = isLarge ? 2 : 4;
	const float w = (sz.x - 3 * PANEL_MARGIN) / 2, h = (sz.y - (rows + 1) * PANEL_MARGIN) / rows;
	makeArrowTextures();
	if (!gSpecialPanels.mHasFonts) acquireSmoothTextFonts();
	gSpecialPanels.mHasFonts = 1;
	const int labelFont = isLarge ? SMOOTH_TEXT_LARGE : SMOOTH_TEXT_SMALL;
	for (size_t i = 0; i < gSpecialPanels.mCommands.size(); i++) {
		const auto& command = gSpecialPanels.mCommands[i];
		Panel panel;
		panel.mFlashFrames = 0;
		const float x = PANEL_MARGIN + (i % 2) * (w + PANEL_MARGIN), y = PANEL_MARGIN + (i / 2) * (h + PANEL_MARGIN);
		panel.mRectangle = GeoRectangle2D(x, y, w, h);
		panel.mBackground = playOneFrameAnimationLoop(Vector3D(x, y, PANEL_Z), getEmptyWhiteTextureReference());
		setAnimationScreen(panel.mBackground, PANEL_SCREEN);
		setAnimationSize(panel.mBackground, Vector3D(w, h, 1), Vector3D(0, 0, 0));
		setAnimationTransparency(panel.mBackground, 0.85f);
		setPanelColor(panel, command);

		// The name at the top, the arrows and buttons in the middle of the
		// rest (small ones if the large do not fit).
		const float pad = isLarge ? 6 : 4;
		const Vector3D labelColor = command.mIsSuper ? Vector3D(1, 0.85, 0.35) : Vector3D(0.75, 0.9, 1);
		addSmoothText(&panel.mElements, labelFont, command.mLabel, x + pad, y + pad - 1, PANEL_Z + 1, PANEL_SCREEN, labelColor, x + w - pad);
		int sizeIndex = isLarge ? 0 : 2;
		while (sizeIndex < 2 && getPanelRowWidth(command, sizeIndex, sizeIndex < 2 ? SMOOTH_TEXT_LARGE : SMOOTH_TEXT_SMALL) > w - 2 * pad) sizeIndex++;
		const int buttonFont = sizeIndex < 2 ? SMOOTH_TEXT_LARGE : SMOOTH_TEXT_SMALL;
		const int arrowSize = PANEL_ARROW_SIZES[sizeIndex];
		const float rowTop = y + pad + getSmoothTextLineHeight(labelFont) + 1;
		const float rowY = floorf(rowTop + (y + h - rowTop - arrowSize) / 2);
		float cursor = x + pad;
		for (size_t s = 0; s < command.mArrows.size(); s++) {
			if (command.mArrows[s] >= 0 && cursor + arrowSize <= x + w - pad) {
				auto element = playOneFrameAnimationLoop(Vector3D(cursor, rowY, PANEL_Z + 1), &gSpecialPanels.mArrowTextures[sizeIndex][command.mArrows[s]]);
				setAnimationScreen(element, PANEL_SCREEN);
				panel.mElements.push_back(element);
				cursor += arrowSize + (sizeIndex < 2 ? 1 : 0);
			}
			if (!command.mAfterArrows[s].empty()) {
				cursor = addSmoothText(&panel.mElements, buttonFont, command.mAfterArrows[s], cursor + 1, rowY + (arrowSize - getSmoothTextLineHeight(buttonFont)) / 2.0f, PANEL_Z + 1, PANEL_SCREEN, Vector3D(1, 1, 1), x + w - pad) + 1;
			}
		}
		gSpecialPanels.mPanels.push_back(panel);
	}
}

// --- Touching ---

static void touchPanel(int tPanel) {
	if (tPanel < 0 || tPanel >= int(gSpecialPanels.mPanels.size())) return;
	DreamPlayer* p = getRootPlayer(0);
	setDreamPlayerCommandActiveForAI(p->mCommandID, gSpecialPanels.mCommands[tPanel].mCommandName.c_str(), PANEL_TOUCH_BUFFER_TIME);
	gSpecialPanels.mPanels[tPanel].mFlashFrames = 6;
	setPanelColor(gSpecialPanels.mPanels[tPanel], gSpecialPanels.mCommands[tPanel]);
}

void touchN3DSSpecialPanelForTesting(int tPanel) { gSpecialPanels.mTestTouch = tPanel; }
int getN3DSSpecialPanelCommandAmount() { return int(gSpecialPanels.mCommands.size()); }
const char* getN3DSSpecialPanelCommandName(int tPanel) { return gSpecialPanels.mCommands[tPanel].mCommandName.c_str(); }

static int isWanted() {
	return gSpecialPanels.mAmount > 0 && isPlayerHuman(getRootPlayer(0));
}

static void showPanelsIfWanted() {
	gSpecialPanels.mShownAmount = isWanted() ? gSpecialPanels.mAmount : 0;
	if (!gSpecialPanels.mShownAmount) {
		gSpecialPanels.mCommands.clear();
		removePanels();
		return;
	}
	findPanelCommands();
	addPanels();
}

static void loadSpecialPanels(void* /*tData*/) {
	gSpecialPanels.mIsActive = 1;
	gSpecialPanels.mPanels.clear();
	gSpecialPanels.mCommands.clear();
	gSpecialPanels.mHasArrowTextures = 0;
	gSpecialPanels.mHasFonts = 0;
	gSpecialPanels.mWasTouching = 1;   // a touch held from before the fight is not one
	gSpecialPanels.mTestTouch = -1;
#ifndef __3DS__
	loadN3DSSpecialPanelSetting();   // the simulator's, from DOLMEXICA_PANELS
#endif
	showPanelsIfWanted();
}

static void unloadSpecialPanels(void* /*tData*/) {
	removePanels();
	unloadArrowTextures();
	if (gSpecialPanels.mHasFonts) releaseSmoothTextFonts();
	gSpecialPanels.mHasFonts = 0;
	gSpecialPanels.mIsActive = 0;
}

static void updateSpecialPanels(void* /*tData*/) {
	// The amount changed in the pause menu.
	if ((isWanted() ? gSpecialPanels.mAmount : 0) != gSpecialPanels.mShownAmount) showPanelsIfWanted();

	for (size_t i = 0; i < gSpecialPanels.mPanels.size(); i++) {
		auto& panel = gSpecialPanels.mPanels[i];
		if (panel.mFlashFrames > 0 && --panel.mFlashFrames == 0) setPanelColor(panel, gSpecialPanels.mCommands[i]);
	}

	if (gSpecialPanels.mTestTouch >= 0) {
		touchPanel(gSpecialPanels.mTestTouch);
		gSpecialPanels.mTestTouch = -1;
	}
	const int isTouching = hasPressedMouseLeftSingle(0);
	if (isTouching && !gSpecialPanels.mWasTouching) {
		for (size_t i = 0; i < gSpecialPanels.mPanels.size(); i++) {
			if (isMouseInRectangle(gSpecialPanels.mPanels[i].mRectangle)) touchPanel(int(i));
		}
	}
	gSpecialPanels.mWasTouching = isTouching;
}

ActorBlueprint getSpecialPanels3DS() {
	return makeActorBlueprint(loadSpecialPanels, unloadSpecialPanels, updateSpecialPanels);
}

#endif
