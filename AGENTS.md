# 工程维护约定

- 从 [README](README.md) 找构建、运行和文档入口。通用约定来自 Notion 的[《AI 工程开发》](https://app.notion.com/p/4159f9a4e09e812e861aef2c51155df8)。
- Windows 面板共用 `examples/main.cpp` 和 XInput 后端。新增型号时先核对实际接口及实体输入，标准映射沿用现有后端，在 `src/devices/` 维护型号配置。
- 公共接口在 `include/gamepad/`，事件时序和队列在 `src/core/`。平台差异留在 `src/backends/` 和 `src/platform/`。
- 构建使用 `build/`，需要不同工具链或调试配置时在其下分目录。改构建入口或产物位置时同步更新 [构建说明原稿](cmake/build-README.txt)、README 和实际调用方。
- 本机采样按批次保存于 `runs/<日期>/`，诊断日志保存于 `log/<日期>/`。原始记录与旧发布快照独立于构建缓存，整理时保留。采样与自动测试均按[测试说明](tests/README.txt)选择相关入口。
- 接口、参数或行为变化时同步修改对应说明。型号兼容结论只覆盖实际连接模式和捕获的控制项，逐项结果维护在[验证记录](docs/validation.md)。交付时说明可运行入口、实际验证范围及行为约束变化。
