// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/EFBInterface.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <fmt/format.h>

#include "Common/MsgHandler.h"

#include "Core/ConfigManager.h"
#include "Core/System.h"

#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/PixelEngine.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"

std::unique_ptr<EFBInterfaceBase> g_efb_interface;

EFBInterfaceBase::~EFBInterfaceBase() = default;

void HardwareEFBInterface::ReinterpretPixelData(EFBReinterpretType convtype)
{
  g_framebuffer_manager->ReinterpretPixelData(convtype);
}

u32 HardwareEFBInterface::PeekColorInternal(u16 x, u16 y)
{
  u32 color = g_framebuffer_manager->PeekEFBColor(x, y);

  // a little-endian value is expected to be returned
  color = ((color & 0xFF00FF00) | ((color >> 16) & 0xFF) | ((color << 16) & 0xFF0000));

  if (bpmem.zcontrol.pixel_format == PixelFormat::RGBA6_Z24)
  {
    color = RGBA8ToRGBA6ToRGBA8(color);
  }
  else if (bpmem.zcontrol.pixel_format == PixelFormat::RGB565_Z16)
  {
    color = RGBA8ToRGB565ToRGBA8(color);
  }
  if (bpmem.zcontrol.pixel_format != PixelFormat::RGBA6_Z24)
  {
    color |= 0xFF000000;
  }

  return color;
}

u32 EFBInterfaceBase::PeekColor(u16 x, u16 y)
{
  u32 color = PeekColorInternal(x, y);

  // check what to do with the alpha channel (GX_PokeAlphaRead)
  PixelEngine::AlphaReadMode alpha_read_mode =
      Core::System::GetInstance().GetPixelEngine().GetAlphaReadMode();

  if (alpha_read_mode == PixelEngine::AlphaReadMode::ReadNone)
  {
    return color;
  }
  else if (alpha_read_mode == PixelEngine::AlphaReadMode::ReadFF)
  {
    return color | 0xFF000000;
  }
  else
  {
    if (alpha_read_mode != PixelEngine::AlphaReadMode::Read00)
    {
      PanicAlertFmt("Invalid PE alpha read mode: {}", static_cast<u16>(alpha_read_mode));
    }
    return color & 0x00FFFFFF;
  }
}

u32 HardwareEFBInterface::PeekDepth(u16 x, u16 y)
{
  // Depth buffer is inverted for improved precision near far plane
  float depth = g_framebuffer_manager->PeekEFBDepth(x, y);
  if (!g_backend_info.bSupportsReversedDepthRange)
    depth = 1.0f - depth;

  // Convert to 24bit depth
  u32 z24depth = std::clamp<u32>(static_cast<u32>(depth * 16777216.0f), 0, 0xFFFFFF);

  if (bpmem.zcontrol.pixel_format == PixelFormat::RGB565_Z16)
  {
    // When in RGB565_Z16 mode, EFB Z peeks return a 16bit value, which is presumably a
    // resolved sample from the MSAA buffer.
    // Dolphin doesn't currently emulate the 3 sample MSAA mode (and potentially never will)
    // it just transparently upgrades the framebuffer to 24bit depth and color and whatever
    // level of MSAA and higher Internal Resolution the user has configured.

    // This is mostly transparent, unless the game does an EFB read.
    // But we can simply convert the 24bit depth on the fly to the 16bit depth the game expects.

    return CompressZ16(z24depth, bpmem.zcontrol.zformat);
  }

  return z24depth;
}

void HardwareEFBInterface::PokeColor(u16 x, u16 y, u32 poke_data)
{
  // Convert to expected format (BGRA->RGBA)
  // TODO: Check alpha, depending on mode?
  const u32 color =
      ((poke_data & 0xFF00FF00) | ((poke_data >> 16) & 0xFF) | ((poke_data << 16) & 0xFF0000));

  g_framebuffer_manager->PokeEFBColor(x, y, color);
}

void HardwareEFBInterface::PokeDepth(u16 x, u16 y, u32 poke_data)
{
  // Convert to floating-point depth.
  float depth = float(poke_data & 0xFFFFFF) / 16777216.0f;
  if (!g_backend_info.bSupportsReversedDepthRange)
    depth = 1.0f - depth;

  g_framebuffer_manager->PokeEFBDepth(x, y, depth);
}
