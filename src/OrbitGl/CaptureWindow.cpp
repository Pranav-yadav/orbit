// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "CaptureWindow.h"

#include <absl/time/time.h>
#include <glad/glad.h>
#include <imgui.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <ostream>
#include <string_view>
#include <tuple>

#include "App.h"
#include "CaptureViewElement.h"
#include "ClientData/CallstackData.h"
#include "ClientData/CaptureData.h"
#include "ClientProtos/capture_data.pb.h"
#include "CoreMath.h"
#include "DisplayFormats/DisplayFormats.h"
#include "Geometry.h"
#include "GlUtils.h"
#include "ImGuiOrbit.h"
#include "Introspection/Introspection.h"
#include "OrbitAccessibility/AccessibleInterface.h"
#include "OrbitBase/Append.h"
#include "OrbitBase/Logging.h"
#include "OrbitBase/Profiling.h"
#include "OrbitBase/ThreadConstants.h"
#include "TextRenderer.h"
#include "TimeGraphLayout.h"
#include "absl/base/casts.h"

using orbit_accessibility::AccessibleInterface;
using orbit_accessibility::AccessibleWidgetBridge;

using orbit_client_data::CaptureData;
using orbit_gl::Batcher;
using orbit_gl::CaptureViewElement;
using orbit_gl::ModifierKeys;
using orbit_gl::PickingUserData;
using orbit_gl::PrimitiveAssembler;
using orbit_gl::TextRenderer;

constexpr const char* kTimingDraw = "Draw";
constexpr const char* kTimingDrawAndUpdatePrimitives = "Draw & Update Primitives";
constexpr const char* kTimingFrame = "Complete Frame";

class AccessibleCaptureWindow : public AccessibleWidgetBridge {
 public:
  explicit AccessibleCaptureWindow(CaptureWindow* window) : window_(window) {}

  [[nodiscard]] int AccessibleChildCount() const override {
    if (window_->GetTimeGraph() == nullptr) {
      return 0;
    }
    return 1;
  }

  [[nodiscard]] const AccessibleInterface* AccessibleChild(int /*index*/) const override {
    if (window_->GetTimeGraph() == nullptr) {
      return nullptr;
    }
    return window_->GetTimeGraph()->GetOrCreateAccessibleInterface();
  }

 private:
  CaptureWindow* window_;
};

using orbit_client_protos::TimerInfo;

CaptureWindow::CaptureWindow(OrbitApp* app) : GlCanvas(), app_{app}, capture_client_app_{app} {
  draw_help_ = true;

  scoped_frame_times_[kTimingDraw] = std::make_unique<orbit_gl::SimpleTimings>(30);
  scoped_frame_times_[kTimingDrawAndUpdatePrimitives] =
      std::make_unique<orbit_gl::SimpleTimings>(30);
  scoped_frame_times_[kTimingFrame] = std::make_unique<orbit_gl::SimpleTimings>(30);
}

void CaptureWindow::PreRender() {
  GlCanvas::PreRender();

  if (ShouldAutoZoom()) {
    ZoomAll();
  }

  if (time_graph_ != nullptr) {
    int layout_loops = 0;

    // Layout changes of one element may require other elements to be updated as well,
    // so layouting needs to be done until all elements report that they do not need to
    // be updated further. As layout requests bubble up, it's enough to check this for
    // the root element (time graph) of the tree.
    // During loading or capturing, only a single layouting loop is executed as we're
    // streaming in data from a seperate thread (for performance reasons)
    const int kMaxLayoutLoops =
        (app_ != nullptr && (app_->IsCapturing() || app_->IsLoadingCapture()))
            ? 1
            : time_graph_->GetLayout().GetMaxLayoutingLoops();

    // TODO (b/229222095) Log when the max loop count is exceeded
    do {
      time_graph_->UpdateLayout();
    } while (++layout_loops < kMaxLayoutLoops && time_graph_->HasLayoutChanged());
  }
}

void CaptureWindow::ZoomAll() {
  ResetHoverTimer();
  RequestUpdatePrimitives();
  if (time_graph_ == nullptr) return;
  time_graph_->ZoomAll();
}

