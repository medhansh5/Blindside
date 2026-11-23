#include "blindside/platform.hpp"
#include <iostream>
#include <cstdlib>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <unistd.h>

namespace blindside {

class LinuxX11PlatformManager : public PlatformManager {
public:
    LinuxX11PlatformManager() = default;
    
    ~LinuxX11PlatformManager() {
        clear_alerts();
        if (display_) {
            XCloseDisplay(display_);
        }
    }

    bool initialize() override {
        display_ = XOpenDisplay(nullptr);
        if (!display_) {
            std::cerr << "[Platform] X11 display connection unavailable (headless or non-GUI environment). Falling back to non-visual responses." << std::endl;
            return true;
        }
        return true;
    }

    PlatformCapabilities get_capabilities() const override {
        PlatformCapabilities caps;
        caps.screen_redaction = (display_ != nullptr);
        caps.targeted_blur = false; // X11 override-redirect window provides opaque redaction, not compositor blur
        caps.session_lock = true;   // via loginctl/xset/dbus-screensaver
        caps.click_through_overlay = false; // Standard X11 override-redirect blocks pointer events without XShape
        caps.multi_monitor_aware = (display_ != nullptr);
        return caps;
    }

    PlatformDiagnostics get_diagnostics() const override {
        PlatformDiagnostics diag;
        diag.os_name = "Linux (X11)";
        diag.supports_native_redaction = (display_ != nullptr);
        diag.supports_targeted_blur = false;
        diag.supports_screen_lock = true;
        diag.supports_desktop_notifications = true;
        diag.supports_click_through = false;
        // Note: ScreenCount() reports X11 screens; modern multi-head RandR setups map displays into Screen 0
        diag.monitor_count = display_ ? ScreenCount(display_) : 1;
        diag.redaction_mode = display_ ? "OpaqueRedaction (X11 Override-Redirect)" : "None (Headless Fallback)";
        diag.capabilities = get_capabilities();
        return diag;
    }

    WindowRect get_active_window_geometry() override {
        WindowRect rect;
        if (display_) {
            Window focus_win = 0;
            int revert_to = 0;
            XGetInputFocus(display_, &focus_win, &revert_to);
            if (focus_win != 0 && focus_win != PointerRoot) {
                Window root = 0;
                int x = 0, y = 0;
                unsigned int width = 0, height = 0, border = 0, depth = 0;
                if (XGetGeometry(display_, focus_win, &root, &x, &y, &width, &height, &border, &depth)) {
                    int child_x = 0, child_y = 0;
                    Window child = 0;
                    XTranslateCoordinates(display_, focus_win, root, 0, 0, &child_x, &child_y, &child);
                    rect.x = child_x;
                    rect.y = child_y;
                    rect.width = static_cast<int>(width);
                    rect.height = static_cast<int>(height);
                    rect.valid = true;
                }
            }
        }
        if (!rect.valid) {
            rect.x = 100;
            rect.y = 100;
            rect.width = 1280;
            rect.height = 800;
            rect.valid = true;
        }
        return rect;
    }

    void trigger_targeted_blur(const WindowRect& rect) override {
        if (!display_) return;
        if (rect.width <= 0 || rect.height <= 0) return;

        if (overlay_win_ == 0) {
            int screen = DefaultScreen(display_);
            Window root = RootWindow(display_, screen);

            XSetWindowAttributes attrs;
            attrs.override_redirect = True;
            attrs.background_pixel = BlackPixel(display_, screen);

            overlay_win_ = XCreateWindow(
                display_, root,
                rect.x, rect.y, static_cast<unsigned int>(rect.width), static_cast<unsigned int>(rect.height),
                0, CopyFromParent, InputOutput, CopyFromParent,
                CWOverrideRedirect | CWBackPixel, &attrs
            );

            if (overlay_win_) {
                XMapRaised(display_, overlay_win_);
                XFlush(display_);
                last_rect_ = rect;
            }
        } else {
            // Idempotent repositioning if geometry changed
            if (rect.x != last_rect_.x || rect.y != last_rect_.y ||
                rect.width != last_rect_.width || rect.height != last_rect_.height) {
                XMoveResizeWindow(display_, overlay_win_,
                                  rect.x, rect.y,
                                  static_cast<unsigned int>(rect.width),
                                  static_cast<unsigned int>(rect.height));
                XRaiseWindow(display_, overlay_win_);
                XFlush(display_);
                last_rect_ = rect;
            }
        }

        pump_events();
    }

    void trigger_soft_alert(const std::string& message) override {
        int ret = std::system("notify-send -u critical -t 2000 '🛡️ Blindside Privacy Warning' 'Unrecognized face looking at screen!' 2>/dev/null &");
        (void)ret;
    }

    void trigger_hard_defense(const std::string& message) override {
        int ret = std::system("loginctl lock-session 2>/dev/null || xset ss activate 2>/dev/null || dbus-send --type=method_call --dest=org.gnome.ScreenSaver /org/gnome/ScreenSaver org.gnome.ScreenSaver.Lock 2>/dev/null &");
        (void)ret;
    }

    void clear_alerts() override {
        if (display_ && overlay_win_ != 0) {
            XUnmapWindow(display_, overlay_win_);
            XDestroyWindow(display_, overlay_win_);
            overlay_win_ = 0;
            last_rect_ = WindowRect{};
            XFlush(display_);
            pump_events();
        }
    }

    void pump_events() override {
        if (!display_) return;
        while (XPending(display_) > 0) {
            XEvent ev;
            XNextEvent(display_, &ev);
        }
    }

private:
    Display* display_ = nullptr;
    Window overlay_win_ = 0;
    WindowRect last_rect_{};
};

std::unique_ptr<PlatformManager> PlatformManager::create() {
    return std::make_unique<LinuxX11PlatformManager>();
}

} // namespace blindside
