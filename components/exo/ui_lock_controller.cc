// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/exo/ui_lock_controller.h"

#include <memory>

#include "ash/constants/app_types.h"
#include "ash/constants/ash_features.h"
#include "ash/public/cpp/keyboard/keyboard_controller.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/wm/window_state.h"
#include "ash/wm/window_state_observer.h"
#include "base/bind.h"
#include "base/feature_list.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chromeos/ui/base/window_properties.h"
#include "components/exo/pointer.h"
#include "components/exo/seat.h"
#include "components/exo/shell_surface_util.h"
#include "components/exo/surface.h"
#include "components/exo/wm_helper.h"
#include "components/fullscreen_control/fullscreen_control_popup.h"
#include "components/fullscreen_control/subtle_notification_view.h"
#include "components/strings/grit/components_strings.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/strings/grit/ui_strings.h"
#include "ui/views/widget/widget.h"

namespace {

// The Esc hold notification shows a message to press and hold Esc to exit
// fullscreen. It will hide after a 4s timeout but shows again each time the
// window goes to fullscreen.
//
// The exit popup is a circle with an 'X' close icon which exits fullscreen when
// the user clicks it.
// * It is only shown on windows with property kEscHoldToExitFullscreen.
// * It is displayed when the mouse moves to the top 3px of the screen.
// * It will hide after a 3s timeout, or the user moves below 150px.
// * After hiding, there is a cooldown where it will not display again until the
//   mouse moves below 150px.

// Duration to show notifications.
constexpr auto kNotificationDuration = base::Seconds(4);
// Position of Esc notification from top of screen.
const int kEscNotificationTopPx = 45;
// Duration to show the exit 'X' popup.
constexpr auto kExitPopupDuration = base::Seconds(3);
// Display the exit popup if mouse is above this height.
constexpr float kExitPopupDisplayHeight = 3.f;
// Hide the exit popup if mouse is below this height.
constexpr float kExitPopupHideHeight = 150.f;

// Once the pointer capture notification has finished showing without
// being interrupted, don't show it again until this long has passed.
constexpr auto kPointerCaptureNotificationCooldown = base::Minutes(5);

constexpr int kUILockControllerSeatObserverPriority = 1;
static_assert(
    exo::Seat::IsValidObserverPriority(kUILockControllerSeatObserverPriority),
    "kUILockCOntrollerSeatObserverPriority is not in the valid range");

bool IsUILockControllerEnabled(aura::Window* window) {
  if (!window)
    return false;

  if (window->GetProperty(chromeos::kEscHoldToExitFullscreen) ||
      window->GetProperty(chromeos::kUseOverviewToExitFullscreen) ||
      window->GetProperty(chromeos::kUseOverviewToExitPointerLock)) {
    return true;
  }
  return false;
}

// Creates the separator view between bubble views of modifiers and key.
std::unique_ptr<views::View> CreateIconView(const gfx::VectorIcon& icon) {
  constexpr int kIconSize = 28;

  std::unique_ptr<views::ImageView> view = std::make_unique<views::ImageView>();
  gfx::ImageSkia image = gfx::CreateVectorIcon(icon, SK_ColorWHITE);
  view->SetImage(ui::ImageModel::FromImageSkia(image));
  view->SetImageSize(gfx::Size(kIconSize, kIconSize));
  return view;
}

// Create and position Esc notification.
views::Widget* CreateEscNotification(
    aura::Window* parent,
    int message_id,
    std::initializer_list<int> key_message_ids) {
  auto content_view = std::make_unique<SubtleNotificationView>();

  std::vector<std::u16string> key_names;
  std::vector<std::unique_ptr<views::View>> icons;
  for (int key_message_id : key_message_ids) {
    key_names.push_back(l10n_util::GetStringUTF16(key_message_id));

    if (key_message_id == IDS_APP_OVERVIEW_KEY) {
      icons.push_back(CreateIconView(ash::kKsvOverviewIcon));
    } else {
      icons.push_back(nullptr);
    }
  }
  content_view->UpdateContent(
      l10n_util::GetStringFUTF16(message_id, key_names, nullptr),
      std::move(icons));

  gfx::Size size = content_view->GetPreferredSize();
  views::Widget* popup = SubtleNotificationView::CreatePopupWidget(
      parent, std::move(content_view));
  popup->SetZOrderLevel(ui::ZOrderLevel::kSecuritySurface);
  gfx::Rect bounds = parent->GetBoundsInScreen();
  int y = bounds.y() + kEscNotificationTopPx;
  bounds.ClampToCenteredSize(size);
  bounds.set_y(y);
  popup->SetBounds(bounds);
  return popup;
}

// Exits fullscreen to previous state.
void ExitFullscreen(aura::Window* window) {
  ash::WindowState* window_state = ash::WindowState::Get(window);
  if (window_state->IsFullscreen())
    window_state->Restore();
}

// Owns the widgets for messages prompting to exit fullscreen/mouselock, and
// the exit popup. Owned as a window property.
class ExitNotifier : public ui::EventHandler, public ash::WindowStateObserver {
 public:
  explicit ExitNotifier(aura::Window* window) : window_(window) {
    ash::WindowState* window_state = ash::WindowState::Get(window);
    window_state_observation_.Observe(window_state);
    if (window_state->IsFullscreen())
      OnFullscreen();
  }

