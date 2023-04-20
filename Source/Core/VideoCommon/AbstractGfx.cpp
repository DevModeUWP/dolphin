// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/AbstractGfx.h"

#include "Common/Assert.h"

#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/RenderBase.h"
#include "VideoCommon/ShaderCache.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VideoConfig.h"

bool AbstractGfx::IsHeadless() const
{
  return true;
}

void AbstractGfx::BeginUtilityDrawing()
{
  if (g_renderer)
    g_renderer->BeginUtilityDrawing();
}

void AbstractGfx::EndUtilityDrawing()
{
  if (g_renderer)
    g_renderer->EndUtilityDrawing();
}

void AbstractGfx::SetFramebuffer(AbstractFramebuffer* framebuffer)
{
  m_current_framebuffer = framebuffer;
}

void AbstractGfx::SetAndDiscardFramebuffer(AbstractFramebuffer* framebuffer)
{
  m_current_framebuffer = framebuffer;
}

void AbstractGfx::SetAndClearFramebuffer(AbstractFramebuffer* framebuffer,
                                         const ClearColor& color_value, float depth_value)
{
  m_current_framebuffer = framebuffer;
}

void AbstractGfx::ClearRegion(const MathUtil::Rectangle<int>& rc,
                              const MathUtil::Rectangle<int>& target_rc, bool colorEnable,
                              bool alphaEnable, bool zEnable, u32 color, u32 z)
{
  g_framebuffer_manager->ClearEFB(rc, colorEnable, alphaEnable, zEnable, color, z);
}

void AbstractGfx::SetViewportAndScissor(const MathUtil::Rectangle<int>& rect, float min_depth,
                                        float max_depth)
{
  SetViewport(static_cast<float>(rect.left), static_cast<float>(rect.top),
              static_cast<float>(rect.GetWidth()), static_cast<float>(rect.GetHeight()), min_depth,
              max_depth);
  SetScissorRect(rect);
}

void AbstractGfx::ScaleTexture(AbstractFramebuffer* dst_framebuffer,
                               const MathUtil::Rectangle<int>& dst_rect,
                               const AbstractTexture* src_texture,
                               const MathUtil::Rectangle<int>& src_rect)
{
  ASSERT(dst_framebuffer->GetColorFormat() == AbstractTextureFormat::RGBA8);

  BeginUtilityDrawing();

  // The shader needs to know the source rectangle.
  const auto converted_src_rect =
      ConvertFramebufferRectangle(src_rect, src_texture->GetWidth(), src_texture->GetHeight());
  const float rcp_src_width = 1.0f / src_texture->GetWidth();
  const float rcp_src_height = 1.0f / src_texture->GetHeight();
  const std::array<float, 4> uniforms = {{converted_src_rect.left * rcp_src_width,
                                          converted_src_rect.top * rcp_src_height,
                                          converted_src_rect.GetWidth() * rcp_src_width,
                                          converted_src_rect.GetHeight() * rcp_src_height}};
  g_vertex_manager->UploadUtilityUniforms(&uniforms, sizeof(uniforms));

  // Discard if we're overwriting the whole thing.
  if (static_cast<u32>(dst_rect.GetWidth()) == dst_framebuffer->GetWidth() &&
      static_cast<u32>(dst_rect.GetHeight()) == dst_framebuffer->GetHeight())
  {
    SetAndDiscardFramebuffer(dst_framebuffer);
  }
  else
  {
    SetFramebuffer(dst_framebuffer);
  }

  SetViewportAndScissor(ConvertFramebufferRectangle(dst_rect, dst_framebuffer));
  SetPipeline(dst_framebuffer->GetLayers() > 1 ? g_shader_cache->GetRGBA8StereoCopyPipeline() :
                                                 g_shader_cache->GetRGBA8CopyPipeline());
  SetTexture(0, src_texture);
  SetSamplerState(0, RenderState::GetLinearSamplerState());
  Draw(0, 3);
  EndUtilityDrawing();
  if (dst_framebuffer->GetColorAttachment())
    dst_framebuffer->GetColorAttachment()->FinishedRendering();
}

MathUtil::Rectangle<int>
AbstractGfx::ConvertFramebufferRectangle(const MathUtil::Rectangle<int>& rect,
                                         const AbstractFramebuffer* framebuffer) const
{
  return ConvertFramebufferRectangle(rect, framebuffer->GetWidth(), framebuffer->GetHeight());
}

MathUtil::Rectangle<int>
AbstractGfx::ConvertFramebufferRectangle(const MathUtil::Rectangle<int>& rect, u32 fb_width,
                                         u32 fb_height) const
{
  MathUtil::Rectangle<int> ret = rect;
  if (g_ActiveConfig.backend_info.bUsesLowerLeftOrigin)
  {
    ret.top = fb_height - rect.bottom;
    ret.bottom = fb_height - rect.top;
  }
  return ret;
}

std::unique_ptr<VideoCommon::AsyncShaderCompiler> AbstractGfx::CreateAsyncShaderCompiler()
{
  return std::make_unique<VideoCommon::AsyncShaderCompiler>();
}

bool AbstractGfx::UseGeometryShaderForUI() const
{
  // OpenGL doesn't render to a 2-layer backbuffer like D3D/Vulkan for quad-buffered stereo,
  // instead drawing twice and the eye selected by glDrawBuffer() (see Presenter::RenderXFBToScreen)
  return g_ActiveConfig.stereo_mode == StereoMode::QuadBuffer &&
         g_ActiveConfig.backend_info.api_type != APIType::OpenGL;
}
