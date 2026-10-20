# 使用与接入指南

## 当前支持范围

原生采集支持 Windows XInput 1.4。飞智沙漠狐在 USB 接收器的 PC 模式下已完成部分实机验证，详细控制项见 [兼容列表](../README.md#手柄兼容列表)。型号编译配置不等于硬件身份校验；同一只手柄切换到 Switch 或其他模式后，需要重新适配和验证。

Linux 原生后端尚未实现。后续移植以 Ubuntu 为目标，暂不进行 Debian 13 或 WSL USB 转发测试。现有 Linux CI 仅验证共用核心，不能据此认定 Linux 手柄输入已受支持。

## 启动示例程序

以下命令在仓库根目录执行，使用 Ninja 构建后的路径。Visual Studio 构建时在 `bin` 后增加 `Release` 目录；运行包中的程序在 `bin` 目录。

```powershell
.\build\bin\gamepad_flydigi_desert_fox.exe
```

交互终端默认显示固定面板，左侧标签固定，右侧值变化时原位更新。多个键显示为 `A + B`；最近一次短按或长按会保留，便于松手后查看。左右摇杆显示原始 X/Y 数值，左右扳机分别显示数值和进度条。

面板最多约每秒刷新 30 次，独立于采集周期。面板慢一些不会降低采集线程的配置频率。`press count` 统计示例实际消费到的短按/长按，队列丢失时不能作为全部物理操作次数。终端过小时会提示调整到至少 40 列、17 行。

按 Ctrl+C 正常退出。程序会停止线程并恢复进入面板之前的终端画面和光标。修改或重新编译正在运行的 EXE 前，先退出旧程序。

## 输出与日志

| 场景 | 命令或选项 |
| --- | --- |
| 实时面板 | 无输出选项，或显式 `--live` |
| 原来的逐行事件输出 | `--events` |
| JSON Lines 数据输出 | `--json` |
| 自动运行 30 秒后退出 | `--duration 30` |
| 只消费并显示退出统计 | `--stats-only` |
| 写轮转日志 | `--log-file capture.log` |
| 查看完整参数 | `--help` |

`--live`、`--events`、`--json` 互斥。输出重定向到文件时，默认自动使用逐行模式，不写光标控制字符；显式 `--live` 遇到不支持的终端会报错。`--live` 不能与 `--stats-only` 组合。

记录 30 秒 JSON 输入，并单独保存诊断输出：

```powershell
.\build\bin\gamepad_flydigi_desert_fox.exe --json --duration 30 --log-file capture.log > events.jsonl 2> diagnostics.log
```

实时面板禁止模块日志输出到控制台，以免打乱布局；文件日志仍可用。面板的 `diagnostics` 显示读取错误次数、最近错误码和错过轮询期限次数，`queue` 显示队列深度与丢弃数量。逐行/JSON 模式下，日志与退出统计写 stderr，输入数据写 stdout。

模块默认日志级别为 warning，示例程序默认 info，可用 `--log-level off/error/warning/info/debug` 调整。默认日志单文件上限为 1 MiB，保留两份备份；日志父目录必须已经存在。打开日志失败会拒绝启动；运行时写文件失败会停止文件日志并尝试报告一次，不会因此终止采集。

## 常用命令行配置

| 选项 | 默认值 | 有效范围或说明 |
| --- | --- | --- |
| `--slot` | 0 | 0..3；当前 XInput 槽位，不是永久设备 ID |
| `--queue-capacity` | 256 | 1..1048576 条消息 |
| `--poll-ms` | 4 | 1..1000 ms；连接时的目标查询间隔 |
| `--long-press-ms` | 500 | 1..3600000 ms；达到阈值触发一次长按 |
| `--analog-ms` | 8 | 0..1000 ms；0 表示不合并模拟消息 |
| `--duration` | 0 | 0..86400 秒；0 表示运行至 Ctrl+C |

示例只开放常用参数。完整 C++ `Config` 还可设置重连周期、摇杆死区、扳机滞回、日志备份数量等，见下文。

## CMake 接入

把本项目作为子目录，关闭示例与测试，只链接模块：

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_app LANGUAGES CXX)
set(GAMEPAD_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(GAMEPAD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/gamepad-input-bridge gamepad-build)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE gamepad::input)
```

也可以先执行 `cmake --install`，在消费项目中使用 `find_package(GamepadInputBridge CONFIG REQUIRED)`。静态库必须与消费工程使用兼容的编译器、运行库和架构；例如 MinGW 的 `.a` 不能直接作为 MSVC 静态库使用，换工具链时应从源码重建。

## 一个完整的 C++ 消费示例

以下程序运行 10 秒。构造对象自动启动线程，Lambda 在调用它的 main 线程执行；`stop()` 后排空队列，析构会再次安全地确认停止。

```cpp
#include <gamepad/gamepad.hpp>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>

