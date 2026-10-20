#pragma once
#include "gamepad/gamepad.hpp"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace gamepad::detail {
// Worker-owned after construction. No log sink callbacks on the capture thread.
class Logger {
public:
    explicit Logger(const Config& config) : config_(config) {
        if (!config_.log_file.empty() && config_.log_level != LogLevel::off) {
            path_ = std::filesystem::u8path(config_.log_file);
            file_.open(path_, std::ios::binary | std::ios::app);
            if (!file_) throw std::runtime_error("Cannot open gamepad log file: " + config_.log_file);
            std::error_code ec;
            bytes_ = std::filesystem::file_size(path_, ec);
            if (ec) bytes_ = 0;
        }
    }
    void write(LogLevel level, const char* message, std::uint64_t detail = 0) noexcept {
        if (level == LogLevel::off || config_.log_level == LogLevel::off || level > config_.log_level)
            return;
        try {
            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            std::tm calendar{};
#ifdef _WIN32
            localtime_s(&calendar, &time);
#else
            localtime_r(&time, &calendar);
#endif
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count() % 1000;
            std::ostringstream out;
            const char* name = level == LogLevel::error ? "ERROR" :
                level == LogLevel::warning ? "WARN" : level == LogLevel::info ? "INFO" : "DEBUG";
            out << std::put_time(&calendar, "%Y-%m-%dT%H:%M:%S") << '.'
                << std::setfill('0') << std::setw(3) << ms << " [" << name << "] "
                << config_.device_label << " slot=" << config_.slot << ' ' << message
                << " value=" << detail << '\n';
            const auto line = out.str();
            if (config_.log_to_console) std::clog << line << std::flush;
            if (file_.is_open()) {
                if (bytes_ + line.size() > config_.log_max_bytes) rotate();
                file_ << line;
                file_.flush(); // Lifecycle/warning logs only, never per input.
                if (!file_) throw std::runtime_error("gamepad log write failed");
                bytes_ += line.size();
            }
        } catch (...) {
            file_.close();
            if (!failure_reported_) {
                failure_reported_ = true;
                // Keep diagnostics failure from terminating the capture worker.
                try { if (config_.log_to_console) std::clog << "[ERROR] gamepad file logging disabled after I/O failure\n"; }
                catch (...) {}
            }
        }
    }
private:
    Config config_;
    std::filesystem::path path_;
    std::ofstream file_;
    std::uintmax_t bytes_ = 0;
    bool failure_reported_ = false;
    void rotate() {
        file_.close();
        if (config_.log_backups == 0) {
            file_.open(path_, std::ios::binary | std::ios::trunc);
        } else {
            const auto backup = [&](std::size_t i) {
                auto path = path_; path += "." + std::to_string(i); return path;
            };
            std::filesystem::remove(backup(config_.log_backups));
            for (std::size_t i = config_.log_backups; i > 1; --i) {
                if (std::filesystem::exists(backup(i - 1)))
                    std::filesystem::rename(backup(i - 1), backup(i));
            }
            if (std::filesystem::exists(path_)) std::filesystem::rename(path_, backup(1));
            file_.open(path_, std::ios::binary | std::ios::trunc);
        }
        if (!file_) throw std::runtime_error("gamepad log rotation failed");
        bytes_ = 0;
    }
};
} // namespace gamepad::detail
