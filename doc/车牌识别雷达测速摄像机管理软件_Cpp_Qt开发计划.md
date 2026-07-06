# 车牌识别雷达测速摄像机管理软件 C++/Qt 开发计划

## 1. 项目定位

本项目目标是使用 **C++/Qt** 直接开发一个 Windows 桌面端上位机调试工具，用于管理车牌识别雷达测速一体化摄像机。

软件本身不负责实现车牌识别算法、雷达测速算法、违法判断底层算法。这些能力默认由前端嵌入式设备完成。上位机软件主要负责：

- 设备发现、添加、连接、断开
- 实时视频预览
- 车牌识别结果展示
- 雷达测速结果展示
- 抓拍记录管理
- 单设备参数配置
- 批量设备运维
- 抓拍图片、录像、日志、配置文件本地保存
- 后续对接真实设备 SDK、HTTP、TCP、UDP、RTSP 等接口

逆向参考边界采用黑盒复刻方式：根据安装版软件界面、使用说明、功能需求文档和操作行为重建功能，不破解、不绕过授权、不复制原软件代码。

第一阶段仍建议先使用 **模拟设备层**，但工程结构直接按真实设备接入设计。这样既能降低硬件联调风险，又避免后期从 Python/PySide6 迁移到 C++/Qt 的大规模重写。

## 2. 推荐技术栈

| 模块 | 推荐方案 |
|---|---|
| UI 框架 | Qt 6 Widgets |
| 编程语言 | C++17 或 C++20 |
| 构建系统 | CMake |
| IDE | Qt Creator 或 Visual Studio + Qt 插件 |
| 配置保存 | QSettings，生成 `.ini` 文件 |
| 表格模型 | QAbstractTableModel |
| 视频显示 | QWidget 自绘，后续可升级 QOpenGLWidget |
| 图像处理 | OpenCV，可选 |
| 日志系统 | Qt 日志系统或 spdlog |
| 数据导出 | CSV 优先，Excel 后续扩展 |
| 网络通信 | QTcpSocket、QUdpSocket、QNetworkAccessManager |
| 多线程 | QThread、QtConcurrent |
| 打包部署 | windeployqt + NSIS/Inno Setup |

建议第一版使用 **Qt Widgets**，不优先使用 QML。该软件属于传统工业调试工具，Qt Widgets 更适合表格、树形参数、右键菜单、分栏布局和 Windows 桌面软件风格。

## 3. 总体架构

推荐工程结构如下：

```text
CameraManagerApp/
├─ CMakeLists.txt
├─ src/
│  ├─ main.cpp
│  ├─ app/
│  │  ├─ ApplicationContext
│  │  ├─ AppSettings
│  │  └─ Logger
│  ├─ ui/
│  │  ├─ MainWindow
│  │  ├─ DeviceConfigDialog
│  │  ├─ GlobalSettingsDialog
│  │  ├─ CalibrationDialog
│  │  └─ widgets/
│  ├─ models/
│  │  ├─ Device
│  │  ├─ DeviceStatus
│  │  ├─ DeviceConfig
│  │  ├─ CaptureRecord
│  │  └─ table_models/
│  ├─ services/
│  │  ├─ DeviceManager
│  │  ├─ CaptureRecordService
│  │  ├─ StorageService
│  │  ├─ ExportService
│  │  └─ OperationLogService
│  ├─ device/
│  │  ├─ IDeviceClient
│  │  ├─ MockDeviceClient
│  │  └─ RealDeviceClient
│  ├─ video/
│  │  ├─ VideoFrame
│  │  ├─ VideoRenderer
│  │  ├─ MockVideoSource
│  │  └─ RtspVideoSource
│  └─ resources/
└─ config/
```

核心原则：

- UI 不直接操作设备协议。
- 所有设备操作通过 `IDeviceClient`。
- 所有设备由 `DeviceManager` 管理。
- 所有抓拍记录由 `CaptureRecordService` 管理。
- 所有文件路径、图片保存、CSV 导出由服务层完成。
- 模拟设备和真实设备使用同一套接口。
- 后续接入真实设备时，尽量只替换 `RealDeviceClient` 和视频源，不重写主界面。

## 4. 核心模块拆解

### 4.1 工程基础与主界面

目标：先搭出和参考软件类似的工业软件界面。

需要实现：

- `MainWindow`
- 顶部工具栏
- 左侧设备列表
- 左下设备属性表
- 中间双画面预览区
- 下方抓拍记录表
- 底部状态栏
- 单设备右键菜单框架
- 基础图标资源

