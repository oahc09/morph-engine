﻿#include "BitmapFont.hpp"

#include "Engine/Math/AABB2.hpp"

AABB2 BitmapFont::GetUVsForGlyph(int glyphUnicode) const {
  return m_spriteSheet.getTexCoordsByIndex(glyphUnicode);
}
float BitmapFont::GetStringWidth(const std::string& asciiText, float cellHeight, float aspectScale) const {
  return float(asciiText.length()) * cellHeight * aspectScale;
}
BitmapFont::BitmapFont(const std::string& fontName, const SpriteSheet& glyphSheet, float baseAspect)
  : m_fontName(fontName)
  , m_spriteSheet(glyphSheet)
  , m_baseAspect(baseAspect) {
}

BitmapFont::~BitmapFont() {
  delete &m_spriteSheet;
}
