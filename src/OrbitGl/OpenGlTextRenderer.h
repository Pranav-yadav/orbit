// Copyright (c) 2022 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ORBIT_GL_OPEN_GL_TEXT_RENDERER_H_
#define ORBIT_GL_OPEN_GL_TEXT_RENDERER_H_

#include <GteVector.h>
#include <freetype-gl/mat4.h>
#include <freetype-gl/texture-atlas.h>
#include <freetype-gl/texture-font.h>
#include <freetype-gl/vec234.h>
#include <glad/glad.h>
#include <stddef.h>
#include <stdint.h>

#include <map>
#include <unordered_map>
#include <vector>

#include "CoreMath.h"
#include "PickingManager.h"
#include "PrimitiveAssembler.h"
#include "TextRenderer.h"

namespace ftgl {
struct vertex_buffer_t;
struct texture_font_t;
}  // namespace ftgl

namespace orbit_gl {

// OpenGl implementation of the TextRenderer.
class OpenGlTextRenderer : public TextRenderer {
 public:
  explicit OpenGlTextRenderer();
  ~OpenGlTextRenderer();

  void Init() override;
  void Clear() override;

  void RenderLayer(float layer) override;
  void RenderDebug(PrimitiveAssembler* primitive_assembler) override;
  [[nodiscard]] std::vector<float> GetLayers() const override;

  void AddText(const char* text, float x, float y, float z, TextFormatting formatting) override;
  void AddText(const char* text, float x, float y, float z, TextFormatting formatting,
               Vec2* out_text_pos, Vec2* out_text_size) override;

  float AddTextTrailingCharsPrioritized(const char* text, float x, float y, float z,
                                        TextFormatting formatting,
                                        size_t trailing_chars_length) override;

  [[nodiscard]] float GetStringWidth(const char* text, uint32_t font_size) override;
  [[nodiscard]] float GetStringHeight(const char* text, uint32_t font_size) override;

 protected:
  void AddTextInternal(const char* text, ftgl::vec2* pen, const TextFormatting& formatting, float z,
                       ftgl::vec2* out_text_pos = nullptr, ftgl::vec2* out_text_size = nullptr);

  [[nodiscard]] int GetStringWidthScreenSpace(const char* text, uint32_t font_size);
  [[nodiscard]] int GetStringHeightScreenSpace(const char* text, uint32_t font_size);
  [[nodiscard]] ftgl::texture_font_t* GetFont(uint32_t size);
  [[nodiscard]] ftgl::texture_glyph_t* MaybeLoadAndGetGlyph(ftgl::texture_font_t* self,
                                                            const char* character);

  void DrawOutline(PrimitiveAssembler* primitive_assembler, ftgl::vertex_buffer_t* buffer);

 private:
  ftgl::texture_atlas_t* texture_atlas_;
  // Indicates when a change to the texture atlas occurred so that we have to reupload the
  // texture data. Only freetype-gl's texture_font_load_glyph modifies the texture atlas,
  // so we need to set this to true when and only when we call that function.
  bool texture_atlas_changed_;
  std::unordered_map<float, ftgl::vertex_buffer_t*> vertex_buffers_by_layer_;
  std::map<uint32_t, ftgl::texture_font_t*> fonts_by_size_;
  GLuint shader_;
  ftgl::mat4 model_;
  ftgl::mat4 view_;
  ftgl::mat4 projection_;
  ftgl::vec2 pen_;
  bool initialized_;
};

}  // namespace orbit_gl

#endif  // ORBIT_GL_OPEN_GL_TEXT_RENDERER_H_