  ExitNotifier(const ExitNotifier&) = delete;
  ExitNotifier& operator=(const ExitNotifier&) = delete;

  ~ExitNotifier() override {
    OnExitFullscreen();
    ClosePointerCaptureNotification();
  }

  views::Widget* fullscreen_esc_notification() {
    return fullscreen_esc_notification_;
  }

  views::Widget* pointer_capture_notification() {
    return pointer_capture_notification_;
  }

  FullscreenControlPopup* exit_popup() { return exit_popup_.get(); }

  void MaybeShowPointerCaptureNotification() {
    // Respect cooldown.
    if (base::TimeTicks::Now() < next_pointer_notify_time_)
      return;

    want_pointer_capture_notification_ = true;

    // Don't show in fullscreen; the fullscreen notification will show and is
    // prioritized.
    ash::WindowState* window_state = ash::WindowState::Get(window_);
    if (window_state->IsFullscreen())
      return;

    if (pointer_capture_notification_) {
      pointer_capture_notification_->CloseWithReason(
          views::Widget::ClosedReason::kUnspecified);
    }

    if (ash::KeyboardController::Get()->AreTopRowKeysFunctionKeys()) {
      pointer_capture_notification_ =
          CreateEscNotification(window_, IDS_PRESS_TO_EXIT_MOUSELOCK_TWO_KEYS,
                                {IDS_APP_SEARCH_KEY, IDS_APP_OVERVIEW_KEY});
    } else {
      pointer_capture_notification_ = CreateEscNotification(
          window_, IDS_PRESS_TO_EXIT_MOUSELOCK, {IDS_APP_OVERVIEW_KEY});
    }

    pointer_capture_notification_->Show();

    // Close Esc notification after 4s.
    pointer_capture_notify_timer_.Start(
        FROM_HERE, kNotificationDuration,
        base::BindOnce(&ExitNotifier::OnPointerCaptureNotifyTimerFinished,
                       base::Unretained(this)));
  }

  void ClosePointerCaptureNotification() {
    pointer_capture_notify_timer_.Stop();
    if (pointer_capture_notification_) {
      pointer_capture_notification_->CloseWithReason(
          views::Widget::ClosedReason::kUnspecified);
      pointer_capture_notification_ = nullptr;
    }
  }

 private:
  void OnPointerCaptureNotifyTimerFinished() {
    // Start the cooldown when the timer successfully elapses, to ensure the
    // notification was shown for a sufficiently long time.
    next_pointer_notify_time_ =
        base::TimeTicks::Now() + kPointerCaptureNotificationCooldown;
    ClosePointerCaptureNotification();
    want_pointer_capture_notification_ = false;
  }

