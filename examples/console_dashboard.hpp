#pragma once

#include "gamepad/gamepad.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace gamepad::example {

// Presentation only: caller/main thread owns this object. Labels are drawn once;
// subsequent frames write only changed values, at most 30 times per second.
class ConsoleDashboard final {
public:
    static std::unique_ptr<ConsoleDashboard> open(std::string profile) {
        auto dashboard = std::unique_ptr<ConsoleDashboard>(new ConsoleDashboard(std::move(profile)));
        if (!dashboard->enable_terminal()) return nullptr;
        dashboard->active_ = true;
        std::cout << "\x1b[?1049h\x1b[?25l" << std::flush;
        return dashboard;
    }
    ~ConsoleDashboard() {
        if (!active_) return;
        try {
            std::cout << "\x1b[0m\x1b[?1049l"
                      << (cursor_was_visible_ ? "\x1b[?25h" : "\x1b[?25l") << std::flush;
        } catch (...) {}
#ifdef _WIN32
        SetConsoleMode(output_, original_mode_);
#endif
    }
    ConsoleDashboard(const ConsoleDashboard&) = delete;
    ConsoleDashboard& operator=(const ConsoleDashboard&) = delete;

    void observe(const Event& event) {
        if (event.type == EventType::short_press) ++short_count_;
        if (event.type == EventType::long_press) ++long_count_;
        if (event.type == EventType::backend_error) last_error_ = event.native_error;
        if (event.type == EventType::connected) last_error_ = 0;
        if (event.button == Button::none) return;

        const auto name = button_name(event.button);
        const auto duration = std::to_string(event.held_ns / 1000000) + " ms";
        if (event.cancelled != CancelReason::none) {
            last_action_ = name + " CANCELLED (" + to_string(event.cancelled) + ')';
        } else if (event.type == EventType::button_down) {
            last_action_ = name + " DOWN";
        } else if (event.type == EventType::short_press) {
            last_action_ = name + " SHORT PRESS (" + duration + ')';
        } else if (event.type == EventType::long_press) {
            last_action_ = name + " LONG PRESS (" + duration + ')';
            last_long_button_ = event.button;
        } else if (event.type == EventType::button_up) {
            if (last_long_button_ == event.button) {
                last_action_ = name + " LONG PRESS, released (" + duration + ')';
                last_long_button_ = Button::none;
            } else {
                last_action_ = name + " UP (" + duration + ')';
            }
        }
    }

    void render(const State& state, const Statistics& stats, bool force = false) {
        const auto now = std::chrono::steady_clock::now();
        if (!force && now < next_frame_) return;
        next_frame_ = now + std::chrono::milliseconds(33);
        const auto dimensions = terminal_size();
        const bool resized = dimensions != size_;
        if (resized || !drawn_) {
            size_ = dimensions;
            draw_layout();
            force = true;
        }
        if (size_.first < 40 || size_.second < 17) return;

        const auto& raw = state.raw;
        const std::array<std::string, 11> values = {
            std::string(state.connected ? "CONNECTED" : "DISCONNECTED") +
                "  slot=" + std::to_string(state.device.slot),
            pressed_buttons(state, false),
            pressed_buttons(state, true),
            stick(raw.left_x, raw.left_y),
            stick(raw.right_x, raw.right_y),
            trigger(raw.left_trigger, state.pressed(Button::left_trigger)),
            trigger(raw.right_trigger, state.pressed(Button::right_trigger)),
            last_action_,
            "short=" + std::to_string(short_count_) + "  long=" + std::to_string(long_count_),
            "pending=" + std::to_string(stats.queue_depth) +
                "  dropped=" + std::to_string(stats.dropped),
            "errors=" + std::to_string(stats.read_errors) +
                "  last=" + std::to_string(last_error_) +
                "  late=" + std::to_string(stats.missed_deadlines)
        };
        std::string output;
        output.reserve(1024);
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto visible = values[i].substr(0, static_cast<std::size_t>(size_.first - value_column_));
            if (!force && visible == previous_[i]) continue;
            // Erase only the value area, keeping the label untouched.
            output += position(static_cast<int>(i) + 4, value_column_);
            output += "\x1b[K";
            output += visible;
            previous_[i] = visible;
        }
        if (!output.empty()) std::cout << output << std::flush;
    }

private:
    explicit ConsoleDashboard(std::string profile) : profile_(std::move(profile)) {}
    static constexpr int value_column_ = 15;
    bool active_ = false, drawn_ = false, cursor_was_visible_ = true;
    std::string profile_, last_action_ = "-";
    Button last_long_button_ = Button::none;
    std::uint64_t short_count_ = 0, long_count_ = 0;
    std::uint32_t last_error_ = 0;
    std::array<std::string, 11> previous_;
    std::pair<int, int> size_{0, 0};
    std::chrono::steady_clock::time_point next_frame_{};
