# 车牌识别雷达测速摄像机管理软件

本项目是一个基于 C++17 和 Qt 6 Widgets 的 Windows 桌面端上位机调试工具。

当前进度：已完成模块四全局系统设置，以及 RV1126B 对接 C 部分阶段 2/3 的搜索连接、实时视频、事件可靠性界面和本地图片回看集成。A/B 的真实发现、会话、仓储、事件同步与 evidence 缓存实现仍待合入。

已完成内容：

- CMake + Qt Widgets 工程骨架
- `MainWindow` 主界面
- 顶部工具栏
- 左侧设备列表和设备属性表
- 中间主预览画面和抓拍画面占位区
- 下方抓拍记录管理区
- 底部状态栏
- 设备右键菜单框架
- 工具栏基础响应
- `Device`、`DeviceStatus`、`DeviceConfig`、`CaptureRecord`、`OperationLog` 数据结构
- `DeviceTableModel`、`DevicePropertyModel`、`CaptureRecordTableModel`
- 设备表、属性表、抓拍表切换为 `QTableView + QAbstractTableModel`
- 默认启动真实设备模式；旧模拟设备仅通过 `--mock` 显式启用
- 点击设备后刷新属性表
- 抓拍记录支持新增、单条删除、手动刷新、暂停刷新和历史回溯
- `IDeviceClient` 统一设备接口
- `MockDeviceClient` 模拟设备客户端
- `DeviceManager` 设备管理服务
- 设备连接、断开、重启、同步时间走设备层
- 在线模拟设备会周期刷新状态
- 在线模拟设备会自动生成抓拍记录
- 手动抓拍通过模拟设备客户端生成记录
- `VideoWidget` 自绘视频控件
- 主画面实时刷新模拟道路画面
- 副画面显示当前设备最近一次抓拍
- 绘制车辆框、车牌框、车道线和道路背景
- 叠加地点、方向、时间、车牌、颜色、速度、限速、设备状态和防伪码
- 切换设备时同步更新预览画面和最近抓拍
- `CaptureRecordService` 抓拍记录服务
- 抓拍记录使用 SQLite 持久化保存到 `data/capture_records.sqlite`
- 抓拍列表字段覆盖抓拍精确时间、车牌号码、车牌颜色、事件类型、设备编号、通行朝向、画面坐标参数和备注
- 抓拍列表支持 `全部`、`有效车牌`、`无牌未知` 分类展示
- 暂停刷新期间抓拍记录继续入库，恢复或手动刷新后补入列表
- 抓拍记录支持导出 CSV，使用 UTF-8 BOM 避免中文乱码
- 新增 `CaptureRecordServiceTest`，覆盖持久化、删除、分类查询、空备注保存和 CSV 导出
- 顶部齿轮 `全局设置` 已接入真实配置弹窗
- 新增 `SystemSettings` 全局配置模型，覆盖界面选项、结果保存和系统选项
- 新增 `SystemSettingsService`，全局设置使用 `QSettings` 持久化保存到 `data/config/system.ini`
- 全局字体、字号、抓拍列表缓存条数和列表展示字段支持配置并即时应用
- 视频预览帧率、仅显示有车画面、车速叠加、标定线、车牌放大图位置和多路分屏支持配置
- 启动最大化、启动自动连接设备和最小化停止视频查询支持配置
- 新增 `CaptureStorageService`，支持抓拍素材存储路径、文件名模板、索引位数和文本信息文件生成
- 模拟抓拍记录会按配置生成本地素材占位文件，并把主素材路径写入抓拍记录
- 新增 `MaintenanceController`，支持每日定时同步设备时间、定时关机回调和磁盘自动维护调度
- 磁盘维护支持清理过期素材，并在低剩余空间时删除最早素材
- `DeviceManager` 支持批量连接、批量断开和批量同步设备时间
- `CaptureRecordService` 支持按最大缓存条数查询最近抓拍记录
- 新增正式 `QtMultimediaRtspPlayer`，使用 Qt Multimedia 的 FFmpeg backend 播放 RV1126B 主/辅 RTSP 码流
- RTSP 播放器支持首帧超时、卡流检测、`1/2/4/8/10` 秒退避重连、流切换、状态上报和可靠停止
- 播放器与媒体后端已分层，使用播放尝试令牌隔离旧码流的迟到帧和错误
- `Rv1126bRtspSpike` 与正式应用共用同一播放器实现
- 新增 `RtspPlayerTest`，使用 fake backend 覆盖状态、超时、重连、停止、切流、资源释放和凭据脱敏
- 新增 `DeviceIntegrationController`，统一管理扫描代次、Token 引用、设备选择、会话状态与 RTSP 生命周期
- 新增 RV1126B 搜索/连接对话框，支持按 `device_id` 去重、密码式 Token 输入和认证失败提示
- 主界面嵌入真实播放器输出，默认播放 `/live/1`，并可切换 `/live/0`
- HTTP 退化与 RTSP 故障相互隔离，切换设备只停止旧视频，不停止旧设备数据会话
- 新增控制器和 UI 自动化测试，覆盖搜索、连接、凭据、认证、切流、断开与未装配状态
- 新增 `EventViewController`，通过正式仓储、同步和 evidence 缓存端口编排实时事件、本地历史、分页、导出与删除
- 真实模式事件表以 `(device_id,event_id,track_id)` 原行 upsert，支持 queued 到终态更新及不同 track 隔离
- 抓拍区域新增“实时事件/本地历史”、当前/全部设备、车牌与 UTC 时间范围筛选及历史分页
- 正式事件展示 OCR、速度、时间质量和图片缓存状态，板端时间未校验时给出明确警告
- 真实 JPEG 优先从 PC 缓存解码回看；在线缺图自动补下载，离线未缓存和 409 重试状态明确展示
- 真实事件支持按当前筛选完整分页导出 CSV，以及先删本地图片再删仓储记录的单条删除/批量清空
- 设备断开和应用退出按同步、仓储请求、图片任务、RTSP、数据会话的顺序释放 PC 资源
- 新增 `SystemSettingsServiceTest`、`CaptureStorageServiceTest`、`MaintenanceControllerTest`、`DeviceManagerTest`
- 当前测试覆盖 11 个 Qt Test 目标，包括 RV1126B 架构契约、应用集成、事件回看 UI 和 RTSP 播放器状态机
- `CMakePresets.json` 已补充本机 Qt 6.11.1 MinGW / MSVC 构建配置

后续由 A/B 合入真实设备发现、会话、SQLite v2、事件同步和 evidence 缓存生产实现，再执行阶段 3 真机 G3 联合验收。

## 构建方式

本机推荐使用已配置好的 Qt 6.11.1 MinGW preset：

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

可用 preset：

- `mingw-debug`
- `mingw-release`
- `msvc-debug`
- `msvc-release`

普通 PowerShell 下建议使用 `mingw-debug` / `mingw-release`。`msvc-*` preset 适合在 Visual Studio Developer PowerShell 或 Qt Creator 的 MSVC Kit 中使用。

如果使用 Qt Creator，直接打开本目录的 `CMakeLists.txt` 或选择 `CMakePresets.json` 中的 preset 即可。

## 运行模式

默认启动真实设备模式。当前分支尚未包含 A/B 的具体网络服务，因此会明确显示“真实设备网络服务尚未装配”，不会回退到模拟设备：

```powershell
build\mingw-debug\CameraManagerApp.exe
```

需要继续使用旧模拟设备和模拟抓拍流程时，显式添加 `--mock`：

```powershell
build\mingw-debug\CameraManagerApp.exe --mock
```
