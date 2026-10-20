# Windows 输入接口调研

记录日期：2026-10-20。本文记录接口选择依据和设备检测结果，不代表所有功能已验证。

## 飞智沙漠狐：本机观察

- 系统：Windows 11，系统版本 10.0.26100。
- 连接方式：用户确认使用 USB 无线接收器。
- USB 总线产品描述：`Flydigi Dune Fox`。
- Windows 设备名称：支持 Windows 的 XBOX 360 手柄。
- USB VID:PID：`045E:028E`；设备修订号：`0110`。
- 驱动服务：`xusb22`。父设备为真实 USB 根集线器，排除了将本机虚拟手柄总线误认为该设备的情况。
- HID 兼容接口：Usage Page `0x01`，Usage `0x05`（Game Pad）。
- 调用系统 `xinput1_4.dll`：槽位 0 的 `XInputGetState` 返回成功，其余槽位未连接。
- 一次状态快照中，按键位图、左右扳机和两侧摇杆均为 0。已确认 API 能返回状态，尚未验证这些值是否随实体操作变化。

首次检测时用户表示手柄尚未连上，因此当时仅凭接收器枚举成功和 XInput 返回成功，不能证明无线链路已建立。随后用户开机连接手柄，并在采样窗口中操作，已捕获真实输入变化。

### 连接后的基础输入实测

使用临时诊断脚本，以约 10 ms 的采样间隔调用 XInput，采样窗口为 60 秒。实际采样间隔受系统调度影响，不代表设备回报率。本次记录到 105 个不同状态（含首个状态），所有记录均读取成功。

| 控制项 | 本次观察 |
| --- | --- |
| 按键 | A、B、Y 有按下与释放变化；其他按键不在本次验证范围 |
| 左扳机 | 最小 0，最大 255 |
| 右扳机 | 最小 0，最大 255 |
| 左摇杆 X / Y | X：-32768 至 768；Y：-32768 至 32000 |
| 右摇杆 X / Y | X：-29184 至 32767；Y：-23040 至 8192 |

摇杆数值只是本次操作经过的范围，不能据此判断完整量程、精度或校准质量。已确认左右摇杆的两个轴和左右独立扳机都有变化；尚未逐项验证方向键、其余按键、长短按事件和断连行为。正式 C++ 程序尚未实现。

`045E:028E` 是兼容 Xbox 360 的身份标识，不应当作为飞智型号的唯一识别条件。后续设备选择应结合产品描述和设备路径；XInput 槽位只用于当前会话，不能当作固定设备身份。

## 应区分的三层

1. 手柄与接收器之间的无线链路：本次没有分析空中报文，不能据此确认其无线协议。
2. 接收器向电脑暴露的设备接口：当前观察为 Xbox 360 兼容的 XUSB 接口，同时存在 HID 兼容接口。
3. 应用调用的 Windows API：首期采用 XInput 读取当前模式下的标准控制项。

XInput、DirectInput 是应用接口，USB HID 是设备类规范。日常所说的“XInput 协议模式”，在实现中需要区分设备侧接口和应用侧 API。

## 常见接入路径

| 设备当前模式 | Windows 应用接入方式 | 实现注意事项 |
| --- | --- | --- |
| Xbox / XInput 兼容模式 | XInput | 标准按键、双摇杆、独立左右扳机；不能假定所有额外功能键和传感器都暴露出来 |
| 通用 HID 手柄模式（常被称为 DInput 模式） | DirectInput，或 Raw Input / HID 解析接口 | 依据设备描述与实测确定按键、轴、Hat Switch 映射，不假定各型号报文一致 |
| Switch Pro 兼容模式 | 支持该设备的 HID 实现，例如 SDL 的 Switch 后端 | 有专用报文、子命令与 USB 初始化流程；“兼容 Switch”不能证明使用 Xbox 报文，也不能证明兼容所有原厂扩展命令 |

微软文档说明，XUSB 同时暴露 XInput 和 HID 兼容接口。通过旧的 DirectInput/HID 映射访问时，左右扳机可能被合并到同一轴。因此当前设备优先走 XInput，并避免把它的两种接口当成两只手柄。

## C++ 工程建议（尚未实现）

采用 C++17 与 CMake，每个型号提供一个可执行目标，共用采集与事件逻辑。

```text
CMakeLists.txt
src/
  core/                 # 统一状态、事件、时间戳、短按与长按判断
  backends/windows/     # 首期 XInput；按实际需要增加 HID 或 DirectInput
  devices/              # 各型号的识别条件、映射和专有行为
  app/                  # 共用入口、命令行与数据输出
```

首个目标建议为 `gamepad_flydigi_desert_fox`，生成 `gamepad_flydigi_desert_fox.exe`。共用入口在构建时选择对应型号配置，无需复制整套实现。

长短按由单调时钟和按键边沿生成，不依赖硬件直接报告。后续实现须明确长按阈值、长按触发次数，以及长按释放后是否还产生短按事件；断连应清理按住状态。

## 资料

- [Microsoft：XInput 与 DirectInput 功能比较](https://learn.microsoft.com/en-us/windows/win32/xinput/xinput-and-directinput)
- [Microsoft：DirectInput 与 XUSB 设备及 HID 映射](https://learn.microsoft.com/en-us/windows/win32/xinput/directinput-and-xusb-devices)
- [Microsoft：XInputGetState](https://learn.microsoft.com/en-us/windows/win32/api/xinput/nf-xinput-xinputgetstate)
- [USB-IF：HID 规范与 Usage Tables](https://usb.org/hid)
- [SDL：Switch HID 后端源码](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_switch.c)

资料用于解释接口和指导实现；具体手柄能力仍以本机实测为准。
