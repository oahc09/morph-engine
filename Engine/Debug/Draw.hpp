﻿#pragma once
#include "Engine/Core/common.hpp"
#include "Engine/Core/Rgba.hpp"
#include "Engine/Math/Primitives/vec3.hpp"
#include "Engine/Math/Primitives/AABB2.hpp"
#include "Engine/Renderer/Geometry/Mesh.hpp"

class Renderer;
class Texture;
class Clock;
class Font;
class Camera;
namespace Debug {
  enum eDebugDrawDepthMode {
    DEBUG_DEPTH_DEFAULT,
    DEBUG_DEPTH_ENABLE,
    DEBUG_DEPTH_DISABLE,
    DEBUG_DEPTH_XRAY,
  };

  class DrawHandle {
    friend struct DebugDrawMeta;
    uint id = NULL;
    inline static uint _id = 0;
    
  public:
    bool terminate() const;
    inline static uint next() {
      return ++_id;
    }
  };

  inline constexpr float INF = INFINITY;
  void setRenderer(Renderer* renderer);
  void setCamera(Camera* camera);
  void setClock(const Clock* clock = nullptr);
  void setDuration(float time);
  void setDepth(eDebugDrawDepthMode depthMode);
  void setDecayColor(const Rgba& from, const Rgba& to);
  void setDecayColor(const Rgba& color);
  void clear();
  void toggleDebugRender(bool isEnabled);
  void toggleDebugRender();
  void init();
  void drawNow();
  // draw APIs: <Geometry Info>, float duration, <Color Info>, Clock* clockOverride
  // Complete version
  // Complete version without time(using current default time)
  // Other versions

  // -------------------- 2D ---------------------------

  const DrawHandle* drawQuad2(const vec2& a, const vec2& b, const vec2& c, const vec2& d, float duration = INF,
                 const Rgba& cl = Rgba::white, const Clock* clockOverride = nullptr);

  const DrawHandle* drawLine2(const vec2& a, const vec2& b, float duration,
                 const Rgba& cla = Rgba::white, const Rgba& clb = Rgba::white, const Clock* clockOverride = nullptr);

  const DrawHandle* drawText2(std::string_view text, float size, const vec2& bottomLeft, 
                              float duration = INF, const Rgba& cl = Rgba::white, const Font* font = nullptr);

  // -------------------- 3D ---------------------------

  const DrawHandle* drawPoint(const vec3& position, float duration, const Rgba& color = Rgba::white, const Clock* clockOverride = nullptr);
  const DrawHandle* drawPoint(const vec3& position, const Rgba& color = Rgba::white, float duration = INF, const Clock* clockOverride = nullptr);

  const DrawHandle* drawLine(const vec3& from, const vec3& to, float thickness = 1.f, float duration = INF,
                const Rgba& colorStart = Rgba::white, const Rgba& colorEnd = Rgba::white, const Clock* clockOverride = nullptr);

  const DrawHandle* drawTri(const vec3& a, const vec3& b, const vec3& c, float duration = INF,
               const Rgba& cl = Rgba::white, const Clock* clockOverride = nullptr);

  const DrawHandle* drawQuad(const vec3& a, const vec3& b, const vec3& c, const vec3& d, float duration = INF,
                const Rgba& cl = Rgba::white, const Clock* clockOverride = nullptr);
  const DrawHandle* drawQuad(const vec3& center, const vec3& normal, float width, float height, float duration = INF,
                const Rgba& cl = Rgba::white, const Clock* clockOverride = nullptr);
  const DrawHandle* drawQuad(const vec3& center, const vec3& normal, float size, float duration = INF,
                const Rgba& cl = Rgba::white, const Clock* clockOverride = nullptr);

  const DrawHandle* drawCone(const vec3& origin, const vec3& direction, float length, float angle, uint slides,
                             float duration = INF, bool framed = true, const Rgba& color = Rgba::white, const Clock* clockoverride = nullptr);

  const DrawHandle* drawSphere(const vec3& center, float size, uint levelX = 10u, uint levelY = 10u, float duration = INF,
                  const Rgba& color = Rgba::white, const Clock* clockoverride = nullptr);
  const DrawHandle* drawSphere(const vec3& center, uint levelX = 10u, uint levelY = 10u, float size = 1.f,
                  const Rgba& color = Rgba::white, const Clock* clockoverride = nullptr);

  const DrawHandle* drawCube(const vec3& center, const vec3& dimension, bool framed = true, float duration = INF,
                const Rgba& cl = Rgba::white, const Clock* clockOverride = nullptr);
  const DrawHandle* drawCube(const vec3& center, float size, bool framed = true, float duration = INF,
                const Rgba& cl = Rgba::white, const Clock* clockOverride = nullptr);

  const DrawHandle* drawBasis(const vec3& position = vec3::zero, const vec3& i = vec3::right, const vec3& j = vec3::up, const vec3& k = vec3::forward, float duration = INF, Clock* clockOverride = nullptr);

  const DrawHandle* drawGrid(const vec3& center = vec3::zero, const vec3& right =  vec3::right, const vec3 forward = vec3::forward, 
                             float unitSize = 1.f, float limit = 10.f, float duration = INF,
                             const Rgba& cl = Rgba::white, const Clock* clockOverride = nullptr);

  const DrawHandle* drawText(std::string_view text, float size, const vec3& bottomLeft, 
                             const vec3& direction = vec3::right, const vec3& up = vec3::up, const Font* font = nullptr, float duration = INF, 
                             const Rgba& cl = Rgba::white, const Clock* clockOverride = nullptr);
  const DrawHandle* drawText(std::string_view text, float size, const vec3& bottomLeft, float duration, 
                             const vec3& direction = vec3::right, const vec3& up = vec3::up, const Font* font = nullptr, 
                             const Rgba& cl = Rgba::white, const Clock* clockOverride = nullptr);

  // Tag always facing to the camera
  const DrawHandle* drawTag(std::string_view text, const vec3& bottomLeft, float duration = INF,
               const Rgba& cl = Rgba::white, const Clock* clockOverride = nullptr);

  // Glyph always facing to the camera
  const DrawHandle* drawGlyph(const vec3& bottomLeft, const vec2& size, const aabb2& uvs, Texture* tex, float duration = INF,
                 const Rgba& tint = Rgba::white, const Clock* clockOverride = nullptr);

  const DrawHandle* log(std::string_view info, float duration = INF, const Rgba& color = Rgba::white);

  const DrawHandle* drawMesh(const Mesh& mesh, float duration = INF, const Rgba& tint = Rgba::white, const Clock* clockOverride = nullptr);
}