void CaptureWindow::MouseMoved(int x, int y, bool left, bool right, bool middle) {
  GlCanvas::MouseMoved(x, y, left, right, middle);
  if (time_graph_ == nullptr) return;

  std::ignore = time_graph_->HandleMouseEvent(
      CaptureViewElement::MouseEvent{CaptureViewElement::MouseEventType::kMouseMove,
                                     viewport_.ScreenToWorld(Vec2i(x, y)), left, right, middle});

  // Pan
  if (left && !picking_manager_.IsDragging() && !capture_client_app_->IsCapturing()) {
    Vec2i mouse_click_screen = viewport_.WorldToScreen(mouse_click_pos_world_);
    Vec2 mouse_pos_world = viewport_.ScreenToWorld({x, y});
    time_graph_->GetTrackContainer()->SetVerticalScrollingOffset(
        track_container_click_scrolling_offset_ + mouse_click_pos_world_[1] - mouse_pos_world[1]);

    int timeline_width = viewport_.WorldToScreen(Vec2(time_graph_->GetTimelineWidth(), 0))[0];
    time_graph_->PanTime(mouse_click_screen[0], x, timeline_width, ref_time_click_);

    click_was_drag_ = true;
  }

  // Update selection timestamps
  if (is_selecting_) {
    select_stop_time_ = time_graph_->GetTickFromWorld(select_stop_pos_world_[0]);
  }
}

void CaptureWindow::LeftDown(int x, int y) {
  GlCanvas::LeftDown(x, y);

  click_was_drag_ = false;

  if (time_graph_ == nullptr) return;

  int timeline_width = viewport_.WorldToScreen(Vec2(time_graph_->GetTimelineWidth(), 0))[0];
  ref_time_click_ = time_graph_->GetTime(static_cast<double>(x) / timeline_width);
  track_container_click_scrolling_offset_ =
      time_graph_->GetTrackContainer()->GetVerticalScrollingOffset();
}

void CaptureWindow::LeftUp() {
  GlCanvas::LeftUp();

  if (!click_was_drag_ && background_clicked_) {
    app_->SelectTimer(nullptr);
    app_->set_selected_thread_id(orbit_base::kAllProcessThreadsTid);
    app_->set_selected_thread_state_slice(std::nullopt);
    RequestUpdatePrimitives();
  }

  if (time_graph_ != nullptr) {
    std::ignore = time_graph_->HandleMouseEvent(
        CaptureViewElement::MouseEvent{CaptureViewElement::MouseEventType::kLeftUp,
                                       viewport_.ScreenToWorld(mouse_move_pos_screen_)});
  }
}

void CaptureWindow::HandlePickedElement(PickingMode picking_mode, PickingId picking_id, int x,
                                        int y) {
  // Early-out: This makes sure the timegraph was not deleted in between redraw and mouse click
  if (time_graph_ == nullptr) return;
  PickingType type = picking_id.type;

  orbit_gl::Batcher& batcher = GetBatcherById(picking_id.batcher_id);

  if (picking_mode == PickingMode::kClick) {
    background_clicked_ = false;
    const orbit_gl::PickingUserData* user_data = batcher.GetUserData(picking_id);
    const orbit_client_protos::TimerInfo* timer_info =
        (user_data == nullptr ? nullptr : user_data->timer_info_);
    if (timer_info != nullptr) {
      SelectTimer(timer_info);
    } else if (type == PickingType::kPickable) {
      picking_manager_.Pick(picking_id, x, y);
    } else {
      // If the background is clicked: The selection should only be cleared
      // if the user doesn't drag around the capture window.
      // This is handled later in CaptureWindow::LeftUp()
      background_clicked_ = true;
    }
  } else if (picking_mode == PickingMode::kHover) {
    std::string tooltip;

    if (type == PickingType::kPickable) {
      auto pickable = GetPickingManager().GetPickableFromId(picking_id);
      if (pickable) {
        tooltip = pickable->GetTooltip();
      }
    } else {
      const orbit_gl::PickingUserData* user_data = batcher.GetUserData(picking_id);

      if (user_data && user_data->generate_tooltip_) {
        tooltip = user_data->generate_tooltip_(picking_id);
      }
    }

    app_->SendTooltipToUi(tooltip);
  }
}

void CaptureWindow::SelectTimer(const TimerInfo* timer_info) {
  ORBIT_CHECK(time_graph_ != nullptr);
  if (timer_info == nullptr) return;

  app_->SelectTimer(timer_info);
  app_->set_selected_thread_id(timer_info->thread_id());

  if (double_clicking_) {
    // Zoom and center the text_box into the screen.
    time_graph_->Zoom(*timer_info);
  }
}