int main() {
    using namespace std::chrono_literals;
    try {
        gamepad::Config config;
        config.slot = 0;
        config.queue_capacity = 256;
        config.poll_interval = 4ms;
        config.long_press_threshold = 500ms;
        gamepad::Gamepad input(config);

        std::uint64_t last_sequence = 0;
        std::uint64_t last_dropped = 0;
        auto handle = [&](const gamepad::Event& event) {
            const bool gap = event.sequence != last_sequence + 1;
            if (gap || event.dropped_total != last_dropped) {
                // 业务此时应根据 event.state 恢复状态，放弃旧的边沿推断。
                std::cerr << "Input gap; current A="
                          << event.state.pressed(gamepad::Button::a) << '\n';
            }
            last_sequence = event.sequence;
            last_dropped = event.dropped_total;
            std::cout << gamepad::to_string(event.type) << ' '
                      << gamepad::to_string(event.button) << '\n';
        };

        const auto end = std::chrono::steady_clock::now() + 10s;
        while (std::chrono::steady_clock::now() < end) {
            gamepad::Event event;
            const auto result = input.wait_pop(event, 100ms);
            if (result == gamepad::WaitResult::stopped) break;
            if (result == gamepad::WaitResult::event) {
                handle(event);
                input.dispatch_pending(handle, 64);
            }
        }
        input.stop();
        while (input.dispatch_pending(handle, 64) != 0) {}
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
```

已有业务主循环时，可以只调用 `dispatch_pending(lambda, max_count)`。它不阻塞，每次最多处理指定数量；`try_pop` 则每次尝试取一条。`wait_pop` 可等待消息、超时或队列停止，负超时会抛异常。

三个消费接口共用一个 FIFO。多个消费者会分走消息，不提供广播。回调耗时不阻塞采集线程，但可能导致队列积压；回调异常会传回调用者，该消息已经出队。不要在回调中销毁正在派发消息的对象。

## C++ 配置默认值

| 字段 | 默认值 | 含义与限制 |
| --- | --- | --- |
| `device_label` | `generic_xinput` | 日志标签，不用于识别硬件 |
| `slot` | 0 | 当前 XInput 槽位，0..3 |
| `queue_capacity` | 256 | 固定容量，满时丢最老消息 |
| `poll_interval` | 4000 us | 连接时轮询间隔，1 ms..1 s |
| `reconnect_interval` | 1000 ms | 未连接或读取失败后的重试间隔，10 ms..1 min |
| `long_press_threshold` | 500 ms | 长按阈值，1 ms..1 h |
| `analog_event_interval` | 8000 us | 模拟消息合并间隔，0..1 s；不延迟数字按键边沿 |
| `left_deadzone` / `right_deadzone` | 0.12 | 径向死区，必须是 `[0,1)` 的有限数 |
| `trigger_press_threshold` | 30 | 扳机达到此值合成为逻辑按下，必须大于释放阈值 |
| `trigger_release_threshold` | 25 | 扳机降至此值合成为逻辑松开 |
| `log_level` | warning | off/error/warning/info/debug |
| `log_to_console` | true | 是否向 stderr 输出模块日志 |
| `log_file` | 空字符串 | 可选 UTF-8 文件路径；空表示禁用文件日志 |
| `log_max_bytes` | 1048576 | 单份日志最大字节数，至少 4096 |
| `log_backups` | 2 | 轮转备份数量，0..10 |

非法配置在构造时抛异常。配置在构造时复制，不提供运行时修改配置接口。

## 事件、状态与退出

- `Event.state` 是生成该事件时的完整状态。`snapshot()` 返回最近采集状态，可能比队列中排队的消息更新。
- `raw` 保留原始整数值；归一化后的摇杆应用径向死区，扳机保留独立 `[0,1]` 数值。`State.buttons` 额外包含合成的 LT/RT 逻辑键。
- 短按顺序为按下、松开、短按。长按只触发一次，释放后不会补发短按。
- 启动或重新连接时已经按着的键没有已知起点，不补发按下/短按/长按；首次松开标为 `initially_held`。
- 断连、后端错误、停止会取消仍按住的键；取消的松开不触发短按。结束时队列内仍可能有取消和停止消息，需要时在 `stop()` 后继续排空。
- `timestamp_ns` 是对象内单调时间，单位纳秒，不是 Unix 时间；`held_ns` 同样是纳秒。硬件 `packet` 不是队列事件序号。
- `sequence` 可发现消息缺失，`dropped_total` 是出队时的累计丢弃量。完整状态只能恢复当前状态，无法补回丢掉的历史短按/长按。
- 析构停止并 join 线程；对象不可复制、不可移动。外部调用线程必须在析构前结束访问，不可一边销毁一边调用成员。

更多线程、时序和性能约定见 [设计文档](design.md)，实机覆盖范围见 [验证记录](validation.md)。

## 常见排查

| 现象 | 检查方式 |
| --- | --- |
| 一直显示断开 | 确认手柄开机、接收器连接和 PC/XInput 模式；多手柄时检查 `--slot` |
| 接收器显示连接，但按键一直为 0 | 连接状态本身不足以证明无线链路正常；操作实体按键确认值会变化 |
| 面板不能打开 | 直接在交互 PowerShell / Windows Terminal 运行；重定向时使用 `--events` 或 `--json` |
| 面板按键松开后变成 `-` | `button` 表示当前按住状态，已完成的短按/长按看 `last action` |
| 操作变慢或队列丢弃增加 | 检查业务回调耗时，及时消费；按需要增加容量或合并模拟消息 |
| 电池显示未知或设备被报告为有线 | 接收器可能不暴露电池信息，不能据此判断实体手柄电量 |
| FN、Turbo、Home 等键没出现 | 标准 XInput 不保证暴露厂商专用键；当前只输出文档化的标准控制项 |
| 退出打印 `disconnected`，但 USB 没拔 | `cancelled=stopped` 表示软件停止时取消输入状态，是正常退出事件 |
