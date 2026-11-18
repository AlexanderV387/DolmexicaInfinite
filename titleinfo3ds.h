#pragma once

// [3DS] The title screen's bottom screen, for information (the menu stays
// on the top screen, the motif's own): the chosen mode's description, the
// characters and stages on the SD card (those new since the last time),
// and the last fight, Y to fight it again. Also keeps that last fight.

// The title screen: shown from its load, the description following the
// menu's choice (its select.def-like key: "menu.itemname.arcade"...).
void loadN3DSTitleInfo();
void updateN3DSTitleInfo(const char* tModeKey, const char* tModeName);
void unloadN3DSTitleInfo();
// Y on the title screen: 1 if the last fight was started again.
int startN3DSLastFightIfWanted();

// A fight's characters, colors, levels and stage, kept as the last fight.
void recordN3DSLastFight();

// The characters and stages' count on the bottom screen (OPTIONS), kept on
// the SD card.
int isN3DSTitleContentShown();
void setN3DSTitleContentShown(int tIsShown);
void loadN3DSTitleInfoSetting();