void CaptureWindow::PostRender() {
  if (picking_mode_ != PickingMode::kNone) {
    RequestUpdatePrimitives();
  }

  GlCanvas::PostRender();
}

void CaptureWindow::RightDown(int x, int y) {
  GlCanvas::RightDown(x, y);
  if (time_graph_ != nullptr) {
    select_start_time_ = time_graph_->GetTickFromWorld(select_start_pos_world_[0]);
  }
}

bool CaptureWindow::RightUp() {
  if (time_graph_ != nullptr && is_selecting_ &&
      (select_start_pos_world_[0] != select_stop_pos_world_[0]) && ControlPressed()) {
    float min_world = std::min(select_start_pos_world_[0], select_stop_pos_world_[0]);
    float max_world = std::max(select_start_pos_world_[0], select_stop_pos_world_[0]);

    double new_min =
        TicksToMicroseconds(time_graph_->GetCaptureMin(), time_graph_->GetTickFromWorld(min_world));
    double new_max =
        TicksToMicroseconds(time_graph_->GetCaptureMin(), time_graph_->GetTickFromWorld(max_world));

    time_graph_->SetMinMax(new_min, new_max);

    // Clear the selection display
    select_stop_pos_world_ = select_start_pos_world_;
  }

  if (app_->IsDevMode()) {
    auto result = selection_stats_.Generate(this, select_start_time_, select_stop_time_);
    if (result.has_error()) {
      ORBIT_ERROR("%s", result.error().message());
    }
  }

  if (time_graph_ != nullptr) {
    std::ignore = time_graph_->HandleMouseEvent(
        CaptureViewElement::MouseEvent{CaptureViewElement::MouseEventType::kRightUp,
                                       viewport_.ScreenToWorld(mouse_move_pos_screen_)});
  }

  return GlCanvas::RightUp();
}

void CaptureWindow::ZoomHorizontally(int delta, int mouse_x) {
  if (delta == 0) return;
  if (time_graph_ != nullptr) {
    double mouse_ratio = static_cast<double>(mouse_x) / time_graph_->GetTimelineWidth();
    time_graph_->ZoomTime(delta, mouse_ratio);
  }
}

void CaptureWindow::Pan(float ratio) {
  if (time_graph_ == nullptr) return;

  int timeline_width = viewport_.WorldToScreen(Vec2(time_graph_->GetTimelineWidth(), 0))[0];
  double ref_time =
      time_graph_->GetTime(static_cast<double>(mouse_move_pos_screen_[0]) / timeline_width);
  time_graph_->PanTime(
      mouse_move_pos_screen_[0],
      mouse_move_pos_screen_[0] + static_cast<int>(ratio * static_cast<float>(timeline_width)),
      timeline_width, ref_time);
  RequestUpdatePrimitives();
}

void CaptureWindow::MouseWheelMoved(int x, int y, int delta, bool ctrl) {
  GlCanvas::MouseWheelMoved(x, y, delta, ctrl);

  if (time_graph_ != nullptr) {
    ModifierKeys modifiers;
    modifiers.ctrl = ctrl;
    std::ignore = time_graph_->HandleMouseEvent(
        CaptureViewElement::MouseEvent{
            (delta > 0 ? CaptureViewElement::MouseEventType::kMouseWheelUp
                       : CaptureViewElement::MouseEventType::kMouseWheelDown),
            viewport_.ScreenToWorld(Vec2i(x, y))},
        modifiers);
  }
}

void CaptureWindow::MouseWheelMovedHorizontally(int x, int y, int delta, bool ctrl) {
  GlCanvas::MouseWheelMovedHorizontally(x, y, delta, ctrl);

  if (delta == 0) return;

  if (delta > 0) {
    Pan(0.1f);
  } else {
    Pan(-0.1f);
  }
}

