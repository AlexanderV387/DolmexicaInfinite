#pragma once

#include <prism/wrapper.h>
#include <prism/mugendefreader.h>

using namespace prism;

Screen* getCharacterSelectScreen();

void setCharacterSelectCustomSelectFile(const std::string& tFileName);
void setCharacterSelectScreenModeName(const char* tModeName);
void setCharacterSelectFinishedCB(void(*tCB)());
void setCharacterSelectStageActive(int tUseOnlyOsuStages = 0);
void setCharacterSelectStageInactive();
void setCharacterSelectOnePlayer();
void setCharacterSelectOnePlayerSelectAll();
void setCharacterSelectTwoPlayers();
// After setCharacterSelectOnePlayer*/TwoPlayers: the sides the computer
// plays, chosen with a level on the select screen.
void setCharacterSelectComputerPlayers(int tIsFirstComputer, int tIsSecondComputer);
void setCharacterSelectNetplay(int isHost);
void setCharacterSelectCredits();
void setCharacterSelectStory();
void setCharacterSelectDisableReturnOneTime();
void resetCharacterSelectSelectors();

void parseOptionalCharacterSelectParameters(const MugenStringVector& tVector, int* oOrder, int* oDoesIncludeStage, char* oMusicPath, int* oIsValid, char* oDisplayName);
void getCharacterSelectNamePath(const char* tName, char* oDst);

int setCharacterRandomAndReturnIfSuccessful(MugenDefScript* tScript, int i);
void setStageRandom(MugenDefScript* tScript);
// [3DS] For tests (the simulator has no touch screen): a tap at (x, y) on
// the bottom screen, in the game's coordinates, in the next frame.
void tapN3DSSelectScreenForTesting(float x, float y);
