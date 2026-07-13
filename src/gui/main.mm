// VirtualBasys GUI (milestone 1.3): 16 switches + 16 LEDs over the counter
// demo. SDL2 + Metal wiring follows Dear ImGui's canonical
// examples/example_sdl2_metal/main.mm.
//
// Engine construction happens HERE (the sanctioned entry point, same as
// tests); everything after setup talks to BoardModel only — the draw layer
// (BoardWindow.cpp) never sees SimEngine or Verilator.

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include "Vcounter.h"
#include "board/BoardModel.h"
#include "constraints/Xdc.h"
#include "engine/VerilatorEngine.h"
#include "gui/BoardWindow.h"

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// How much virtual time one GUI frame advances. Real-time for the 100 MHz
// board clock at 60 fps would be ~1'666'667 cycles/frame; the default is
// deliberately slower so a fast-counting design's LEDs are watchable, and
// demos may tweak it. NOTE (R1): virtual time is tied to frames by this
// FIXED count, never to wall time — "step until 16 ms elapsed" would make
// turbo and realtime behave differently. Honest pacing (realtime/turbo with
// a live speed indicator) is a phase 2 feature.
static constexpr uint64_t kCyclesPerFrame = 100'000;

static std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.good()) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Write a BGRA8 texture readback as a 24-bit BMP (bottom-up, 4-byte row
// padding). Used by --screenshot; also the readback path milestone 1.6's VGA
// frame dumps will build on.
static bool writeBmp(const std::string& path, const uint8_t* bgra, uint32_t w, uint32_t h) {
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
      row[x * 3 + 0] = static_cast<char>(src[x * 4 + 0]);  // B
      row[x * 3 + 1] = static_cast<char>(src[x * 4 + 1]);  // G
      row[x * 3 + 2] = static_cast<char>(src[x * 4 + 2]);  // R
    }
    out.write(row.data(), rowBytes);
  }
  return out.good();
}

int main(int argc, char** argv) {
  std::string xdcPath = VB_DEFAULT_XDC;
  long maxFrames = -1;  // --frames N: exit after N frames (smoke/screenshot runs)
  std::string screenshotPath;  // --screenshot P: dump the final frame as BMP
  std::string switchPreset;    // --switches 0011: pre-set SW3..SW0 (demo/smoke)
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--xdc" && i + 1 < argc) xdcPath = argv[++i];
    else if (arg == "--frames" && i + 1 < argc) maxFrames = std::strtol(argv[++i], nullptr, 10);
    else if (arg == "--screenshot" && i + 1 < argc) screenshotPath = argv[++i];
    else if (arg == "--switches" && i + 1 < argc) switchPreset = argv[++i];
  }

  // --- simulation setup (the only code here that sees the engine) ---------
  const std::string xdcText = readFile(xdcPath);
  if (xdcText.empty()) {
    std::fprintf(stderr, "error: cannot read XDC '%s'\n", xdcPath.c_str());
    return 1;
  }
  auto engine = vb::makeVerilatorEngine<Vcounter>({.topModule = "counter"});
  const vb::XdcDoc xdc = vb::parseXdc(xdcText);
  for (const auto& w : xdc.warnings) std::fprintf(stderr, "xdc %s\n", w.c_str());
  vb::PinBinding binding = vb::PinBinding::bind(xdc, *engine);
  for (const auto& d : binding.diagnostics()) std::fprintf(stderr, "bind %s\n", d.c_str());
  vb::BoardModel board(*engine, std::move(binding));
  // --switches: rightmost character is SW0, same code path as a click.
  for (size_t i = 0; i < switchPreset.size(); ++i) {
    const size_t sw = switchPreset.size() - 1 - i;
    if (switchPreset[i] == '1') board.setSwitch(static_cast<uint32_t>(sw), true);
  }

  // --- SDL + Metal + ImGui setup ------------------------------------------
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    std::fprintf(stderr, "error: SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
  SDL_Window* window =
      SDL_CreateWindow("VirtualBasys — Basys 3 (counter demo)", 100, 100, 800, 260,
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
  if (!screenshotPath.empty()) layer.framebufferOnly = NO;  // allow readback

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  // No imgui.ini: the board window is fixed-layout (NoResize + forced pos),
  // and headless smoke/screenshot runs must not litter their cwd.
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui_ImplMetal_Init(layer.device);
  ImGui_ImplSDL2_InitForMetal(window);

  id<MTLCommandQueue> commandQueue = [layer.device newCommandQueue];
  MTLRenderPassDescriptor* renderPass = [MTLRenderPassDescriptor new];

  // --- frame loop -----------------------------------------------------------
  bool done = false;
  long frame = 0;
  while (!done) {
    @autoreleasepool {
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
      // frame == exactly kCyclesPerFrame of virtual time, even if the
      // drawable pool stalls (nil), so --frames N always lands on
      // N * kCyclesPerFrame cycles — the determinism 1.6's golden frame
      // dumps will rely on.
      if (!drawable) continue;

      // Advance the simulation by a fixed cycle budget, then render the
      // resulting board state. Input (setSwitch) was applied by the widgets
      // during the PREVIOUS ImGui frame and takes effect in this tick — a
      // click is visible within one GUI frame.
      board.tick(kCyclesPerFrame);

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
      vb::drawBoardWindow(board);
      ImGui::Render();
      ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, encoder);

      [encoder endEncoding];
      [commandBuffer presentDrawable:drawable];
      [commandBuffer commit];

      if (maxFrames >= 0 && ++frame >= maxFrames) {
        done = true;
        if (!screenshotPath.empty()) {
          [commandBuffer waitUntilCompleted];
          id<MTLTexture> tex = drawable.texture;
          const uint32_t w = static_cast<uint32_t>(tex.width);
          const uint32_t h = static_cast<uint32_t>(tex.height);
          std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
          [tex getBytes:pixels.data()
              bytesPerRow:static_cast<NSUInteger>(w) * 4
               fromRegion:MTLRegionMake2D(0, 0, w, h)
              mipmapLevel:0];
          if (!writeBmp(screenshotPath, pixels.data(), w, h))
            std::fprintf(stderr, "error: could not write screenshot '%s'\n",
                         screenshotPath.c_str());
        }
      }
    }
  }

  ImGui_ImplMetal_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