void CaptureWindow::KeyPressed(unsigned int key_code, bool ctrl, bool shift, bool alt) {
  GlCanvas::KeyPressed(key_code, ctrl, shift, alt);
  const float kPanRatioPerLeftAndRightArrowKeys = 0.1f;
  const float kScrollingRatioPerUpAndDownArrowKeys = 0.05f;
  const float kScrollingRatioPerPageUpAndDown = 0.9f;

  // TODO(b/234116147): Move this part to TimeGraph and manage events similarly to HandleMouseEvent.
  switch (key_code) {
    case ' ':
      if (!shift) {
        ZoomAll();
      }
      break;
    case 'A':
      Pan(kPanRatioPerLeftAndRightArrowKeys);
      break;
    case 'D':
      Pan(-kPanRatioPerLeftAndRightArrowKeys);
      break;
    case 'W':
      ZoomHorizontally(1, mouse_move_pos_screen_[0]);
      break;
    case 'S':
      ZoomHorizontally(-1, mouse_move_pos_screen_[0]);
      break;
    case 'X':
      ToggleRecording();
      break;
    // For arrow keys, we will scroll horizontally or vertically if no timer is selected. Otherwise,
    // jump to the neighbour timer in that direction.
    case 18:  // Left
      if (time_graph_ == nullptr) return;
      if (app_ == nullptr || app_->selected_timer() == nullptr) {
        Pan(kPanRatioPerLeftAndRightArrowKeys);
      } else if (shift) {
        time_graph_->JumpToNeighborTimer(app_->selected_timer(),
                                         TimeGraph::JumpDirection::kPrevious,
                                         TimeGraph::JumpScope::kSameFunction);
      } else if (alt) {
        time_graph_->JumpToNeighborTimer(app_->selected_timer(),
                                         TimeGraph::JumpDirection::kPrevious,
                                         TimeGraph::JumpScope::kSameThreadSameFunction);
      } else {
        time_graph_->JumpToNeighborTimer(app_->selected_timer(),
                                         TimeGraph::JumpDirection::kPrevious,
                                         TimeGraph::JumpScope::kSameDepth);
      }
      break;
    case 20:  // Right
      if (time_graph_ == nullptr) return;
      if (app_ == nullptr || app_->selected_timer() == nullptr) {
        Pan(-kPanRatioPerLeftAndRightArrowKeys);
      } else if (shift) {
        time_graph_->JumpToNeighborTimer(app_->selected_timer(), TimeGraph::JumpDirection::kNext,
                                         TimeGraph::JumpScope::kSameFunction);
      } else if (alt) {
        time_graph_->JumpToNeighborTimer(app_->selected_timer(), TimeGraph::JumpDirection::kNext,
                                         TimeGraph::JumpScope::kSameThreadSameFunction);
      } else {
        time_graph_->JumpToNeighborTimer(app_->selected_timer(), TimeGraph::JumpDirection::kNext,
                                         TimeGraph::JumpScope::kSameDepth);
      }
      break;
    case 19:  // Up
      if (time_graph_ == nullptr) return;
      if (app_ == nullptr || app_->selected_timer() == nullptr) {
        time_graph_->GetTrackContainer()->IncrementVerticalScroll(
            /*ratio=*/kScrollingRatioPerUpAndDownArrowKeys);
      } else {
        time_graph_->JumpToNeighborTimer(app_->selected_timer(), TimeGraph::JumpDirection::kTop,
                                         TimeGraph::JumpScope::kSameThread);
      }
      break;
    case 21:  // Down
      if (time_graph_ == nullptr) return;
      if (app_ == nullptr || app_->selected_timer() == nullptr) {
        time_graph_->GetTrackContainer()->IncrementVerticalScroll(
            /*ratio=*/-kScrollingRatioPerUpAndDownArrowKeys);
      } else {
        time_graph_->JumpToNeighborTimer(app_->selected_timer(), TimeGraph::JumpDirection::kDown,
                                         TimeGraph::JumpScope::kSameThread);
      }
      break;
    case 22:  // Page Up
      if (time_graph_ == nullptr) return;
      time_graph_->GetTrackContainer()->IncrementVerticalScroll(
          /*ratio=*/kScrollingRatioPerPageUpAndDown);
      break;
    case 23:  // Page Down
      if (time_graph_ == nullptr) return;
      time_graph_->GetTrackContainer()->IncrementVerticalScroll(
          /*ratio=*/-kScrollingRatioPerPageUpAndDown);
      break;
    case '+':
      if (time_graph_ == nullptr) return;
      if (ctrl) {
        time_graph_->VerticalZoom(1, viewport_.ScreenToWorld(mouse_move_pos_screen_)[1]);
      }
      break;
    case '-':
      if (time_graph_ == nullptr) return;
      if (ctrl) {
        time_graph_->VerticalZoom(-1, viewport_.ScreenToWorld(mouse_move_pos_screen_)[1]);
      }
      break;
  }
}

