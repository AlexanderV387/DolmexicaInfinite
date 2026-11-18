#pragma once

// [3DS] The bottom screen while the computer fights the computer: the
// stage's picture behind, and as a commentator would tell it, each side's
// face, name, life (with its number), power and rounds won, the combo going
// on, the fight's numbers so far (hits, damage, specials, supers, guards,
// longest combo) and its last plays.

#include <prism/actorhandler.h>

using namespace prism;

ActorBlueprint getWatchPanel3DS();
// At the fight's load, before the fighters: the stage's picture.
void preloadN3DSWatchPanel();
