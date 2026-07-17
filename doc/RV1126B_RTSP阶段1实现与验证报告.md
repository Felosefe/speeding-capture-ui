# RV1126B RTSP 阶段 1 实现与验证报告

状态：**代码与自动化验证完成；真机量化记录待归档**

负责人：软件 C

实现日期：2026-07-17

正式后端：Qt 6.11.1 Multimedia / FFmpeg backend

## 1. 阶段交付

- 正式 `QtMultimediaRtspPlayer` 位于 `src/rv1126b/infrastructure/video/`，应用和
  `Rv1126bRtspSpike` 共用同一实现。
- 播放器状态机与 `QMediaPlayer/QVideoWidget` 后端分离，自动化测试使用 fake backend，不依赖网络或
  RTSP 测试服务。
- 支持主/辅码流和可变分辨率、3 秒默认首帧超时、5 秒卡流检测、`1/2/4/8/10` 秒封顶退避重连、
  重复打开、切换设备、停止和销毁。
- 每次媒体打开使用单独的 attempt token；旧流迟到的帧、元数据、错误或结束事件不会改变当前状态。
- `QMediaPlayer`、`QVideoWidget`、计时器和状态机留在 GUI 线程；实际解码线程由 Qt FFmpeg backend
  创建和回收，未新增业务 `QThread`。
- 正式应用在创建 `QApplication` 前设置 `QT_MEDIA_BACKEND=ffmpeg`；阶段 1 未接入主界面或设备会话。

## 2. 状态和错误契约

状态流为 `Idle -> Opening -> Playing`。可重试故障进入 `Reconnecting`，首帧恢复后进入 `Playing`
并清零退避；鉴权、格式或参数错误进入 `Error`；显式停止进入 `Stopped` 并取消全部计时器和重连。

| 错误码 | 分类 | 自动重试 |
| --- | --- | --- |
| `invalid_rtsp_spec` | Validation | 否 |
| `rtsp_open_timeout` | Network | 是 |
| `rtsp_frame_stalled` | Network | 是 |
| `rtsp_authentication_failed` | Authentication | 否 |
| `rtsp_format_unsupported` | Protocol | 否 |
| `rtsp_backend_error` | Network/Temporary | 是 |

播放器不传递 Qt backend 的原始错误字符串，避免 RTSP URL 中的用户名、密码或查询参数进入普通日志。

## 3. 自动化验证

`RtspPlayerTest` 覆盖：

- 合法打开、首帧进入 Playing 和可变分辨率。
- 参数校验、首帧超时、卡流检测和终态错误。
- 完整退避序列、10 秒封顶策略及成功后退避复位。
- 主辅码流切换、快速重复打开和旧 attempt 信号隔离。
- 播放或重连期间停止、重复停止、销毁和 widget/backend 资源释放。
- 后端错误信息不包含 RTSP 用户名、密码或查询参数。

验证命令：

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug --output-on-failure
```

本次仓库验证结果：MinGW Debug 全量 7/7 测试通过；MinGW Release 的 `CameraManagerApp`、
`Rv1126bRtspSpike`、`RtspPlayerTest` 构建成功，Release 播放器测试通过。

`windeployqt --dry-run` 已确认部署清单包含 `Qt6Multimedia`、`Qt6MultimediaWidgets`、
`ffmpegmediaplugin` 和 FFmpeg 运行库。工具提示未找到 `dxcompiler.dll/dxil.dll`；当前 FFmpeg RTSP
路径不依赖 Direct3D 12，该提示不阻塞阶段 1，发布阶段仍需在干净 Windows 环境复核。

## 4. 真机验收与剩余项

阶段 0 的主/辅码流矩阵已确认通过并冻结 Qt Multimedia/FFmpeg 后端，但仓库中尚无脱敏 CSV 或量化
汇总。取得记录后必须回填编码、分辨率、帧率、首帧 P50/P95、端到端延迟、CPU、工作集和断网恢复
成功数。未归档前，本阶段可标记“代码完成”，不可作为发布阶段的完整真机验收证据。

阶段 2 再由设备会话生成当前设备的 RTSP spec，将 `outputWidget()` 装配到主界面，并完成连接、切流和
断开的应用生命周期集成。
