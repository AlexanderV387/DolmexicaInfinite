#pragma once

// [3DS] The fight's pause menu: START stops the fight, with "continue",
// "choose characters" (the mode's character select) and "title screen". The
// 3DS has no Esc: without it, only the HOME Menu left a fight.

#include <string>

#include <prism/actorhandler.h>

using namespace prism;

ActorBlueprint getPauseMenu3DS();
void loadN3DSScreenSetting();

// The round's time, as the pause menu's TIEMPO: the next (1) or the one
// before (-1) of 30, 45, 60, 75, 90, 99 and none, kept for the next start.
void stepN3DSRoundTime(int tDelta);
// "SIN LÍMITE" or its seconds.
std::string getN3DSRoundTimeText();
