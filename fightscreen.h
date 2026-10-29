#pragma once

#include <stdio.h>
#include <prism/wrapper.h>

using namespace prism;

void startFightScreen(void(*tWinCB)(), void(*tLoseCB)() = NULL);
void reloadFightScreen();
void stopFightScreenWin();
void stopFightScreenLose();
void stopFightScreenToFixedScreen(Screen* tNextScreen);
// Leaves the fight for whatever tFunc starts (a mode's character select).
void stopFightScreenToFunction(void(*tFunc)());

Screen* getDreamFightScreenForTesting();