void CaptureWindow::SetIsMouseOver(bool value) {
  GlCanvas::SetIsMouseOver(value);

  if (time_graph_ != nullptr && !value) {
    std::ignore = time_graph_->HandleMouseEvent(
        CaptureViewElement::MouseEvent{CaptureViewElement::MouseEventType::kMouseLeave});
  }
}

bool CaptureWindow::ShouldAutoZoom() const { return capture_client_app_->IsCapturing(); }

std::unique_ptr<AccessibleInterface> CaptureWindow::CreateAccessibleInterface() {
  return std::make_unique<AccessibleCaptureWindow>(this);
}

void CaptureWindow::Draw() {
  ORBIT_SCOPE("CaptureWindow::Draw");
  uint64_t start_time_ns = orbit_base::CaptureTimestampNs();
  bool time_graph_was_redrawn = time_graph_ != nullptr && time_graph_->IsRedrawNeeded();

  text_renderer_.Init();

  if (ShouldSkipRendering()) {
    return;
  }

  if (ShouldAutoZoom()) {
    ZoomAll();
  }

  if (time_graph_ != nullptr) {
    time_graph_->DrawAllElements(primitive_assembler_, GetTextRenderer(), picking_mode_);
  }

  RenderSelectionOverlay();

  if (picking_mode_ == PickingMode::kNone) {
    if (draw_help_) {
      RenderHelpUi();
    }
  }

  if (picking_mode_ == PickingMode::kNone) {
    text_renderer_.RenderDebug(&primitive_assembler_);
  }

  if (picking_mode_ == PickingMode::kNone) {
    double update_duration_in_ms = (orbit_base::CaptureTimestampNs() - start_time_ns) / 1000000.0;
    if (time_graph_was_redrawn) {
      scoped_frame_times_[kTimingDrawAndUpdatePrimitives]->PushTimeMs(update_duration_in_ms);
    } else {
      scoped_frame_times_[kTimingDraw]->PushTimeMs(update_duration_in_ms);
    }
  }

  RenderAllLayers();

  if (picking_mode_ == PickingMode::kNone) {
    if (last_frame_start_time_ != 0) {
      double frame_duration_in_ms =
          (orbit_base::CaptureTimestampNs() - last_frame_start_time_) / 1000000.0;
      scoped_frame_times_[kTimingFrame]->PushTimeMs(frame_duration_in_ms);
    }
  }

  last_frame_start_time_ = orbit_base::CaptureTimestampNs();
}

void CaptureWindow::RenderAllLayers() {
  std::vector<float> all_layers{};
  if (time_graph_ != nullptr) {
    all_layers = time_graph_->GetBatcher().GetLayers();
    orbit_base::Append(all_layers, time_graph_->GetTextRenderer()->GetLayers());
  }
  orbit_base::Append(all_layers, ui_batcher_.GetLayers());
  orbit_base::Append(all_layers, text_renderer_.GetLayers());

  // Sort and remove duplicates.
  std::sort(all_layers.begin(), all_layers.end());
  auto it = std::unique(all_layers.begin(), all_layers.end());
  all_layers.resize(std::distance(all_layers.begin(), it));
  if (all_layers.size() > GlCanvas::kMaxNumberRealZLayers) {
    ORBIT_ERROR("Too many z-layers. The current number is %d", all_layers.size());
  }

  for (float layer : all_layers) {
    if (time_graph_ != nullptr) {
      time_graph_->GetBatcher().DrawLayer(layer, picking_mode_ != PickingMode::kNone);
    }
    ui_batcher_.DrawLayer(layer, picking_mode_ != PickingMode::kNone);

    if (picking_mode_ == PickingMode::kNone) {
      text_renderer_.RenderLayer(layer);
      RenderText(layer);
    }
  }
}

void CaptureWindow::ToggleRecording() {
  capture_client_app_->ToggleCapture();
  draw_help_ = false;
#ifdef __linux__
  ZoomAll();
#endif
}

bool CaptureWindow::ShouldSkipRendering() const {
  // Don't render when loading a capture to avoid contention with the loading thread.
  return app_->IsLoadingCapture();
}

void CaptureWindow::set_draw_help(bool draw_help) {
  draw_help_ = draw_help;
  RequestRedraw();
}

void CaptureWindow::CreateTimeGraph(CaptureData* capture_data) {
  time_graph_ =
      std::make_unique<TimeGraph>(this, app_, &viewport_, capture_data, &GetPickingManager());
}

