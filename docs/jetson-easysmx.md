# Jetson 上的 EasySMX USB 接收器

这台 Jetson AGX Orin 运行 Ubuntu 22.04.5 和 `5.15.148-rt-tegra`。系统有 evdev，缺少手柄协议驱动。EasySMX D15 Dune 8K 接收器以 `413d:2131` 枚举，接口 0 的描述符为 `ff/5d/01`。本机安装的 `xpad_easysmx` 模块只匹配这个 USB ID 和接口类型，其余 HID 接口由 `usbhid` 处理。

## 读取输入

在这台 Jetson 的 `ti5robot` 桌面打开应用列表，搜索 `EasySMX`，点击 **EasySMX Gamepad Test** 即可打开与 Windows 相同的实时面板。在 Jetson 终端或交互式 SSH 会话中也可以直接启动：

```sh
/opt/gamepad-input-bridge/bin/gamepad_easysmx_d15_dune_8k --live
```

面板默认显示槽位 0。按键时看 `button` 和 `last action`，转动摇杆看左右 X/Y 原始值，扣扳机看 LT/RT 的 0–255 进度条。按 Ctrl+C 退出。需要逐行采样时使用 `--events`。

EasySMX 专用驱动将四个摇杆轴的 `fuzz` 设为 0，让回中时最后几个计数的变化也能传给面板。驱动按 Xbox 报告格式翻转 Y 轴，evdev 中的 Y 中心为 -1，项目还原为 0。面板保留接收器实际报告的小位移；用于控制的归一化输出另行应用项目的 12% 径向死区。

查看 USB 识别、驱动和手柄节点时可用以下命令。节点编号会随插拔变化。权限规则仅作用于这个接收器的游戏手柄 event 节点，`kuang` 和属于 `plugdev` 的 `ti5robot` 均可读取。

```sh
lsusb -d 413d:2131
modinfo -n xpad_easysmx
lsusb -t
ls -l /dev/input/by-id/*EasySMX*event-joystick
```

## 复现驱动构建

在项目根目录操作。需要 CMake 3.20+、C/C++ 编译器、make、binutils、kmod、curl，以及与正在运行的内核包完全匹配的 NVIDIA RT 头文件（含 `Module.symvers`）。先用 `uname -r` 和 `dpkg-query -W nvidia-l4t-rt-kernel nvidia-l4t-rt-kernel-headers` 核对。本机的系统手柄编号由 joydev 提供；只有 evdev 时，可给示例传 `--device-path` 和实际的 `*-event-joystick` 路径。

本机 RT 内核包和头文件同为 `36.4.0-20241014173246`。构建脚本校验 Linux stable v5.15.148 的 `xpad.c` 哈希，加入型号和精确的 USB 匹配项，再用当前 RT 内核的头文件及符号版本表编译。构建目录必须是新目录。已部署的源文件位于 Jetson 的 `/usr/local/src/gamepad-xpad-easysmx-5.15.148-rt-tegra/`。[Linux 外部模块构建](https://docs.kernel.org/5.15/kbuild/modules.html) · [NVIDIA R36.4 内核说明](https://docs.nvidia.com/jetson/archives/r36.4/DeveloperGuide/SD/Kernel/KernelCustomization.html)

```sh
mkdir -p build
sh drivers/jetson-easysmx/build-module.sh "$PWD/build/easysmx-rt-module"
```

默认从上游下载源码；离线构建可传第二个参数，指向已保存的 `xpad.c.orig`。安装的模块只适用于当前内核构建，更新内核后需要针对新版本重新核对驱动支持、头文件和符号版本。

在相同 RT 内核的新机器上，首次安装可使用以下命令。先将接收器拔出，确认 `kuang` 用户和 `plugdev` 组存在，或改为当地实际的账号和组。模块匹配别名会在下次插入接收器时触发加载。

```sh
k=$(uname -r)
sudo install -D -m 0644 build/easysmx-rt-module/xpad_easysmx.ko "/lib/modules/$k/updates/xpad_easysmx.ko"
sudo depmod -a "$k"
echo 'SUBSYSTEM=="input", KERNEL=="event*", ENV{ID_INPUT_JOYSTICK}=="1", ATTRS{idVendor}=="413d", ATTRS{idProduct}=="2131", OWNER="kuang", GROUP="plugdev", MODE="0660"' | sudo tee /etc/udev/rules.d/72-easysmx-gamepad.rules
sudo udevadm control --reload-rules
```

排查时先用 `lsusb -t` 确认手柄接口绑定，再检查 `*-event-joystick` 的读权限，最后以实际使用账号运行 `--live` 或 `--json`。USB 列表里出现接收器只能证明系统识别了 USB 设备；接口 0、游戏手柄节点和项目输入事件分别验证后续链路。

安装 C++ 程序及 GNOME 桌面入口时，用实际的桌面账号执行以下命令。桌面入口使用 GNOME Terminal。

```sh
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release -DGAMEPAD_DEVICE=easysmx_d15_dune_8k
cmake --build build/linux --parallel
sudo cmake --install build/linux --prefix /opt/gamepad-input-bridge
install -Dm644 drivers/jetson-easysmx/gamepad-easysmx-test.desktop "$HOME/.local/share/applications/gamepad-easysmx-test.desktop"
```

## 撤回本机安装

退出输入程序后，可卸载模块并移除自动匹配和设备权限配置。以下路径对应这次安装的内核版本。

```sh
sudo modprobe -r xpad_easysmx
sudo rm -f /lib/modules/5.15.148-rt-tegra/updates/xpad_easysmx.ko
sudo depmod -a 5.15.148-rt-tegra
sudo rm -f /etc/udev/rules.d/72-easysmx-gamepad.rules
sudo udevadm control --reload-rules
rm -f "$HOME/.local/share/applications/gamepad-easysmx-test.desktop"
```

这次部署的 C++ 程序及开发库位于独立目录 `/opt/gamepad-input-bridge/`。该目录和上述 `/usr/local/src/` 下的驱动源码可单独移除。

## Linux 手柄振动

本机模块使用同一份上游 xpad 源码的 CONFIG_JOYSTICK_XPAD_FF 分支，依赖内核 ff-memless。读取节点在 sysfs 的 capabilities/ff 应声明 FF_RUMBLE。库提供 gamepad/evdev_rumble.hpp，使用 gamepad::EvdevRumble(持久 event 路径)，调用 pulse(strong, weak, duration)。两路幅度为 0–65535，单次时长 1–1000 ms；成功表示内核接受上传和播放，设备端到时停止。接收器重新枚举后按路径重开。旧输入专用模块会返回不支持，采集仍可用。

使用预设时，长确认使用双马达满幅 500 ms，短确认 130 ms，轻提示 90 ms，边界提示 160 ms。现场使用短确认提示开启，长确认提示关闭或速度档位端点。每次调用播放一次，调用者决定哪一次状态变化触发提示。

```cpp
gamepad::EvdevRumble feedback(event_path);
auto error = feedback.pulse(gamepad::rumble::long_confirmation);
```

其余预设为 `gamepad::rumble::short_confirmation`、`gamepad::rumble::step`、`gamepad::rumble::limit`。调用方可检查返回的 `std::error_code` 判断内核是否受理。
