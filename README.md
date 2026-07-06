# 车牌识别雷达测速摄像机管理软件

本项目是一个基于 C++17 和 Qt 6 Widgets 的 Windows 桌面端上位机调试工具。

当前进度：第 1 阶段，界面骨架。

已完成内容：

- CMake + Qt Widgets 工程骨架
- `MainWindow` 主界面
- 顶部工具栏
- 左侧设备列表和设备属性表
- 中间主预览画面和抓拍画面占位区
- 下方抓拍记录表
- 底部状态栏
- 设备右键菜单框架
- 工具栏基础响应

后续阶段将继续实现 Model/View 数据模型、模拟设备、视频自绘预览、配置系统、日志和导出能力。

## 构建方式

确保本机已安装 Qt 6 和 CMake，并让 CMake 能找到 Qt 6：

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvcxxxx_64"
cmake --build build --config Release
```

如果使用 Qt Creator，直接打开本目录的 `CMakeLists.txt` 即可。