  // Overridden from ui::EventHandler:
  void OnMouseEvent(ui::MouseEvent* event) override {
    gfx::PointF point = event->location_f();
    aura::Window::ConvertPointToTarget(
        static_cast<aura::Window*>(event->target()), window_, &point);
    if (!fullscreen_esc_notification_ && !exit_popup_cooldown_ &&
        window_ == exo::WMHelper::GetInstance()->GetActiveWindow() &&
        point.y() <= kExitPopupDisplayHeight) {
      // Show exit popup if mouse is above 3px, unless esc notification is
      // visible, or during cooldown (popup shown and mouse still at top).
      if (!exit_popup_) {
        exit_popup_ = std::make_unique<FullscreenControlPopup>(
            window_, base::BindRepeating(&ExitFullscreen, window_),
            base::DoNothing());
      }
      views::Widget* widget =
          views::Widget::GetTopLevelWidgetForNativeView(window_);
      exit_popup_->Show(widget->GetClientAreaBoundsInScreen());
      exit_popup_timer_.Start(
          FROM_HERE, kExitPopupDuration,
          base::BindOnce(&ExitNotifier::HideExitPopup, base::Unretained(this),
                         /*animate=*/true));
      exit_popup_cooldown_ = true;
    } else if (point.y() > kExitPopupHideHeight) {
      // Hide exit popup if mouse is below 150px, reset cooloff.
      HideExitPopup(/*animate=*/true);
      exit_popup_cooldown_ = false;
    }
  }

  // Overridden from ash::WindowStateObserver:
  void OnPostWindowStateTypeChange(
      ash::WindowState* window_state,
      chromeos::WindowStateType old_type) override {
    DCHECK_EQ(window_, window_state->window());
    if (window_state->IsFullscreen()) {
      OnFullscreen();
    } else {
      OnExitFullscreen();
    }
  }

  void OnFullscreen() {
    // Register ui::EventHandler to watch if mouse goes to top of screen.
    if (!is_handling_events_ &&
        window_->GetProperty(chromeos::kEscHoldToExitFullscreen)) {
      window_->AddPreTargetHandler(this);
      is_handling_events_ = true;
    }

    // Only show Esc notification when window is active.
    if (window_ != exo::WMHelper::GetInstance()->GetActiveWindow())
      return;

    // Fullscreen notifications override pointer capture notifications.
    ClosePointerCaptureNotification();

    if (fullscreen_esc_notification_) {
      fullscreen_esc_notification_->CloseWithReason(
          views::Widget::ClosedReason::kUnspecified);
    }

    if (window_->GetProperty(chromeos::kUseOverviewToExitFullscreen)) {
      if (ash::KeyboardController::Get()->AreTopRowKeysFunctionKeys()) {
        fullscreen_esc_notification_ = CreateEscNotification(
            window_, IDS_FULLSCREEN_PRESS_TO_EXIT_FULLSCREEN_TWO_KEYS,
            {IDS_APP_SEARCH_KEY, IDS_APP_OVERVIEW_KEY});
      } else {
        fullscreen_esc_notification_ = CreateEscNotification(
            window_, IDS_FULLSCREEN_PRESS_TO_EXIT_FULLSCREEN,
            {IDS_APP_OVERVIEW_KEY});
      }
    } else {
      fullscreen_esc_notification_ = CreateEscNotification(
          window_,
          window_->GetProperty(chromeos::kEscHoldToExitFullscreen)
              ? IDS_FULLSCREEN_HOLD_TO_EXIT_FULLSCREEN
              : IDS_FULLSCREEN_PRESS_TO_EXIT_FULLSCREEN,
          {IDS_APP_ESC_KEY});
    }

    fullscreen_esc_notification_->Show();

    // Close Esc notification after 4s.
    fullscreen_notify_timer_.Start(
        FROM_HERE, kNotificationDuration,
        base::BindOnce(&ExitNotifier::CloseFullscreenEscNotification,
                       base::Unretained(this)));
  }