建议使用：

- `QMainWindow`
- `QToolBar`
- `QSplitter`
- `QTableView`
- `QTreeView`
- `QStatusBar`
- `QMenu`
- `QAction`

验收标准：

- 软件可启动。
- 主界面布局接近参考截图。
- 窗口缩放时布局稳定。
- 工具栏按钮有基础响应。
- 空设备、空记录状态正常。

### 4.2 数据模型与表格模型

目标：先把软件的数据骨架搭稳。

需要定义：

- `Device`
- `DeviceStatus`
- `DeviceConfig`
- `CaptureRecord`
- `OperationLog`

表格模型：

- `DeviceTableModel`
- `DevicePropertyModel`
- `CaptureRecordTableModel`

建议使用 `QAbstractTableModel`，不要长期依赖 `QTableWidget`。`QAbstractTableModel` 更适合后续大量设备、大量抓拍记录和数据刷新。

验收标准：

- 可显示多台模拟设备。
- 点击设备后刷新属性表。
- 抓拍记录表支持新增、删除、清空。
- 表格字段和需求文档一致。

### 4.3 模拟设备系统

目标：没有真实硬件时也能跑完整流程。

建议定义统一接口：

```cpp
class IDeviceClient : public QObject {
    Q_OBJECT

public:
    explicit IDeviceClient(QObject* parent = nullptr);
    virtual ~IDeviceClient() = default;

    virtual bool connectDevice() = 0;
    virtual void disconnectDevice() = 0;
    virtual DeviceStatus readStatus() = 0;
    virtual DeviceConfig readConfig() = 0;
    virtual bool writeConfig(const DeviceConfig& config) = 0;
    virtual bool reboot() = 0;
    virtual bool syncTime(const QDateTime& time) = 0;
    virtual bool triggerCapture() = 0;

signals:
    void statusChanged(const DeviceStatus& status);
    void captureGenerated(const CaptureRecord& record);
    void errorOccurred(const QString& message);
};
```

第一阶段实现：

- `MockDeviceClient`
- 定时生成在线状态
- 定时生成车牌、车速、颜色、方向
- 模拟超速车辆
- 模拟未知车牌
- 模拟黑名单车辆
- 模拟离线、连接失败、参数下发失败

验收标准：

- 软件能添加多台模拟设备。
- 设备可连接、断开。
- 可手动触发抓拍。
- 抓拍记录自动进入列表。
- 设备状态周期刷新。

### 4.4 视频预览与画面叠加

目标：实现主实时画面和副抓拍画面。

第一阶段可以不接真实 RTSP，先实现：

- 生成模拟道路画面
- 或加载本地视频、图片作为模拟源
- 绘制车牌框
- 绘制车辆框
- 绘制画面叠加文字

叠加字段包括：

- 地点
- 通行方向
- 抓拍时间
- 车牌号码
- 车牌颜色
- 实时车速
- 道路限速值
- 设备运行状态
- 8 位防伪码

推荐实现：

- `VideoWidget : public QWidget`
- 重写 `paintEvent`
- 使用 `QPainter` 绘制图像、红框、文字
- 后续性能不足再切换为 `QOpenGLWidget`

验收标准：

- 主画面能实时刷新。
- 副画面显示最近一次抓拍。
- 车牌框、车辆框和文字叠加清晰。
- 切换设备时画面和记录同步切换。

### 4.5 设备配置系统

目标：完成核心配置弹窗。

配置弹窗结构：

- 左侧树形分组
- 右侧参数编辑区
- 底部按钮：功能、确定、取消

配置分组：

- 相机
- 安全
- 硬件
- 算法
- 高级选项

关键逻辑：

- 在线设备可编辑。
- 离线设备只读或置灰。
- 点击确定后调用 `writeConfig()`。
- 修改失败要提示错误。
- 修改成功后保存到 `.ini`。
- 支持配置模板导入、导出、批量下发。

验收标准：

- 可打开单设备配置。
- 参数能读取、修改、保存。
- 重启软件后配置仍存在。
- 离线设备不可编辑。

### 4.6 全局设置系统

目标：完成顶部齿轮配置。

全局设置包括：

- 启动最大化
- 启动自动连接设备
- 自动侦听设备数据
- 全局字体、字号
- 抓拍列表缓存条数
- 视频预览帧率
- 抓拍保存路径
- 是否保存车牌小图
- 是否生成文本信息文件
- 自动清理过期文件
- 定时同步设备时间

推荐实现：

- `GlobalSettingsDialog`
- `AppSettings`
- `QSettings`

