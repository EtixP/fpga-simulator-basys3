#include "gui/BoardWindow.h"

#include "board/BoardModel.h"
#include "imgui.h"

#include <cstdio>

namespace vb {

namespace {

// Board order: index 15 on the left, 0 on the right (matches the hardware).
constexpr float kCell = 46.0f;   // horizontal pitch per LED/switch position
constexpr float kLeftPad = 8.0f;

void drawLedRow(const BoardModel& board) {
  ImGui::TextDisabled("LED");
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 base = ImGui::GetCursorScreenPos();
  for (uint32_t col = 0; col < BoardModel::kLedCount; ++col) {
    const uint32_t i = BoardModel::kLedCount - 1 - col;
    const ImVec2 center(base.x + kLeftPad + col * kCell + kCell * 0.5f, base.y + 14.0f);
    const bool has = board.hasLed(i);
    const bool on = has && board.ledState(i);
    const ImU32 fill = !has ? IM_COL32(38, 38, 38, 255)
                       : on ? IM_COL32(105, 230, 60, 255)
                            : IM_COL32(70, 82, 66, 255);
    if (on) dl->AddCircleFilled(center, 13.0f, IM_COL32(105, 230, 60, 60));  // glow
    dl->AddCircleFilled(center, 8.0f, fill);
    dl->AddCircle(center, 8.5f, IM_COL32(20, 20, 20, 255), 0, 1.5f);
    char label[4];
    std::snprintf(label, sizeof label, "%u", i);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(center.x - ts.x * 0.5f, base.y + 30.0f),
                IM_COL32(140, 140, 140, 255), label);
  }
  ImGui::Dummy(ImVec2(kLeftPad + BoardModel::kLedCount * kCell, 48.0f));
}

void drawSwitchRow(BoardModel& board) {
  ImGui::TextDisabled("SW");
  const float rowX = ImGui::GetCursorPosX();
  for (uint32_t col = 0; col < BoardModel::kSwitchCount; ++col) {
    const uint32_t i = BoardModel::kSwitchCount - 1 - col;
    if (col > 0) ImGui::SameLine();
    // Center each ~22 px checkbox inside its cell so switches line up with
    // the LEDs above.
    ImGui::SetCursorPosX(rowX + kLeftPad + col * kCell + (kCell - 22.0f) * 0.5f);
    ImGui::BeginGroup();
    ImGui::PushID(static_cast<int>(i));
    const bool has = board.hasSwitch(i);
    bool v = board.switchState(i);
    ImGui::BeginDisabled(!has);
    if (ImGui::Checkbox("##sw", &v)) board.setSwitch(i, v);
    ImGui::EndDisabled();
    char label[4];
    std::snprintf(label, sizeof label, "%u", i);
    const float w = ImGui::CalcTextSize(label).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (22.0f - w) * 0.5f);
    ImGui::TextDisabled("%s", label);
    ImGui::PopID();
    ImGui::EndGroup();
  }
}

}  // namespace

namespace {

// One seven-seg digit at origin (top-left), cell 56x84. Segment bit order:
// bit0=A(top) .. bit6=G(middle), 1 = lit.
void drawDigit(ImDrawList* dl, ImVec2 o, uint8_t segs, bool dpLit) {
  const ImU32 lit = IM_COL32(255, 128, 24, 255);
  const ImU32 unlit = IM_COL32(52, 46, 40, 255);
  const float t = 7.0f;   // segment thickness
  const float l = 30.0f;  // horizontal segment length
  const float v = 28.0f;  // vertical segment length
  const float x0 = o.x + 8.0f, y0 = o.y + 4.0f;
  auto seg = [&](int bit) { return (segs >> bit) & 1 ? lit : unlit; };
  auto hbar = [&](float x, float y, ImU32 c) {
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + l, y + t), c, 2.0f);
  };
  auto vbar = [&](float x, float y, ImU32 c) {
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + t, y + v), c, 2.0f);
  };
  hbar(x0, y0, seg(0));                              // A
  vbar(x0 + l - t * 0.5f, y0 + t * 0.5f, seg(1));    // B
  vbar(x0 + l - t * 0.5f, y0 + t + v, seg(2));       // C
  hbar(x0, y0 + 2.0f * (t * 0.5f + v), seg(3));      // D
  vbar(x0 - t * 0.5f, y0 + t + v, seg(4));           // E
  vbar(x0 - t * 0.5f, y0 + t * 0.5f, seg(5));        // F
  hbar(x0, y0 + t * 0.5f + v, seg(6));               // G
  dl->AddCircleFilled(ImVec2(x0 + l + 9.0f, y0 + 2.0f * (t * 0.5f + v) + t - 2.0f),
                      3.5f, dpLit ? lit : unlit);
}

void drawSevenSegPanel(const BoardModel& board) {
  ImGui::TextDisabled("SSEG");
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 base = ImGui::GetCursorScreenPos();
  // Digit 3 leftmost, digit 0 rightmost, matching the hardware.
  for (uint32_t col = 0; col < BoardModel::kDigitCount; ++col) {
    const uint32_t i = BoardModel::kDigitCount - 1 - col;
    drawDigit(dl, ImVec2(base.x + kLeftPad + 40.0f + col * 62.0f, base.y),
              board.digitSegments(i), board.digitDp(i));
  }
  ImGui::Dummy(ImVec2(kLeftPad + 4 * 62.0f + 60.0f, 84.0f));
}

// The button cross sits to the right of the display, like the hardware.
void drawButtonCross(BoardModel& board, ImVec2 origin) {
  const ImVec2 bsize(42.0f, 34.0f);
  struct Spot { Button b; const char* label; float dx, dy; };
  const Spot spots[] = {
      {Button::U, "U", 1.0f, 0.0f}, {Button::L, "L", 0.0f, 1.0f},
      {Button::C, "C", 1.0f, 1.0f}, {Button::R, "R", 2.0f, 1.0f},
      {Button::D, "D", 1.0f, 2.0f},
  };
  for (const Spot& s : spots) {
    ImGui::SetCursorPos(
        ImVec2(origin.x + s.dx * (bsize.x + 4.0f), origin.y + s.dy * (bsize.y + 4.0f)));
    ImGui::PushID(s.label);
    const bool has = board.hasButton(s.b);
    ImGui::BeginDisabled(!has);
    ImGui::Button(s.label, bsize);
    // Momentary: pressed exactly while the mouse holds the widget — a hold
    // spans as many frames as the user keeps it down (debounce needs >=10 ms
    // of stable input, i.e. many frames).
    if (ImGui::IsItemActivated()) board.setButton(s.b, true);
    if (ImGui::IsItemDeactivated()) board.setButton(s.b, false);
    ImGui::EndDisabled();
    ImGui::PopID();
  }
}

}  // namespace

void drawBoardWindow(BoardModel& board) {
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
  ImGui::SetNextWindowSize(ImVec2(kLeftPad * 2 + 16 * kCell + 20, 430), ImGuiCond_Once);
  ImGui::Begin("Basys 3", nullptr,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
  const ImVec2 topLeft = ImGui::GetCursorPos();
  drawSevenSegPanel(board);
  drawButtonCross(board, ImVec2(topLeft.x + kLeftPad + 4 * 62.0f + 130.0f, topLeft.y + 8.0f));
  ImGui::SetCursorPos(ImVec2(topLeft.x, topLeft.y + 112.0f));
  drawLedRow(board);
  ImGui::Spacing();
  drawSwitchRow(board);
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Text("cycle %llu", static_cast<unsigned long long>(board.now()));
  ImGui::End();
}

}  // namespace vb
