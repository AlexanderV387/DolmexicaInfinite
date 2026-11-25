#pragma once

// [3DS] "IA MEJORADA": the computer's play for a character with no AI of
// its own (no AILevel, no humanly impossible command asked for, no helper
// telling its player from its commands it is the computer). MUGEN's computer for those presses buttons at random, its moves
// whatever the buttons make; this one plays: it walks in, attacks when near,
// guards the other's attacks, stops jumps with its uppercut-like specials,
// throws its forward specials from afar, follows a hit with a special and
// uses its supers with the power for them, all sooner and more often the
// higher the level. Off: MUGEN's (ai.cpp). On by default.

#include "playerdefinition.h"

int isN3DSSmartAIEnabled();
void setN3DSSmartAIEnabled(int tIsEnabled);

// At the computer player's load (its commands loaded): whether it is for it,
// and its specials.
void prepareN3DSSmartAI(DreamPlayer* p);
// A frame of the fight (round state 2): 1 if it played the frame (MUGEN's
// random buttons not to be pressed then).
int updateN3DSSmartAI(DreamPlayer* p);

// For tests: whether the character tRootIndex plays with it.
int isN3DSSmartAIPlaying(int tRootIndex);
// For tests: only the character tRootIndex plays with it (-1 both).
void setN3DSSmartAIOnlyForTesting(int tRootIndex);

// [3DS] The computer at level 8 (the highest), whatever plays it (its own AI
// too): going into one of its supers (a -1 ChangeState asking for 1000
// power or more) the same as its last one, most times another it has the
// power for, of the same kind (ground or air), the least used first. The
// state to go into. Levels 1 to 7 as MUGEN.
int filterN3DSComputerSuperState(DreamPlayer* p, int tNewState);
