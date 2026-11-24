# Windows 输入接口与设备识别

USB 设备可以同时暴露 XUSB 和 HID。项目通过 XInput 读取标准控制项，保留独立左右扳机。枚举时同一设备的 HID 子接口可能也显示为游戏手柄，判断设备数量时应核对 USB 父设备。

## 识别当前连接

1. 查看当前在线 USB 设备的产品描述、VID/PID、驱动和父设备。飞智沙漠狐和冰原狼 4 均报告 `045E:028E`，产品描述分别为 `Flydigi Dune Fox` 和 `Flydigi Direwolf 4`。EasySMX 接收器的 HID 制造商字符串为 `EasySMX D15 Dune 8K`，产品名为 `Xbox 360 Controller`，标识为 `413D:2131`。三款设备的 XInput 接口在已测模式下均使用 `xusb22`。
2. 读取 XInput 槽位 0 至 3，找到本次会话中能返回状态的槽位。VID/PID 和系统显示的兼容名称可能被多个零售型号共用，型号识别还需参考设备字符串或实体标识。
3. 操作实体按键、摇杆或扳机，在面板观察相应值变化。无线接收器可以在手柄离线时继续枚举，静止的一帧全零输入只证明当时能读取该槽位。[按型号整理的实测结果](validation.md)

接收器暴露给 Windows 的接口与手柄的空中协议属于不同层。本项目观察的是 Windows 输入状态。

## 常见接入路径

| 设备当前模式 | Windows 应用接入方式 | 实现注意事项 |
| --- | --- | --- |
| Xbox / XInput 兼容模式 | XInput | 标准按键、双摇杆、独立左右扳机；不能假定所有额外功能键和传感器都暴露出来 |
| 通用 HID 手柄模式（常被称为 DInput 模式） | DirectInput，或 Raw Input / HID 解析接口 | 依据设备描述与实测确定按键、轴、Hat Switch 映射，不假定各型号报文一致 |
| Switch Pro 兼容模式 | 支持该设备的 HID 实现，例如 SDL 的 Switch 后端 | 有专用报文、子命令与 USB 初始化流程；“兼容 Switch”不能证明使用 Xbox 报文，也不能证明兼容所有原厂扩展命令 |

微软文档说明，XUSB 同时暴露 XInput 和 HID 兼容接口。通过旧的 DirectInput/HID 映射访问时，左右扳机可能被合并到同一轴。因此当前设备优先走 XInput，并避免把它的两种接口当成两只手柄。

## 资料

- [Microsoft：XInput 与 DirectInput 功能比较](https://learn.microsoft.com/en-us/windows/win32/xinput/xinput-and-directinput)
- [Microsoft：DirectInput 与 XUSB 设备及 HID 映射](https://learn.microsoft.com/en-us/windows/win32/xinput/directinput-and-xusb-devices)
- [Microsoft：XInputGetState](https://learn.microsoft.com/en-us/windows/win32/api/xinput/nf-xinput-xinputgetstate)
- [USB-IF：HID 规范与 Usage Tables](https://usb.org/hid)
- [SDL：Switch HID 后端源码](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_switch.c)

资料用于解释接口和指导实现；具体手柄能力仍以本机实测为准。
