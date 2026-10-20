# gamepad-input-bridge

C++17 手柄输入模块：对象自动管理采集线程、输入状态、按键时序、有界事件队列和日志。当前支持 Windows XInput，提供按型号编译的打印示例；后续 Linux 后端共用相同的事件与队列逻辑。

## 功能

- 构造时启动，析构时停止并等待线程退出；支持显式、重复及并发 `stop()`。
- 一个有界 FIFO，支持 `try_pop`、`wait_pop` 和 `dispatch_pending(lambda)`。Lambda 在消费线程执行。
- 队列预分配；满时丢弃最老消息。事件附序号、累计丢弃数量和完整状态，便于检测丢失和恢复当前状态。
- 原始按键、方向键、双摇杆、双扳机；径向死区归一化、扳机滞回、可配置模拟消息合并。
- 按下、松开、短按、长按、连接、断开、停止和读取错误事件。长按只触发一次，释放不重复触发短按。
- Windows 高精度等待计时器，默认 4 ms 查询；断开时降低查询频率并重试。旧系统会回退普通计时器。
- 可选文件轮转日志、限频溢出告警和运行统计。输入内容由示例打印，模块不逐条写日志。
- 能力与可用的粗略电池信息；后端未提供的信息明确标为未知。

## 编译与运行

需要 CMake 3.20+、C++17 编译器和 Windows SDK 或 MinGW-w64。原生 Windows 后端要求 Windows 8+；高精度计时器要求 Windows 10 1803+。

```powershell
cmake -S . -B build -DGAMEPAD_DEVICE=flydigi_desert_fox
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Visual Studio 生成器的程序通常在 `build\bin\Release\`；Ninja 的程序在 `build\bin\`：

```powershell
.\build\bin\gamepad_flydigi_desert_fox.exe
.\build\bin\gamepad_flydigi_desert_fox.exe --duration 30 --json --log-file capture.log
.\build\bin\gamepad_flydigi_desert_fox.exe --slot 0 --long-press-ms 700 --poll-ms 4
```

默认打印事件，Ctrl+C 退出。`--json` 输出 JSON Lines 到 stdout，日志和退出统计到 stderr。`--help` 查看全部参数。`--stats-only` 只消费并打印最终统计，用于减少终端输出对测量的影响。

`GAMEPAD_DEVICE` 可选 `flydigi_desert_fox` 或 `generic_xinput`，分别生成 `gamepad_flydigi_desert_fox.exe` 和 `gamepad_generic_xinput.exe`。不同型号通过 `src/devices/` 中的配置选择编译目标，共用模块实现。

型号名称用于选择配置，**不是实体身份校验**。XInput 槽位不是固定设备 ID，多手柄时用 `--slot 0..3` 选择当前槽位。当前通过标准 XInput 读取，不依赖飞智空间站。

## 接入自己的程序

在自己的 CMake 工程中：

```cmake
add_subdirectory(path/to/gamepad-input-bridge)
target_link_libraries(your_program PRIVATE gamepad::input)
```

作为子项目时，可先将 `GAMEPAD_BUILD_EXAMPLE` 和 `GAMEPAD_BUILD_TESTS` 设为 `OFF`，只构建库。也可 `cmake --install build --config Release --prefix install`，然后使用 `find_package(GamepadInputBridge CONFIG REQUIRED)` 与 `gamepad::input`。

```cpp
#include <gamepad/gamepad.hpp>
#include <chrono>
#include <iostream>

void example() {
    using namespace std::chrono_literals;
    gamepad::Config config;
    config.queue_capacity = 256;
    config.long_press_threshold = 500ms;
    gamepad::Gamepad input(config); // 自动启动

    auto handle = [](const gamepad::Event& event) {
        std::cout << gamepad::to_string(event.type) << ' '
                  << gamepad::to_string(event.button) << '\n';
    };
    const auto until = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < until) {
        gamepad::Event event;
        if (input.wait_pop(event, 100ms) == gamepad::WaitResult::event) {
            handle(event);
            input.dispatch_pending(handle); // 有上限批量消费；回调在当前线程执行
        }
    }
    // 析构自动停止并 join；不需要手动管理采集线程。
}
```

如果业务本身有主循环，可以只调用 `dispatch_pending(lambda)`。队列接口共用同一个消息流，不提供多消费者广播。不要在 Lambda 内销毁正在派发消息的对象。

完整可运行示例：[examples/main.cpp](examples/main.cpp)。接口：[gamepad.hpp](include/gamepad/gamepad.hpp)。时序、溢出恢复和线程约定：[设计文档](docs/design.md)。

## 手柄兼容列表

同一型号的不同连接方式或模式分别验证。未捕获的控制项不标记通过。

| 品牌 | 型号 | 系统 | 连接方式 | 接口 | 验证状态 | 待验证 |
| --- | --- | --- | --- | --- | --- | --- |
| 飞智 | 沙漠狐 | Windows 11（26100） | USB 无线接收器 | XInput 1.4 | C++ 示例已捕获 ABXY、左右方向、肩键、Start/Back、双摇杆、双扳机及短按/长按 | 上下方向、摇杆按下、物理拔插重连；Linux |

### 飞智沙漠狐

![飞智沙漠狐手柄（用户提供的原图）](docs/images/flydigi-desert-fox.jpg)

型号名称由用户提供，图片作为设备识别参考。设备描述 `Flydigi Dune Fox`，当前模式的 VID:PID 为 `045E:028E`，使用 `xusb22` 驱动。该兼容标识不能用作型号的唯一识别条件。

60 秒实机测试生成并消费 393 条事件，包含 16 次短按、2 次长按；15,001 次查询，丢弃、读取错误和错过期限均为 0。结果只代表该次测试。[测试记录](docs/validation.md)

## 测试与目录

```text
include/gamepad/      公共 C++ 接口
src/core/             事件状态机、队列、日志
src/backends/         原生输入后端（当前 Windows XInput）
src/platform/         平台等待/停止机制
src/devices/          型号编译配置
examples/main.cpp    Lambda 消费并打印事件的示例
tests/               确定性时序测试、线程测试、微基准
```

自动测试不依赖连接手柄，覆盖短按、长按、断连取消、重连、扳机滞回、队列并发溢出、回调异常、日志轮转和生命周期等。微基准在构建后运行 `gamepad_benchmark`。

Linux 原生输入后端尚未实现。可用以下命令在 Linux 构建、测试共用逻辑：

```sh
cmake -S . -B build -DGAMEPAD_BUILD_EXAMPLE=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

[Windows 接口调研](docs/windows-input-research.md)记录了 XInput/HID/Switch 的区别及初始检测过程。
