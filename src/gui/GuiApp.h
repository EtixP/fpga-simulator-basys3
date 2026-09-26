#pragma once
#include "script/RunOptions.h"

namespace vb {

class BoardModel;

// Run the SDL2/Metal/ImGui board window until closed or maxFrames rendered.
// Defined in GuiApp.mm (GUI builds only).
int runBoardGui(BoardModel& board, const RunOptions& opts);

}  // namespace vb