验收标准：

- 设置能保存到 `.ini`。
- 重启软件后自动加载。
- 修改保存路径后抓拍文件进入新目录。

### 4.7 本地存储、日志、导出

目标：让 demo 具备真实工具软件的使用闭环。

需要实现：

- 抓拍图片保存
- 车牌小图保存
- 抓拍记录 CSV 导出
- 设备台账 CSV 导出
- 操作日志保存
- 打开本地素材目录
- 清理缓存

推荐目录：

```text
data/
├─ captures/
│  ├─ normal/
│  ├─ overspeed/
│  ├─ unknown/
│  └─ blacklist/
├─ videos/
├─ logs/
├─ exports/
└─ config/
```

验收标准：

- 每条抓拍记录对应本地文件。
- CSV 中文不乱码，建议 UTF-8 BOM。
- 操作日志能追踪连接、断开、配置、导出、批量操作。
- 可一键打开素材目录。

### 4.8 运维工具与批量操作

目标：补齐工业调试软件常见操作。

功能包括：

- 批量重启
- 批量同步时间
- 批量触发抓拍
- 批量导出设备信息
- 清空设备列表
- 批量下发预设配置
- 单设备右键菜单完整化

右键菜单包括：

- 连接
- 断开
- 配置
- 重启相机
- 同步时间
- 网络信息
- 设置 IP
- U 盘状态
- 升级固件，占位
- 打开本地文件夹
- 清理缓存
- 读取硬件运行状态

验收标准：

- 批量操作对在线设备生效。
- 离线设备跳过并记录失败原因。
- 操作结果有日志。
- 用户界面不会卡死。

### 4.9 真实设备接入预留

目标：为后续真机接入做准备。

需要提前保留：

- `RealDeviceClient`
- `RtspVideoSource`
- `DeviceProtocolAdapter`
- 超时、重试、错误码
- 设备 SDK 封装层
- 协议日志开关

可能接入方式：

- RTSP：实时视频流
- HTTP API：参数读取、参数设置
- TCP/UDP：抓拍事件、雷达测速数据
- 厂商 SDK：设备发现、配置、升级、重启

验收标准：

- 模拟设备和真实设备可以共存。
- 替换设备实现不影响主界面。
- 通信失败不会导致 UI 卡死。

## 5. 开发里程碑

### 第 1 阶段：界面骨架

任务：

- 创建 C++/Qt 工程。
- 搭建 `MainWindow`。
- 实现顶部工具栏、左侧设备表、属性表、双画面区域、抓拍记录表、状态栏。
- 添加基础按钮和右键菜单。

验收：

- 软件能正常启动。
- 主界面布局接近参考软件。
- 窗口缩放稳定。

### 第 2 阶段：模型与数据流

任务：

- 定义设备、状态、配置、抓拍记录数据结构。
- 实现设备列表模型、属性模型、抓拍记录模型。
- 实现设备选择后刷新属性。

验收：

- 可显示多台模拟设备。
- 点击设备能看到对应属性。
- 抓拍表能增删数据。

### 第 3 阶段：模拟设备

任务：

- 实现 `IDeviceClient`。
- 实现 `MockDeviceClient`。
- 实现 `DeviceManager`。
- 定时生成状态和抓拍记录。

验收：

- 设备可连接、断开。
- 模拟抓拍可自动生成。
- 手动触发抓拍有效。

### 第 4 阶段：视频预览

任务：

- 实现 `VideoWidget`。
- 生成或加载模拟视频画面。
- 绘制车辆框、车牌框、叠加文字。
- 副画面显示最近抓拍图。

验收：

- 主画面实时刷新。
- 抓拍后副画面更新。
- 叠加信息完整清晰。

### 第 5 阶段：设备配置

任务：

- 实现 `DeviceConfigDialog`。
- 按相机、安全、硬件、算法、高级选项分组。
- 支持读取、编辑、保存参数。
- 离线设备配置项置灰。

验收：

- 配置能打开、修改、保存。
- 重启软件后配置恢复。
- 离线设备不能编辑。

### 第 6 阶段：全局设置

任务：

- 实现 `GlobalSettingsDialog`。
- 实现 `AppSettings`。
- 使用 `QSettings` 保存 `.ini`。

验收：

- 全局配置可保存。
- 重启后配置自动加载。
- 存储路径修改后生效。

### 第 7 阶段：存储、日志、导出

任务：

- 实现抓拍图片保存。
- 实现车牌小图保存。
- 实现 CSV 导出。
- 实现操作日志。
- 实现打开素材目录和清理缓存。