  void OnExitFullscreen() {
    if (is_handling_events_) {
      window_->RemovePreTargetHandler(this);
      is_handling_events_ = false;
    }
    CloseFullscreenEscNotification();
    HideExitPopup();
  }

  void CloseFullscreenEscNotification() {
    if (!fullscreen_esc_notification_)
      return;
    fullscreen_esc_notification_->CloseWithReason(
        views::Widget::ClosedReason::kUnspecified);
    fullscreen_esc_notification_ = nullptr;

    // If a pointer capture notification was previously requested and didn't
    // show (or didn't complete its timer), show it now.
    //
    // This is to prevent the following scenario:
    //   1. App goes fullscreen
    //   2. App immediately requests pointer capture; no notification is shown,
    //      since the fullscreen notification is already visible.
    //   3. App immediately unfullscreens; the fullscreen notification closes.
    //
    // Without this check, the app would have gained pointer capture without
    // any notification showing.
    if (want_pointer_capture_notification_)
      MaybeShowPointerCaptureNotification();
  }

  void HideExitPopup(bool animate = false) {
    if (exit_popup_)
      exit_popup_->Hide(animate);
  }

  aura::Window* const window_;
  views::Widget* fullscreen_esc_notification_ = nullptr;
  views::Widget* pointer_capture_notification_ = nullptr;
  bool want_pointer_capture_notification_ = false;
  std::unique_ptr<FullscreenControlPopup> exit_popup_;
  bool is_handling_events_ = false;
  bool exit_popup_cooldown_ = false;
  base::OneShotTimer fullscreen_notify_timer_;
  base::OneShotTimer pointer_capture_notify_timer_;
  base::TimeTicks next_pointer_notify_time_;
  base::OneShotTimer exit_popup_timer_;
  base::ScopedObservation<ash::WindowState, ash::WindowStateObserver>
      window_state_observation_{this};
};

}  // namespace

DEFINE_UI_CLASS_PROPERTY_TYPE(ExitNotifier*)

