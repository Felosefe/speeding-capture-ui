# RV1126B RTSP 阶段 0 技术验证报告

状态：**Qt Multimedia/FFmpeg 后端已按真机通过结论冻结；量化记录待归档**
负责人：软件 C  
基准环境：Windows、Qt 6.11.1、MinGW 64 位  
正式选型：Qt Multimedia / FFmpeg backend

## 1. 已完成的仓库交付

- `Rv1126bRtspSpike` 独立目标，通过既有 `IRtspPlayer` 接口播放，不接入正式主界面。
- 支持主/辅码流切换、冷启动、停止、3 秒首帧超时、5 秒卡流检测，以及
  `1/2/4/8/10` 秒封顶的退避重连。
- 支持并行 health 轮询、人工断网/恢复标记、人工端到端延迟样本、视频帧、CPU、工作集和
  首帧耗时记录。
- 结果写入脱敏 CSV；URL 的用户信息、查询参数、Bearer Token 和 RTSP 环境变量凭据不会写入结果。
- 进程启动前固定 `QT_MEDIA_BACKEND=ffmpeg`，避免 Windows 原生媒体后端影响选型结果。

2026-07-17 已确认主/辅码流真机矩阵通过并冻结该后端。仓库当前没有对应的脱敏 CSV 或完整量化
汇总，因此下表不虚构实测数值；拿到原始记录后应据实回填并归档。

## 2. 构建和运行

```powershell
cmake --preset mingw-release
cmake --build --preset mingw-release --target Rv1126bRtspSpike
```

凭据不得放入 URL、命令参数或 PowerShell 历史。若设备要求认证，用交互式安全输入在当前进程临时
设置环境变量：

```powershell
function Set-ProcessSecret([string]$Name) {
  $secret = Read-Host $Name -AsSecureString
  $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
  try {
    [Environment]::SetEnvironmentVariable(
      $Name,
      [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer),
      'Process')
  } finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
    $secret.Dispose()
  }
}
$env:RV1126B_RTSP_USER = Read-Host 'RV1126B_RTSP_USER'
Set-ProcessSecret RV1126B_RTSP_PASSWORD
Set-ProcessSecret RV1126B_BEARER_TOKEN
```

用设备实际地址替换 `<device-ip>`：

```powershell
build\mingw-release\Rv1126bRtspSpike.exe `
  --device-id <device-id> `
  --main-url rtsp://<device-ip>/live/0 `
  --sub-url rtsp://<device-ip>/live/1 `
  --health-url http://<device-ip>:18080/api/v1/health `
  --start main `
  --duration-minutes 30
```

默认结果写入 `rtsp-spike-results/rtsp-spike-<时间>.csv`。完成主码流矩阵后用 `--start sub`
重跑，或在窗口中切换码流。测试结束清除凭据：

```powershell
Remove-Item Env:RV1126B_RTSP_USER -ErrorAction SilentlyContinue
Remove-Item Env:RV1126B_RTSP_PASSWORD -ErrorAction SilentlyContinue
Remove-Item Env:RV1126B_BEARER_TOKEN -ErrorAction SilentlyContinue
```

## 3. 真机验证矩阵

| 场景 | 执行方法 | 通过标准 | 实际结果 |
| --- | --- | --- | --- |
| 主码流连续播放 | `/live/0` 运行 30 分钟 | 无崩溃、永久卡流或持续资源增长 | 已确认通过，记录待归档 |
| 辅码流连续播放 | `/live/1` 运行 30 分钟 | 无崩溃、永久卡流或持续资源增长 | 已确认通过，记录待归档 |
| 主码流冷启动 | 点击“冷启动/重新打开”共 10 次 | 至少 9 次首帧不超过 3000 ms | 已确认通过，数据待回填 |
| 辅码流冷启动 | 点击“冷启动/重新打开”共 10 次 | 至少 9 次首帧不超过 3000 ms | 已确认通过，数据待回填 |
| 断网恢复 | 标记断网、实际断网、恢复并标记，共 5 次 | 状态进入重连，恢复后 30 秒内重新出帧 | 已确认通过，数据待回填 |
| 切流和停止 | 主辅切换、停止、重开、关闭程序 | 停止调用不超过 2000 ms，无 UI 卡死或残留进程 | 已确认通过，记录待归档 |
| HTTP 并行 | 全程配置 health URL | 视频故障不停止 health；HTTP 故障不停止视频 | 已确认通过，记录待归档 |
| 延迟和资源 | 每路至少记录 10 个延迟样本并保留 metrics 行 | 报告平均/峰值 CPU、内存、延迟 P50/P95 | 已确认通过，数据待回填 |

端到端延迟需要用同一可见事件或时钟画面人工测得，然后在窗口录入；不能把播放器内部时间戳当作
端到端延迟。

## 4. 结果汇总与选型门禁

| 项目 | `/live/0` | `/live/1` |
| --- | --- | --- |
| 实际编码、分辨率、帧率 | 待从实测记录回填 | 待从实测记录回填 |
| 冷启动成功数/10 | 待从实测记录回填 | 待从实测记录回填 |
| 首帧 P50/P95/最大值 | 待从实测记录回填 | 待从实测记录回填 |
| 延迟 P50/P95 | 待从实测记录回填 | 待从实测记录回填 |
| CPU 平均/峰值 | 待从实测记录回填 | 待从实测记录回填 |
| 工作集平均/峰值 | 待从实测记录回填 | 待从实测记录回填 |
| 断网恢复成功数/5 | 待从实测记录回填 | 待从实测记录回填 |

Qt Multimedia/FFmpeg 已按真机通过结论冻结。量化记录归档仍是发布交接项；若后续复测出现无法解码、
不可控重连、停止挂起或资源泄漏，应重新打开选型结论，并用外部 FFmpeg/libav 运行完全相同的矩阵。

## 5. 部署检查

Release 构建完成后执行只读部署清单检查：

```powershell
C:\Qt\6.11.1\mingw_64\bin\windeployqt.exe --dry-run --json `
  build\mingw-release\Rv1126bRtspSpike.exe
```

检查结果必须包含 Qt Multimedia 插件及其 FFmpeg 运行库。最终还需将实际部署目录复制到没有 Qt
开发环境的干净 Windows 机器，重跑两路打开、停止和退出用例。

## 6. 阶段出口签字

| 确认项 | 负责人 | 状态/日期 |
| --- | --- | --- |
| DTO、错误码、网络接口、脱敏边界已冻结 | A | 待确认 |
| schema/迁移设计和旧数据兼容已确认 | B | 待确认 |
| RTSP 真机矩阵通过且后端选型已冻结 | C | 已确认/2026-07-17，量化记录待归档 |
| 联调板、Token、两路 RTSP、事件源持续可用 | A/B/C | 待确认 |

量化记录及 A/B 共同启动项仍应在进入发布验收前补齐；不得以自动化测试或 spike 成功编译代替真机证据。
