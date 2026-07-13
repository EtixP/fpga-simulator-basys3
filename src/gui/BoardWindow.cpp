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

void drawBoardWindow(BoardModel& board) {
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
  ImGui::SetNextWindowSize(ImVec2(kLeftPad * 2 + 16 * kCell + 20, 220), ImGuiCond_Once);
  ImGui::Begin("Basys 3", nullptr,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
  drawLedRow(board);
  ImGui::Spacing();
  drawSwitchRow(board);
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Text("cycle %llu", static_cast<unsigned long long>(board.now()));
  ImGui::End();
}

}  // namespace vb
