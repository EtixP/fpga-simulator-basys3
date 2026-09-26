// SDL2 + Metal + Dear ImGui plumbing for the virtual board window, following
// ImGui's canonical examples/example_sdl2_metal/main.mm. Engine construction
// happens in the per-demo mains; this file sees only BoardModel.

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include "gui/GuiApp.h"

#include "board/BoardModel.h"
#include "board/Vga.h"
#include "gui/BoardWindow.h"
#include "script/ScriptRunner.h"

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

namespace vb {

namespace {

// Write a BGRA8 texture readback as a 24-bit BMP (bottom-up, 4-byte row
// padding). The 1.6 VGA frame dumps build on this readback path.
bool writeBmp(const std::string& path, const uint8_t* bgra, uint32_t w, uint32_t h) {
  const uint32_t rowBytes = ((w * 3 + 3) / 4) * 4;
  const uint32_t imageSize = rowBytes * h;
  const uint32_t fileSize = 54 + imageSize;
  uint8_t header[54] = {'B', 'M'};
  auto put32 = [&](int off, uint32_t v) {
    header[off] = v & 0xFF; header[off + 1] = (v >> 8) & 0xFF;
    header[off + 2] = (v >> 16) & 0xFF; header[off + 3] = (v >> 24) & 0xFF;
  };
  put32(2, fileSize); put32(10, 54); put32(14, 40);
  put32(18, w); put32(22, h);
  header[26] = 1; header[28] = 24;
  put32(34, imageSize);
  std::ofstream out(path, std::ios::binary);
  if (!out.good()) return false;
  out.write(reinterpret_cast<char*>(header), 54);
  std::string row(rowBytes, '\0');
  for (int32_t y = static_cast<int32_t>(h) - 1; y >= 0; --y) {
    const uint8_t* src = bgra + static_cast<size_t>(y) * w * 4;
    for (uint32_t x = 0; x < w; ++x) {
      row[x * 3 + 0] = static_cast<char>(src[x * 4 + 0]);
      row[x * 3 + 1] = static_cast<char>(src[x * 4 + 1]);
      row[x * 3 + 2] = static_cast<char>(src[x * 4 + 2]);
    }
    out.write(row.data(), rowBytes);
  }
  return out.good();
}

}  // namespace

int runBoardGui(BoardModel& board, const RunOptions& opts) {
  ScriptCursor script;
  initializeScriptedRun(board, opts, script);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    std::fprintf(stderr, "error: SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
  // Taller when the design drives VGA so the frame panel + honest speed banner
  // fit without clipping.
  const int winH = board.hasVga() ? 730 : 490;
  SDL_Window* window = SDL_CreateWindow(opts.windowTitle.c_str(), 100, 100, 820, winH,
                                        SDL_WINDOW_ALLOW_HIGHDPI);
  if (!window) {
    std::fprintf(stderr, "error: SDL_CreateWindow: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Renderer* renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    std::fprintf(stderr, "error: SDL_CreateRenderer: %s\n", SDL_GetError());
    return 1;
  }
  CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_RenderGetMetalLayer(renderer);
  layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  if (!opts.screenshotPath.empty()) layer.framebufferOnly = NO;  // allow readback

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  // No imgui.ini: fixed-layout window; headless runs must not litter cwd.
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui_ImplMetal_Init(layer.device);
  ImGui_ImplSDL2_InitForMetal(window);

  id<MTLCommandQueue> commandQueue = [layer.device newCommandQueue];
  MTLRenderPassDescriptor* renderPass = [MTLRenderPassDescriptor new];

  // Persistent VGA texture (created lazily on the first completed frame; ARC
  // strong reference held for the whole loop). Uploaded ONLY when the frame
  // index advances (frame-accurate, R1).
  id<MTLTexture> vgaTex = nil;
  uint64_t lastUploadedVgaFrame = ~0ull;
  std::vector<uint8_t> bgra;  // reused RGB888 -> BGRA staging

  bool done = false;
  bool artifactFailure = false;  // failed --screenshot/--log writes = nonzero exit
  long frame = 0;
  auto lastFrameTime = std::chrono::steady_clock::now();
  while (!done) {
    @autoreleasepool {
      // --frames 0 is a pure parse/launch smoke: exit before ticking.
      if (opts.maxFrames >= 0 && frame >= opts.maxFrames) break;

      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT) done = true;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)
          done = true;
      }

      int width, height;
      SDL_GetRendererOutputSize(renderer, &width, &height);
      layer.drawableSize = CGSizeMake(width, height);
      id<CAMetalDrawable> drawable = [layer nextDrawable];
      // Tick only when this iteration will actually render: one counted
      // frame == exactly cyclesPerFrame of virtual time even if the drawable
      // pool stalls. Startup contributes a separate 16 cycles to positive runs.
      if (!drawable) continue;

      advanceScripted(board, opts, script, opts.cyclesPerFrame);

      // Honest per-frame speed readout: raw measured dt, never smoothed.
      const auto nowT = std::chrono::steady_clock::now();
      const double dt = std::chrono::duration<double>(nowT - lastFrameTime).count();
      lastFrameTime = nowT;
      VgaView vgaView;
      if (board.hasVga() && board.vgaCompletedFrames() > 0) {
        const uint64_t idx = board.vgaCompletedFrames() - 1;
        if (idx != lastUploadedVgaFrame) {  // upload only on frame advance
          if (!vgaTex) {
            MTLTextureDescriptor* d = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                             width:VgaFrameAssembler::kWidth
                                            height:VgaFrameAssembler::kHeight
                                         mipmapped:NO];
            d.usage = MTLTextureUsageShaderRead;
            vgaTex = [layer.device newTextureWithDescriptor:d];
          }
          const auto& rgb = board.vgaFramebuffer();
          bgra.resize(rgb.size() / 3 * 4);
          for (size_t p = 0, q = 0; p + 2 < rgb.size(); p += 3, q += 4) {
            bgra[q + 0] = rgb[p + 2];  // B
            bgra[q + 1] = rgb[p + 1];  // G
            bgra[q + 2] = rgb[p + 0];  // R
            bgra[q + 3] = 0xFF;        // A
          }
          [vgaTex replaceRegion:MTLRegionMake2D(0, 0, VgaFrameAssembler::kWidth,
                                                VgaFrameAssembler::kHeight)
                    mipmapLevel:0
                      withBytes:bgra.data()
                    bytesPerRow:VgaFrameAssembler::kWidth * 4];
          lastUploadedVgaFrame = idx;
        }
        vgaView.textureId = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
            (__bridge void*)vgaTex));
        vgaView.width = VgaFrameAssembler::kWidth;
        vgaView.height = VgaFrameAssembler::kHeight;
        vgaView.frameIndex = idx;
      }
      if (dt > 0.0) {
        vgaView.simMHz = opts.cyclesPerFrame / dt / 1e6;
        vgaView.realtimeMultiplier = (opts.cyclesPerFrame / dt) / 100e6;
        vgaView.fps = 1.0 / dt;
      }

      id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
      renderPass.colorAttachments[0].clearColor = MTLClearColorMake(0.10, 0.11, 0.12, 1.0);
      renderPass.colorAttachments[0].texture = drawable.texture;
      renderPass.colorAttachments[0].loadAction = MTLLoadActionClear;
      renderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
      id<MTLRenderCommandEncoder> encoder =
          [commandBuffer renderCommandEncoderWithDescriptor:renderPass];

      ImGui_ImplMetal_NewFrame(renderPass);
      ImGui_ImplSDL2_NewFrame();
      ImGui::NewFrame();
      drawBoardWindow(board, vgaView);
      ImGui::Render();
      ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, encoder);

      [encoder endEncoding];
      [commandBuffer presentDrawable:drawable];
      [commandBuffer commit];

      if (opts.maxFrames >= 0 && ++frame >= opts.maxFrames) {
        done = true;
        if (!opts.screenshotPath.empty()) {
          [commandBuffer waitUntilCompleted];
          id<MTLTexture> tex = drawable.texture;
          const uint32_t w = static_cast<uint32_t>(tex.width);
          const uint32_t h = static_cast<uint32_t>(tex.height);
          std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
          [tex getBytes:pixels.data()
              bytesPerRow:static_cast<NSUInteger>(w) * 4
               fromRegion:MTLRegionMake2D(0, 0, w, h)
              mipmapLevel:0];
          if (!writeBmp(opts.screenshotPath, pixels.data(), w, h)) {
            std::fprintf(stderr, "error: could not write screenshot '%s'\n",
                         opts.screenshotPath.c_str());
            artifactFailure = true;
          }
        }
      }
    }
  }

  if (script.event < opts.stimulus.size())
    std::fprintf(stderr, "warning: %zu scripted --at event(s) beyond the run horizon "
                         "were never applied\n",
                 opts.stimulus.size() - script.event);
  if (script.send < opts.sends.size())
    std::fprintf(stderr, "warning: %zu scripted --send event(s) beyond the run "
                         "horizon were never applied\n",
                 opts.sends.size() - script.send);

  if (!opts.logPath.empty()) {
    std::ofstream out(opts.logPath, std::ios::binary);
    for (const auto& line : board.structuredLog()) out << line << '\n';
    out.flush();
    if (!out.good()) {
      std::fprintf(stderr, "error: could not write log '%s'\n", opts.logPath.c_str());
      artifactFailure = true;
    }
  }

  ImGui_ImplMetal_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return artifactFailure ? 1 : 0;
}

}  // namespace vb
