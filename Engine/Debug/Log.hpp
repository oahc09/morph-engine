﻿#pragma once
#include "Engine/Core/common.hpp"

class Rgba;
namespace Debug {
  void Log(std::string_view text, const Rgba& color, float size = 16.f, bool toView = true, bool toConsole = true, bool toMessage = true);
}