Batcher& CaptureWindow::GetBatcherById(BatcherId batcher_id) {
  switch (batcher_id) {
    case BatcherId::kTimeGraph:
      ORBIT_CHECK(time_graph_ != nullptr);
      return time_graph_->GetBatcher();
    case BatcherId::kUi:
      return ui_batcher_;
    default:
      ORBIT_UNREACHABLE();
  }
}

void CaptureWindow::RequestUpdatePrimitives() {
  redraw_requested_ = true;
  if (time_graph_ == nullptr) return;
  time_graph_->RequestUpdate();
}

[[nodiscard]] bool CaptureWindow::IsRedrawNeeded() const {
  return GlCanvas::IsRedrawNeeded() || (time_graph_ != nullptr && time_graph_->IsRedrawNeeded());
}

void CaptureWindow::RenderImGuiDebugUI() {
  if (ImGui::CollapsingHeader("Layout Properties")) {
    if (time_graph_ != nullptr && time_graph_->GetLayout().DrawProperties()) {
      RequestUpdatePrimitives();
    }

    static bool draw_text_outline = false;
    if (ImGui::Checkbox("Draw Text Outline", &draw_text_outline)) {
      TextRenderer::SetDrawOutline(draw_text_outline);
      RequestUpdatePrimitives();
    }
  }

  if (ImGui::CollapsingHeader("Capture Info")) {
    IMGUI_VAR_TO_TEXT(viewport_.GetScreenWidth());
    IMGUI_VAR_TO_TEXT(viewport_.GetScreenHeight());
    IMGUI_VAR_TO_TEXT(viewport_.GetWorldWidth());
    IMGUI_VAR_TO_TEXT(viewport_.GetWorldHeight());
    IMGUI_VAR_TO_TEXT(mouse_move_pos_screen_[0]);
    IMGUI_VAR_TO_TEXT(mouse_move_pos_screen_[1]);
    if (time_graph_ != nullptr) {
      IMGUI_VAR_TO_TEXT(time_graph_->GetTrackContainer()->GetNumVisiblePrimitives());
      IMGUI_VAR_TO_TEXT(time_graph_->GetTrackManager()->GetAllTracks().size());
      IMGUI_VAR_TO_TEXT(time_graph_->GetMinTimeUs());
      IMGUI_VAR_TO_TEXT(time_graph_->GetMaxTimeUs());
      IMGUI_VAR_TO_TEXT(time_graph_->GetCaptureMin());
      IMGUI_VAR_TO_TEXT(time_graph_->GetCaptureMax());
      IMGUI_VAR_TO_TEXT(time_graph_->GetTimeWindowUs());
      const CaptureData* capture_data = time_graph_->GetCaptureData();
      if (capture_data != nullptr) {
        IMGUI_VAR_TO_TEXT(capture_data->GetCallstackData().GetCallstackEventsCount());
      }
    }
  }

  if (ImGui::CollapsingHeader("Performance")) {
    for (auto& item : scoped_frame_times_) {
      IMGUI_VARN_TO_TEXT(item.second->GetAverageTimeMs(),
                         (std::string("Avg time in ms: ") + item.first));
      IMGUI_VARN_TO_TEXT(item.second->GetMinTimeMs(),
                         (std::string("Min time in ms: ") + item.first));
      IMGUI_VARN_TO_TEXT(item.second->GetMaxTimeMs(),
                         (std::string("Max time in ms: ") + item.first));
    }
  }

  if (ImGui::CollapsingHeader("Selection Summary")) {
    const std::string& selection_summary = selection_stats_.GetSummary();

    if (ImGui::Button("Copy to clipboard")) {
      app_->SetClipboard(selection_summary);
    }

    ImGui::TextUnformatted(selection_summary.c_str(),
                           selection_summary.c_str() + selection_summary.size());
  }
}

void CaptureWindow::RenderText(float layer) {
  ORBIT_SCOPE_FUNCTION;
  if (time_graph_ == nullptr) return;
  if (picking_mode_ == PickingMode::kNone) {
    time_graph_->DrawText(layer);
  }
}

