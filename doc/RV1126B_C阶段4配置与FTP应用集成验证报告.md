# RV1126B C 阶段 4 配置与 FTP 应用集成验证报告

状态：**C 侧代码与自动化验证完成；A/B 生产服务合入和真机 G4 联合验收待执行**

负责人：软件 C  
实现日期：2026-07-20

## 1. 阶段交付

- 新增 `DeviceOperationsController`，只通过按 `device_id` 解析的 `IBoardApiClient` 和 `FtpService`
  编排展示配置、时间、FTP 配置/控制和历史任务，不直接操作网络 reply、SQL 或 FTP 命令。
- 真实设备“设备配置”入口新增展示配置、时间、FTP 配置和 FTP 历史任务四页签；顶部“同步时间”
  直接定位到时间页。`--mock` 继续使用原模拟设备配置流程。
- 展示配置完整读写五个字段，并在请求前校验 UTF-8 字节长度、控制字符和 `0..300 km/h`；响应明确
  展示 `restart_required`、生效范围及已有事件不变。
- 时间页展示归一化 UTC、本地时间、source epoch、offset、quality、NTP 状态和写能力。校时只提交
  当前 PC 的 UTC epoch 毫秒，并在写入前要求用户确认。
- FTP 配置支持最多 8 个 IPv4 目标、全局重试/超时参数、keep/replace/clear 密码动作、回滚和自动
  下发启停。密码输入使用密码回显，请求返回前即由 UI 清空，不保存到模型、设置、日志或快照。
- “保存并启用”严格区分配置未保存、配置已保存但控制失败、配置和 `new_events_only` 均成功三种结果。
- revision 冲突自动重新 GET，比较非敏感字段并保留本地编辑；用户确认后使用最新 revision 重提，
  replace 密码必须重新输入。
- 历史任务使用 UTC `[start,end)` 和已启用目标创建，校验 2020–2099、正向范围及最大 366 天跨度；
  支持每页 50 条 cursor 分页、2 秒无重叠刷新、逐目标计数/错误和 failed 任务重试。

## 2. 生命周期与安全规则

- 每类操作同时最多一个请求；任务列表和任务详情互不覆盖。设备切换、断开、对话框关闭或应用退出时
  取消板端与 FTP 请求，并通过设备/操作代次忽略迟到 completion。
- capability 缺失不隐藏页面；只有当前设备在线且 resolver 返回相应 service 时允许操作。401、403、
  409、网络和协议错误均按稳定 `error.code/category` 映射，不匹配英文 message 做业务判断。
- 时间在协议和控制器内始终保存 UTC epoch 毫秒，只在 UI 显示层转换为本地时区，不固定加减 8 小时。
- FTP 每个目标状态独立展示，一个目标失败不会覆盖另一目标的成功结果；禁用自动下发使用
  `scope=preserve`，不取消手工任务。
- cursor 只在当前任务分页链内存中保留；离开任务页停止定时刷新，关闭对话框调用 `cancelAll()`。

## 3. 自动化验证

新增 `DeviceOperationsControllerTest`，覆盖：

- 设备切换取消、迟到响应隔离、展示配置 UTF-8 校验和 UTC 校时范围。
- FTP 完整/部分成功、password replace 请求、revision 冲突重载与密码清除。
- 自动控制禁用的 preserve 语义、配置回滚、任务 366 天限制、启用目标校验、50 条分页和重试。

新增 `DeviceOperationsUiTest`，覆盖：

- 四页签装配、未校验时间警告和校时能力关闭。
- 8 目标上限、密码回显与提交后清空、部分成功提示。
- 双目标混合成功/失败详情、失败任务重试和 revision 冲突确认重提。

验证命令：

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug --output-on-failure
```

本次结果：MinGW Debug 全量构建成功，13/13 个 Qt Test 目标通过；阶段 2/3 搜索连接、RTSP、
事件回看、模拟抓拍、系统设置、存储和架构契约测试均无回归。

## 4. A/B 合入要求与真机待办

当前默认真实模式仍未装配 A/B 的生产 `IBoardApiClient` 和 `FtpService`。A/B 合入时需要通过
`MainWindowDependencies::boardApiForDevice` 与 `ftpServiceForDevice` 按稳定 `device_id` 返回对应对象，
并保证对象存活到设备断开或应用关闭。

真机 G4 必须补充以下记录：

1. 展示配置成功写入、`restart_required` 提示和重启后的新 OCR job 生效范围。
2. 时间写开关关闭的 403 路径，以及获批开启后的 UTC 校时和 quality 结果。
3. 两个 FTP 目标保存后再启用 `new_events_only`，分别展示独立状态。
4. 单目标离线时另一目标、事件显示和本地 evidence 缓存不受影响。
5. 手工 `[start,end)` 任务在 uploader 重启后仍可查询/继续，failed job retry 不影响其他任务。
6. 两客户端制造 revision 冲突，确认 UI 不自动覆盖且密码不回显。
7. 关闭设备管理窗、断开设备和退出应用后无残留配置请求、任务轮询或敏感日志。

只有上述真机记录完成后才能关闭整个阶段 4 G4；本报告当前关闭软件 C 可独立完成的交付项。
