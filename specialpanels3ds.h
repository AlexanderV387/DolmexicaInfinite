#pragma once

// [3DS] Touch panels for the first player's specials and supers on the
// bottom screen during a fight (as Super Street Fighter IV 3D Edition's):
// each shows the command's arrows and buttons and, touched, makes the
// command come true as if entered. Read from the character's .cmd: the
// commands its -1 ChangeStates check, a super if its block asks for power.

#include <string>
#include <vector>

#include <prism/actorhandler.h>

using namespace prism;

ActorBlueprint getSpecialPanels3DS();

// 0 (none), 4 (the default) or 8; kept in the SD card.
int getN3DSSpecialPanelAmount();
void setN3DSSpecialPanelAmount(int tAmount);
void loadN3DSSpecialPanelSetting();

// For tests (the simulator has no touch screen): as if the panel were touched.
void touchN3DSSpecialPanelForTesting(int tPanel);
// For tests: the panels' commands, in order.
int getN3DSSpecialPanelCommandAmount();
const char* getN3DSSpecialPanelCommandName(int tPanel);

// A character's specials and supers as the panels find them (the computer's
// improved AI uses them too): one for each motion, in the .cmd's order.
typedef struct {
	std::string mName;
	std::string mMotion;   // the arrows, '0' forward, '1' down-forward, then on round to '7' up-forward
	int mPower;            // the power its ChangeState asks for
	int mIsSuper;          // 1000 or more
} N3DSSpecialCommand;
std::vector<N3DSSpecialCommand> findN3DSSpecialCommands(int tRootIndex);