void CaptureWindow::RenderHelpUi() {
  constexpr int kOffset = 30;
  Vec2 world_pos = viewport_.ScreenToWorld(Vec2i(kOffset, kOffset));

  Vec2 text_bounding_box_pos;
  Vec2 text_bounding_box_size;
  // TODO(b/180312795): Use TimeGraphLayout's font size again.
  text_renderer_.AddText(GetHelpText(), world_pos[0], world_pos[1], GlCanvas::kZValueUi,
                         {14, Color(255, 255, 255, 255), -1.f /*max_size*/}, &text_bounding_box_pos,
                         &text_bounding_box_size);

  const Color kBoxColor(50, 50, 50, 230);
  const float kMargin = 15.f;
  const float kRoundingRadius = 20.f;
  primitive_assembler_.AddRoundedBox(text_bounding_box_pos, text_bounding_box_size,
                                     GlCanvas::kZValueUi, kRoundingRadius, kBoxColor, kMargin);
}

const char* CaptureWindow::GetHelpText() const {
  const char* help_message =
      "Start/Stop Capture: 'F5'\n\n"
      "Pan: 'A','D' or \"Left Click + Drag\"\n\n"
      "Scroll: Arrow Keys or Mouse Wheel\n\n"
      "Timeline Zoom (10%): 'W', 'S' or \"Ctrl + Mouse Wheel\"\n\n"
      "Zoom to Time Range: \"Ctrl + Right Click + Drag\"\n\n"
      "Select: Left Click\n\n"
      "Measure: \"Right Click + Drag\"\n\n"
      "UI Scale (10%): \"Ctrl + '+'/'-' \"\n\n"
      "Toggle Help: Ctrl + 'H'";
  return help_message;
}

inline double GetIncrementMs(double milli_seconds) {
  constexpr double kDay = 24 * 60 * 60 * 1000;
  constexpr double kHour = 60 * 60 * 1000;
  constexpr double kMinute = 60 * 1000;
  constexpr double kSecond = 1000;
  constexpr double kMilli = 1;
  constexpr double kMicro = 0.001;
  constexpr double kNano = 0.000001;

  std::string res;

  if (milli_seconds < kMicro) {
    return kNano;
  }
  if (milli_seconds < kMilli) {
    return kMicro;
  }
  if (milli_seconds < kSecond) {
    return kMilli;
  }
  if (milli_seconds < kMinute) {
    return kSecond;
  }
  if (milli_seconds < kHour) {
    return kMinute;
  }
  if (milli_seconds < kDay) {
    return kHour;
  }
  return kDay;
}

void CaptureWindow::RenderSelectionOverlay() {
  if (time_graph_ == nullptr) return;
  if (picking_mode_ != PickingMode::kNone) return;
  if (select_start_pos_world_[0] == select_stop_pos_world_[0]) return;

  uint64_t min_time = std::min(select_start_time_, select_stop_time_);
  uint64_t max_time = std::max(select_start_time_, select_stop_time_);

  float from_world = time_graph_->GetWorldFromTick(min_time);
  float to_world = time_graph_->GetWorldFromTick(max_time);
  float stop_pos_world = time_graph_->GetWorldFromTick(select_stop_time_);

  float size_x = to_world - from_world;
  // TODO(http://b/226401787): Allow green selection overlay to be on top of the Timeline after
  // modifying its design and how the overlay is drawn
  float initial_y_position = time_graph_->GetLayout().GetTimeBarHeight();
  Vec2 pos(from_world, initial_y_position);
  Vec2 size(size_x, viewport_.GetWorldHeight() - initial_y_position);

  std::string text = orbit_display_formats::GetDisplayTime(TicksToDuration(min_time, max_time));
  const Color color(0, 128, 0, 128);

  Quad box = MakeBox(pos, size);
  primitive_assembler_.AddBox(box, GlCanvas::kZValueOverlay, color);

  TextRenderer::HAlign alignment = select_stop_pos_world_[0] < select_start_pos_world_[0]
                                       ? TextRenderer::HAlign::Left
                                       : TextRenderer::HAlign::Right;
  TextRenderer::TextFormatting formatting;
  formatting.font_size = time_graph_->GetLayout().GetFontSize();
  formatting.color = Color(255, 255, 255, 255);
  formatting.halign = alignment;

  text_renderer_.AddText(text.c_str(), stop_pos_world, select_stop_pos_world_[1],
                         GlCanvas::kZValueOverlay, formatting);

  const unsigned char g = 100;
  Color grey(g, g, g, 255);
  primitive_assembler_.AddVerticalLine(pos, size[1], GlCanvas::kZValueOverlay, grey);
}