namespace exo {
namespace {
DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(ExitNotifier, kExitNotifierKey, nullptr)

ExitNotifier* GetExitNotifier(aura::Window* window, bool create) {
  if (!base::FeatureList::IsEnabled(chromeos::features::kExoLockNotification))
    return nullptr;

  if (!window)
    return nullptr;

  aura::Window* toplevel = window->GetToplevelWindow();
  if (!IsUILockControllerEnabled(toplevel))
    return nullptr;

  ExitNotifier* notifier = toplevel->GetProperty(kExitNotifierKey);
  if (!notifier && create) {
    // Object is owned as a window property.
    notifier = toplevel->SetProperty(kExitNotifierKey,
                                     std::make_unique<ExitNotifier>(toplevel));
  }

  return notifier;
}

}  // namespace

constexpr auto kLongPressEscapeDuration = base::Seconds(2);
constexpr auto kExcludedFlags = ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN |
                                ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN |
                                ui::EF_ALTGR_DOWN | ui::EF_IS_REPEAT;

UILockController::UILockController(Seat* seat) : seat_(seat) {
  WMHelper::GetInstance()->AddPreTargetHandler(this);
  seat_->AddObserver(this, kUILockControllerSeatObserverPriority);
}

UILockController::~UILockController() {
  seat_->RemoveObserver(this);
  WMHelper::GetInstance()->RemovePreTargetHandler(this);
}

void UILockController::OnKeyEvent(ui::KeyEvent* event) {
  // TODO(oshima): Rather than handling key event here, add a hook in
  // keyboard.cc to intercept key event and handle this.

  // If no surface is focused, let another handler process the event.
  aura::Window* window = static_cast<aura::Window*>(event->target());
  if (!GetTargetSurfaceForKeyboardFocus(window))
    return;

  if (event->code() == ui::DomCode::ESCAPE &&
      (event->flags() & kExcludedFlags) == 0) {
    OnEscapeKey(event->type() == ui::ET_KEY_PRESSED);
  }
}

void UILockController::OnSurfaceFocused(Surface* gained_focus,
                                        Surface* lost_focus,
                                        bool has_focused_surface) {
  if (gained_focus != focused_surface_to_unlock_)
    StopTimer();

  if (!base::FeatureList::IsEnabled(chromeos::features::kExoLockNotification))
    return;

  if (!gained_focus || !gained_focus->window())
    return;

  aura::Window* window = gained_focus->window()->GetToplevelWindow();
  if (!IsUILockControllerEnabled(window))
    return;

  // Object is owned as a window property.
  if (!window->GetProperty(kExitNotifierKey)) {
    window->SetProperty(kExitNotifierKey,
                        std::make_unique<ExitNotifier>(window));
  }
}

void UILockController::OnPointerCaptureEnabled(Pointer* pointer,
                                               aura::Window* window) {
  aura::Window* toplevel = window ? window->GetToplevelWindow() : nullptr;
  if (!toplevel ||
      !toplevel->GetProperty(chromeos::kUseOverviewToExitPointerLock))
    return;

  captured_pointers_.insert(pointer);
  ExitNotifier* notifier = GetExitNotifier(window, false);
  if (notifier)
    notifier->MaybeShowPointerCaptureNotification();
}

void UILockController::OnPointerCaptureDisabled(Pointer* pointer,
                                                aura::Window* window) {
  if (captured_pointers_.empty())
    return;

  captured_pointers_.erase(pointer);
  if (captured_pointers_.empty()) {
    ExitNotifier* notifier = GetExitNotifier(window, false);
    if (notifier)
      notifier->ClosePointerCaptureNotification();
  }
}

views::Widget* UILockController::GetPointerCaptureNotificationForTesting(
    aura::Window* window) {
  return window->GetProperty(kExitNotifierKey)->pointer_capture_notification();
}

views::Widget* UILockController::GetEscNotificationForTesting(
    aura::Window* window) {
  return window->GetProperty(kExitNotifierKey)->fullscreen_esc_notification();
}

FullscreenControlPopup* UILockController::GetExitPopupForTesting(
    aura::Window* window) {
  return window->GetProperty(kExitNotifierKey)->exit_popup();
}

namespace {
bool EscapeHoldShouldExitFullscreen(Seat* seat) {
  auto* surface = seat->GetFocusedSurface();
  if (!surface)
    return false;

  auto* widget =
      views::Widget::GetTopLevelWidgetForNativeView(surface->window());
  if (!widget)
    return false;

  aura::Window* window = widget->GetNativeWindow();
  if (!window || !window->GetProperty(chromeos::kEscHoldToExitFullscreen)) {
    return false;
  }

  auto* window_state = ash::WindowState::Get(window);
  return window_state && window_state->IsFullscreen();
}
}  // namespace

void UILockController::OnEscapeKey(bool pressed) {
  if (pressed) {
    if (EscapeHoldShouldExitFullscreen(seat_) &&
        !exit_fullscreen_timer_.IsRunning()) {
      focused_surface_to_unlock_ = seat_->GetFocusedSurface();
      exit_fullscreen_timer_.Start(
          FROM_HERE, kLongPressEscapeDuration,
          base::BindOnce(&UILockController::OnEscapeHeld,
                         base::Unretained(this)));
    }
  } else {
    StopTimer();
  }
}

void UILockController::OnEscapeHeld() {
  auto* surface = seat_->GetFocusedSurface();
  if (!surface || surface != focused_surface_to_unlock_) {
    focused_surface_to_unlock_ = nullptr;
    return;
  }

  focused_surface_to_unlock_ = nullptr;

  ExitFullscreen(surface->window()->GetToplevelWindow());
}

void UILockController::StopTimer() {
  if (exit_fullscreen_timer_.IsRunning()) {
    exit_fullscreen_timer_.Stop();
    focused_surface_to_unlock_ = nullptr;
  }
}

}  // namespace exo
