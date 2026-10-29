#pragma once

// [3DS] The fight's pause menu: START stops the fight, with "continue",
// "choose characters" (the mode's character select) and "title screen". The
// 3DS has no Esc: without it, only the HOME Menu left a fight.

#include <prism/actorhandler.h>

using namespace prism;

ActorBlueprint getPauseMenu3DS();