#ifdef _WIN32
    HANDLE output_ = INVALID_HANDLE_VALUE;
    DWORD original_mode_ = 0;
#endif
    bool enable_terminal() {
#ifdef _WIN32
        output_ = GetStdHandle(STD_OUTPUT_HANDLE);
        if (output_ == INVALID_HANDLE_VALUE || !GetConsoleMode(output_, &original_mode_)) return false;
        CONSOLE_CURSOR_INFO cursor{};
        if (GetConsoleCursorInfo(output_, &cursor)) cursor_was_visible_ = cursor.bVisible != FALSE;
        return SetConsoleMode(output_, original_mode_ | ENABLE_PROCESSED_OUTPUT |
                              ENABLE_VIRTUAL_TERMINAL_PROCESSING) != FALSE;
#else
        return isatty(STDOUT_FILENO) != 0;
#endif
    }
    std::pair<int, int> terminal_size() const {
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(output_, &info))
            return {info.srWindow.Right - info.srWindow.Left + 1,
                    info.srWindow.Bottom - info.srWindow.Top + 1};
#else
        winsize size{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0)
            return {static_cast<int>(size.ws_col), static_cast<int>(size.ws_row)};
#endif
        return {80, 24};
    }
    static std::string position(int row, int column) {
        return "\x1b[" + std::to_string(row) + ';' + std::to_string(column) + 'H';
    }
    void draw_layout() {
        drawn_ = true;
        previous_.fill("");
        std::string layout = "\x1b[2J\x1b[H";
        if (size_.first < 40 || size_.second < 17) {
            layout += "Resize terminal to at least 40x17.";
        } else {
            layout += ("Gamepad input bridge | " + profile_).substr(0, static_cast<std::size_t>(size_.first - 1));
            layout += position(2, 1) + "Live view - Ctrl+C exits";
            const std::array<const char*, 11> labels = {
                "connection:", "button:", "dpad:", "left stick:", "right stick:",
                "LT:", "RT:", "last action:", "press count:", "queue:", "diagnostics:"
            };
            for (std::size_t i = 0; i < labels.size(); ++i)
                layout += position(static_cast<int>(i) + 4, 1) + labels[i];
            layout += position(16, 1) + std::string("--events: scrolling output; --json: JSON")
                .substr(0, static_cast<std::size_t>(size_.first - 1));
        }
        std::cout << layout << std::flush;
    }
    static std::string button_name(Button button) {
        switch (button) {
        case Button::a: return "A";
        case Button::b: return "B";
        case Button::x: return "X";
        case Button::y: return "Y";
        case Button::left_shoulder: return "LB";
        case Button::right_shoulder: return "RB";
        case Button::left_stick: return "LS";
        case Button::right_stick: return "RS";
        case Button::left_trigger: return "LT";
        case Button::right_trigger: return "RT";
        case Button::start: return "START";
        case Button::back: return "BACK";
        case Button::dpad_up: return "UP";
        case Button::dpad_down: return "DOWN";
        case Button::dpad_left: return "LEFT";
        case Button::dpad_right: return "RIGHT";
        default: return "-";
        }
    }
    static std::string pressed_buttons(const State& state, bool dpad) {
        constexpr std::array<Button, 14> buttons = {
            Button::a, Button::b, Button::x, Button::y, Button::left_shoulder,
            Button::right_shoulder, Button::left_stick, Button::right_stick,
            Button::start, Button::back, Button::dpad_up, Button::dpad_down,
            Button::dpad_left, Button::dpad_right
        };
        std::string result;
        for (const auto button : buttons) {
            const bool is_dpad = (mask(button) & 0x000F) != 0;
            if (is_dpad != dpad || !state.pressed(button)) continue;
            if (!result.empty()) result += " + ";
            result += button_name(button);
        }
        return result.empty() ? "-" : result;
    }
    static std::string stick(std::int16_t x, std::int16_t y) {
        std::ostringstream value;
        value << "X=" << std::setw(6) << x << "  Y=" << std::setw(6) << y;
        return value.str();
    }
    static std::string trigger(std::uint8_t value, bool pressed) {
        const auto filled = static_cast<std::size_t>(value) * 12 / 255;
        std::ostringstream text;
        text << std::setw(3) << static_cast<unsigned>(value) << "/255 ["
             << std::string(filled, '#') << std::string(12 - filled, '-') << ']';
        if (pressed) text << " DOWN";
        return text.str();
    }
};
} // namespace gamepad::example
