#pragma once

// [3DS] The fight's last 5 seconds kept; ZL writes them to marks.txt, ZR
// saves a screenshot (fighttrace3ds.cpp).

#include <prism/actorhandler.h>

using namespace prism;

ActorBlueprint getFightTrace3DS();