验收：

- 抓拍记录和本地文件能对应。
- CSV 中文不乱码。
- 操作日志完整。

### 第 8 阶段：批量运维

任务：

- 实现批量重启。
- 实现批量同步时间。
- 实现批量触发抓拍。
- 实现批量导出设备台账。
- 完善单设备右键菜单。

验收：

- 批量操作不阻塞界面。
- 在线设备操作成功。
- 离线设备记录失败原因。

### 第 9 阶段：真机接入准备

任务：

- 保留 `RealDeviceClient`。
- 保留 `RtspVideoSource`。
- 梳理真实设备通信方式。
- 规划 SDK/API 封装。

验收：

- 模拟设备和真实设备实现可以替换。
- 主界面不依赖具体协议。

## 6. 学习路线

### 第 1 周：Qt Widgets 基础

重点学习：

- `QMainWindow`
- `QWidget`
- `QSplitter`
- `QToolBar`
- `QAction`
- `QTableView`
- `QTreeView`
- `QDialog`
- 信号槽

练习目标：

- 做出主界面布局。
- 工具栏按钮可响应。
- 表格能显示假数据。

### 第 2 周：模型与工程结构

重点学习：

- `QAbstractTableModel`
- CMake
- Qt 资源系统
- C++ 类拆分
- 信号槽跨模块通信

练习目标：

- 设备表和抓拍表改为 Model/View。
- 数据和 UI 分离。

### 第 3 周：模拟设备与定时任务

重点学习：

- `QTimer`
- `QObject`
- 接口设计
- 简单状态机
- 设备管理服务

练习目标：

- 多台模拟设备自动产生抓拍记录。
- 支持连接、断开、重启、同步时间。

### 第 4 周：视频显示与绘制

重点学习：

- `QPainter`
- `QImage`
- `QPixmap`
- `paintEvent`
- OpenCV 基础，可选

练习目标：

- 显示模拟视频。
- 绘制车牌框、车辆框、速度文字。
- 副画面显示抓拍图。

### 第 5 周：配置系统

重点学习：

- `QSettings`
- 表单控件
- 树形配置 UI
- 参数校验

练习目标：

- 实现单设备配置弹窗。
- 实现全局设置弹窗。
- 参数能保存和恢复。

### 第 6 周：文件、日志、导出

重点学习：

- `QFile`
- `QDir`
- `QTextStream`
- CSV 编码
- 日志系统

练习目标：

- 保存抓拍图片。
- 导出 CSV。
- 记录操作日志。
- 打开本地目录。

### 第 7 周以后：真机协议准备

重点学习：

- `QTcpSocket`
- `QUdpSocket`
- `QNetworkAccessManager`
- RTSP/FFmpeg/OpenCV 拉流
- 多线程和异步通信

练习目标：

- 替换 Mock 设备。
- 接入真实视频流。
- 接入真实参数读写接口。

## 7. 测试计划

需要覆盖以下场景：

- 主界面启动、缩放、分屏切换、工具栏按钮点击。
- 添加 1 台、10 台、50 台模拟设备，设备列表和属性刷新正常。
- 在线、离线设备分别测试连接、断开、配置置灰、右键菜单。
- 模拟抓拍正常车、未知车牌、超速车、黑名单车。
- 抓拍记录分类和本地存储正确。
- 修改单设备配置、全局配置、预设模板后重启软件，配置能恢复。
- CSV 导出字段完整，中文内容不乱码。
- 视频预览暂停、恢复、切换设备时不卡死。
- 日志能记录关键操作，异常时有可读错误提示。
- 批量操作对在线设备生效，对离线设备记录失败原因。

## 8. 性价比结论

如果最终目标是长期维护、接真实硬件、做工业调试软件，直接使用 **C++/Qt** 是更合适的路线。

它的缺点是：

- 前期开发速度慢。
- 学习成本更高。
- 编译、链接、Qt 环境配置更复杂。
- 多线程、视频流、内存管理更容易出问题。

但它的优势更符合本项目：

- 最终代码不用从 Python 大规模迁移。
- 更适合接厂商 SDK。
- 更适合多路视频和长期运行。
- 更符合传统工业软件技术栈。
- 后续打包成 Windows 工具更自然。
- 和参考截图里的传统桌面软件风格高度匹配。

最终建议：

**不要再做 PySide6 过渡版，直接做 C++/Qt，但第一版仍然使用模拟设备。**

这样可以同时控制硬件联调风险和后期迁移成本。

