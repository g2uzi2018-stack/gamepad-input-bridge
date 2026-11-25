# gamepad-input-bridge

在 Windows 上通过 XInput、在 Linux 上通过 evdev 读取手柄。可以直接运行实时面板，也可以在 C++17 程序中链接 `gamepad::input`，消费按键、摇杆、扳机和连接事件。

## 编译与运行

Jetson / Linux 使用支持 C++17 的编译器和 Linux 输入头文件。从项目根目录构建 EasySMX 示例：

```sh
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release -DGAMEPAD_DEVICE=easysmx_d15_dune_8k
cmake --build build/linux --parallel
build/linux/bin/gamepad_easysmx_d15_dune_8k --events
```

EasySMX 接收器在 Jetson 上需要驱动创建游戏手柄设备节点。已安装的程序路径、驱动来源和撤回方式见 [Jetson 接入说明](docs/jetson-easysmx.md)。

Windows 构建需要 CMake 3.20 或以上、支持 C++17 的编译器，以及 Windows SDK（MSVC）或 MinGW-w64。Windows 原生后端使用系统的 XInput 1.4，支持 Windows 8 及以上。此机使用便携 w64devkit，工具位于 `work/toolchain/w64devkit/bin/`。

在 PowerShell 中使用 Ninja 构建并启动 EasySMX 面板。若使用本机便携工具链，先执行 `$env:Path = (Resolve-Path work/toolchain/w64devkit/bin).Path + ';' + $env:Path`。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target gamepad_easysmx_d15_dune_8k --parallel
.\build\bin\gamepad_easysmx_d15_dune_8k.exe
```

接上接收器并按键，面板应显示 `CONNECTED` 和相应的键名。默认读取槽位 0，多手柄可加 `--slot 0..3` 选择当前槽位。按 Ctrl+C 退出。用 `--events` 逐行查看事件，用 `--json` 输出 JSON Lines。

同一构建目录可按目标名生成下表中的任意面板。直接执行 `cmake --build build --config Release` 会构建库、测试和默认飞智面板。`-DGAMEPAD_DEVICE=<配置名>` 选择默认构建及安装的面板。Visual Studio 生成器省略 `-G Ninja -DCMAKE_BUILD_TYPE=Release`，构建时加 `--config Release`，程序位于 `build/bin/Release/`。不同生成器各用独立目录。

## 手柄兼容列表

| 手柄 | CMake 配置名 | Windows 11 实机结果 |
| --- | --- | --- |
| EasySMX D15 Dune 8K | `easysmx_d15_dune_8k` | USB 接收器，已捕获 A/B/X、双肩键、双摇杆、独立双扳机和短按 |
| 飞智沙漠狐 | `flydigi_desert_fox` | USB 接收器，已捕获 ABXY、左右方向、肩键、Start/Back、双摇杆、独立双扳机和短按/长按 |
| 飞智冰原狼 4 | `flydigi_direwolf_4` | USB XInput，已捕获双摇杆 X/Y、ABXY、方向下、左右肩键、摇杆按下与 LT，背键见实测记录 |
| 通用 XInput | `generic_xinput` | 可读取标准 XInput 槽位，具体控制项按设备和连接模式验证 |

构建目标名为 `gamepad_<配置名>`。型号配置选择默认参数，实际设备由所选平台的槽位决定。Jetson 已实测 EasySMX USB 接收器。各型号的标识、轴范围和待测控制项见[实机验证](docs/validation.md)。[飞智沙漠狐参考照片](docs/images/flydigi-desert-fox.jpg)

## 项目入口

| 要做的事 | 入口 |
| --- | --- |
| 调整面板显示、选择输出、采样或排查断连 | [使用指南](docs/user-guide.md) · [面板与命令行](examples/main.cpp) |
| 接入自己的 CMake/C++ 工程 | [安装与链接](docs/user-guide.md#cmake-接入) · [消费事件示例](examples/consume_events.cpp) · [公共接口](include/gamepad/gamepad.hpp) |
| 修改型号、平台读取或按键时序 | 型号在 `src/devices/`，采集后端在 `src/backends/`，队列和事件逻辑在 `src/core/`。[线程与事件设计](docs/design.md) · [Windows 接口选择](docs/windows-input-research.md) |
| 选择构建或运行测试 | [构建区说明原稿](cmake/build-README.txt) · [测试入口](tests/README.txt)。配置时会在 `build/README.txt` 生成一份构建说明。 |
| 找本机采样和诊断 | JSONL 原始事件在 `runs/<日期>/<批次>/`，诊断日志在 `log/<日期>/`。默认不写文件。[采样命令](docs/user-guide.md#输出与日志) |

运行时参数由 C++ 的 `Config` 或面板命令行传入。库构造时启动采集，析构或显式 `stop()` 时停止。事件队列满时丢弃最老消息，消费者可用事件序号、丢弃计数和完整状态恢复当前输入。

Linux 消费者可用 `gamepad::EvdevRumble` 通过同一接收器的 event 路径请求硬件定时脉冲，也可直接使用长振、短振、换档和触限预设。见[Jetson 接入说明](docs/jetson-easysmx.md)。
