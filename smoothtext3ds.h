#pragma once

// [3DS] Smooth letters for the port's own menus (the touch panels, the
// pause menu, the title's bottom screen): DejaVu Sans Bold anti-aliased at
// the sizes drawn (panelfont.h), each letter a handled animation. MUGEN's
// debug font scaled up read badly.

#include <string>
#include <vector>

#include <prism/animation.h>
#include <prism/geometry.h>

using namespace prism;

#define SMOOTH_TEXT_LARGE 0   // 16 pixels
#define SMOOTH_TEXT_SMALL 1   // 11 pixels

typedef std::vector<AnimationHandlerElement*> SmoothText;

// Each user acquires the letters' textures while it shows text, and lets
// them go after.
void acquireSmoothTextFonts();
void releaseSmoothTextFonts();

// Its letters from (tX, tTop) on the screen tScreen (0 top, 1 bottom), cut
// at tRight; where it ended.
float addSmoothText(SmoothText* oText, int tFont, const std::string& tText, float tX, float tTop, float tZ, int tScreen, const Vector3D& tColor, float tRight = 100000.0f);
// Centered on tCenterX.
void addSmoothTextCentered(SmoothText* oText, int tFont, const std::string& tText, float tCenterX, float tTop, float tZ, int tScreen, const Vector3D& tColor);
void removeSmoothText(SmoothText* tText);
void setSmoothTextColor(SmoothText* tText, const Vector3D& tColor);
float getSmoothTextWidth(int tFont, const std::string& tText);
int getSmoothTextLineHeight(int tFont);
