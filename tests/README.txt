从仓库根目录执行。下列入口按改动选择。

核心行为测试（tests.cpp）
  cmake --build build --config Release --target gamepad_tests
  ctest --test-dir build -C Release --output-on-failure
  默认 GAMEPAD_BUILD_TESTS=ON。测试分组由 gamepad_tests 逐项输出。
  · 输入时序和模拟量：边沿顺序、长按阈值、初始按住与取消、扳机滞回、死区和合并。
    修改 src/core/event_engine.hpp 或输入状态含义时执行。
  · 队列与线程：溢出丢旧、数据一致性、回调异常、并发停止、等待唤醒和重连。
    修改队列、采集循环、平台等待或对象生命周期时执行。
  · 配置与日志：参数校验、文件轮转、备份数量和打开失败。修改相关实现时执行。
  这些测试用可控输入检查软件行为。设备识别和物理操作用实际面板验证。
  Linux 核心测试使用 build/linux-core，配置与命令见 cmake/build-README.txt。
  Linux 额外执行槽位热插拔回归。测试构造两个内核槽位及不同的 event 节点，验证移除、重建 js0 时 js1 仍选择同一设备。该项使用临时目录，可在 CI 和普通账号下运行。

安装包接入（consumer/）
  使用与主库相同的编译器、架构及配置。Windows 上的 Ninja / Release 示例：
  cmake --build build --config Release
  cmake --install build --config Release --prefix install
  cmake -S tests/consumer -B build/consumer -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/install"
  cmake --build build/consumer --config Release
  .\build\consumer\consumer.exe
  修改安装、导出或链接依赖时执行。独立工程通过 find_package 获取 gamepad::input，
  构建并运行一个订阅和停止采集的调用方，正常退出码为 0。

性能测量（benchmark.cpp）
  cmake --build build --config Release --target gamepad_benchmark
  .\build\bin\gamepad_benchmark.exe
  用于比较事件处理及队列开销。Visual Studio 的 Release 可执行文件在 bin/Release/。
  合成吞吐量只衡量软件内部路径。实体手柄覆盖范围见 docs/validation.md。
