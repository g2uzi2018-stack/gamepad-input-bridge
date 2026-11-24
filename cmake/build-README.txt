手柄输入桥构建区

从项目根目录执行以下命令。日常使用 build/，各型号面板和通用 XInput 面板共用同一构建目录。
本说明由 CMake 从 cmake/build-README.txt 复制，修改原稿后重新配置即可更新。

build/  Windows Release
  源码在 src/ 和 include/gamepad/，面板入口在 examples/main.cpp，型号配置在 src/devices/。
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --config Release --parallel
  默认生成飞智面板、gamepad::input 对应的静态库和测试程序。
  cmake --build build --config Release --target gamepad_easysmx_d15_dune_8k
  cmake --build build --config Release --target gamepad_flydigi_direwolf_4
  cmake --build build --config Release --target gamepad_generic_xinput
  面板位于 bin/（Ninja）或 bin/Release/（Visual Studio），均相对于当前构建目录。
  GAMEPAD_DEVICE 选择默认构建及安装的面板。其他型号可按目标名构建。

build/debug/  Windows 单配置生成器的调试构建
  同一份源码，以 Debug 编译库、面板和 tests/tests.cpp，供调试和断言检查使用。
  cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
  cmake --build build/debug --parallel
  本机 MinGW 调试配置另外使用 -DCMAKE_CXX_FLAGS="-Werror -D_GLIBCXX_ASSERTIONS"。
  Visual Studio 可直接在 build/ 使用 --config Debug。

build/linux-core/  Linux 共用核心
  编译 src/core/、非 Windows 平台代码和测试。此配置关闭 Windows 面板。
  cmake -S . -B build/linux-core -DCMAKE_BUILD_TYPE=Debug -DGAMEPAD_BUILD_EXAMPLE=OFF
  cmake --build build/linux-core --parallel
  ctest --test-dir build/linux-core --output-on-failure

build/consumer/  安装包接入验证
  tests/consumer/ 是独立消费工程，使用 install/ 导出的包构建 consumer。
  生成与运行命令见 tests/README.txt。日常读取手柄时只需 build/ 中的面板。
