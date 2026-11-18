#include "smoothtext3ds.h"

#include <math.h>

#include <prism/texture.h>
#include <prism/file.h>

#include "panelfont.h"

#if defined(__3DS__) || defined(N3DS_TEXTURE_SIM)

typedef struct {
	TextureData mTexture;
	int mLineHeight;
} SmoothFont;

static struct {
	SmoothFont mFonts[2];
	int mUsers;
} gSmoothText;

static void loadSmoothFont(SmoothFont* oFont, const uint8_t* tAlpha, int tWidth, int tHeight, int tLineHeight) {
	std::vector<uint8_t> pixels(size_t(tWidth) * tHeight * 4);
	for (int i = 0; i < tWidth * tHeight; i++) {
		pixels[i * 4 + 0] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 255;
		pixels[i * 4 + 3] = tAlpha[i];
	}
	oFont->mTexture = loadTextureFromARGB32Buffer(makeBuffer(pixels.data(), uint32_t(pixels.size())), tWidth, tHeight);
	oFont->mLineHeight = tLineHeight;
}

void acquireSmoothTextFonts() {
	if (gSmoothText.mUsers++) return;
	loadSmoothFont(&gSmoothText.mFonts[SMOOTH_TEXT_LARGE], gPanelFontLargeAlpha, gPanelFontLargeWidth, gPanelFontLargeHeight, gPanelFontLargeLineHeight);
	loadSmoothFont(&gSmoothText.mFonts[SMOOTH_TEXT_SMALL], gPanelFontSmallAlpha, gPanelFontSmallWidth, gPanelFontSmallHeight, gPanelFontSmallLineHeight);
}

void releaseSmoothTextFonts() {
	if (gSmoothText.mUsers <= 0 || --gSmoothText.mUsers) return;
	for (auto& font : gSmoothText.mFonts) unloadTexture(font.mTexture);
}

// From panelfont.h's tables (known before the textures are made): ASCII,
// then Latin-1 (accents, ñ, ¿, ¡); anything else a '?'.
static const PanelFontGlyph& getGlyph(int tFont, unsigned tCode) {
	int index = '?' - 32;
	if (tCode >= 32 && tCode <= 126) index = int(tCode) - 32;
	else if (tCode >= 160 && tCode <= 255) index = 95 + int(tCode) - 160;
	return (tFont == SMOOTH_TEXT_LARGE ? gPanelFontLargeGlyphs : gPanelFontSmallGlyphs)[index];
}

// The text's characters: UTF-8 (the source's strings) as code points;
// a byte that is not UTF-8 (a character's cp1252 name) as itself.
static std::vector<unsigned> decodeText(const std::string& tText) {
	std::vector<unsigned> codes;
	for (size_t i = 0; i < tText.size(); i++) {
		const unsigned char c = tText[i];
		if (c >= 0xC0 && c < 0xE0 && i + 1 < tText.size() && (tText[i + 1] & 0xC0) == 0x80) {
			codes.push_back(((c & 0x1F) << 6) | (tText[i + 1] & 0x3F));
			i++;
		}
		else if (c >= 0xE0 && c < 0xF0 && i + 2 < tText.size()) {
			codes.push_back('?');
			i += 2;
		}
		else codes.push_back(c);
	}
	return codes;
}

float addSmoothText(SmoothText* oText, int tFont, const std::string& tText, float tX, float tTop, float tZ, int tScreen, const Vector3D& tColor, float tRight) {
	SmoothFont& font = gSmoothText.mFonts[tFont];
	float x = tX;
	for (unsigned c : decodeText(tText)) {
		const PanelFontGlyph& glyph = getGlyph(tFont, c);
		if (x + glyph.xOffset + glyph.w > tRight) break;
		if (c != ' ') {
			// Drawn at its own size: prism's setAnimationSize scales against
			// the whole texture, not the letter's part of it.
			auto element = playAnimationLoop(Vector3D(floorf(x + glyph.xOffset), floorf(tTop), tZ), &font.mTexture, createOneFrameAnimation(), makeRectangle(glyph.x, glyph.y, glyph.w - 1, font.mLineHeight - 1));
			setAnimationScreen(element, tScreen);
			setAnimationColor(element, float(tColor.x), float(tColor.y), float(tColor.z));
			oText->push_back(element);
		}
		x += glyph.advance;
	}
	return x;
}

void addSmoothTextCentered(SmoothText* oText, int tFont, const std::string& tText, float tCenterX, float tTop, float tZ, int tScreen, const Vector3D& tColor) {
	addSmoothText(oText, tFont, tText, tCenterX - getSmoothTextWidth(tFont, tText) / 2, tTop, tZ, tScreen, tColor);
}

void removeSmoothText(SmoothText* tText) {
	for (auto element : *tText) removeHandledAnimation(element);
	tText->clear();
}

void setSmoothTextColor(SmoothText* tText, const Vector3D& tColor) {
	for (auto element : *tText) setAnimationColor(element, float(tColor.x), float(tColor.y), float(tColor.z));
}

float getSmoothTextWidth(int tFont, const std::string& tText) {
	float width = 0;
	for (unsigned c : decodeText(tText)) width += getGlyph(tFont, c).advance;
	return width;
}

int getSmoothTextLineHeight(int tFont) {
	return tFont == SMOOTH_TEXT_LARGE ? gPanelFontLargeLineHeight : gPanelFontSmallLineHeight;
}

#endif
