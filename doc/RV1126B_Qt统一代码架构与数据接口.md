# RV1126B Qt 统一代码架构与数据接口

版本：v1.0  
日期：2026-07-16  
状态：统一类型和抽象接口骨架已建立，具体网络、存储、凭据和视频实现待后续任务完成  
协议依据：[RV1126B 应用接入 API.md](RV1126B%20应用接入%20API.md)  
应用依据：[rv1126b_qt_application_development_handoff.md](rv1126b_qt_application_development_handoff.md)

## 1. 目的和边界

本文冻结 Qt 应用端真实 RV1126B 接入时必须共同遵守的代码分层、传输 DTO、领域模型、异步接口、
数据库结构和跨模块约定。三名软件开发人员应以本文和 `src/rv1126b` 头文件为共同接口基线。

本轮只提供可编译的类型和抽象接口，不包含以下具体实现：

- UDP 广播和响应接收。
- `QNetworkAccessManager` HTTP 客户端和 JSON codec。
- SQLite v2 repository。
- Windows Credential Manager adapter。
- RTSP 解码后端。
- 事件轮询、图片下载或 FTP 业务编排的运行时代码。

现有 `IDeviceClient`、`MockDeviceClient`、`CaptureRecord` 和 `capture_records` 表继续服务于开发模式。
真实 RV1126B 不继承同步 `IDeviceClient`，也不使用 `DeviceConfig` 表达板端配置。

## 2. 总体架构

```text
UI / ViewModel
    |
    v
Application Services
    DeviceFleetService / DeviceSession / EventSyncService
    EvidenceCache / FtpService
    |
    v
Domain Models + Ports
    IBoardApiClient / IEventRepository / ISecretStore / IRtspPlayer
    |
    v
Infrastructure
    UDP / HTTP / JSON / SQLite / Credential Manager / RTSP backend
```

依赖规则：

1. UI 只能调用 Application Service，不能直接操作 `QNetworkReply`、SQL 或 FTP 命令。
2. Protocol DTO 只描述线协议，不包含 UI 展示状态、数据库连接或业务调度。
3. Domain Model 不保存 Bearer Token、FTP 密码、`QNetworkReply` 或板端绝对路径。
4. Infrastructure 实现 port，负责协议校验、I/O、线程和系统 API。
5. 最多允许 8 个数据会话同时在线；只有当前选中设备可以绑定一路 RTSP 播放。
6. RTSP、HTTP、本地历史和 FTP 是独立状态，不以一条链路失败推断其他链路失败。

代码目录：

```text
src/rv1126b/
  core/       通用结果、请求身份、时间和事件身份
  protocol/   v1 JSON DTO 与 codec 接口
  domain/     应用领域模型
  ports/      网络、仓储、秘密和视频抽象端口
  services/   设备、同步、缓存和 FTP 应用服务
```

所有新增类型位于 `rv1126b` 命名空间。现有 Mock 类型暂不迁移命名空间，避免影响已有测试。

## 3. 公共结果与异步约定

### 3.1 请求和结果

- `RequestId` 是 `QUuid`，每个异步操作必须使用新的非空值。
- `ApiResult<T>` 只能处于成功值或 `ApiError` 两种状态，不允许使用默认 DTO 表示失败。
- `ApiError` 保存 HTTP 状态、原始 `error.code`、message、分类和 retryable。
- 数据库和凭据错误也使用统一结果类型，但 category 分别为 `Storage` 或相应本地错误分类。

统一方法形态：

```cpp
RequestId method(
    QObject* context,
    Completion<ApiResult<T>> completion);

void cancel(const RequestId& requestId);
void cancelAll();
```

生命周期约定：

- completion 在 context 所在线程的 Qt 事件循环中排队执行。
- 每个请求最多完成一次。
- context 销毁时取消请求并抑制 completion。
- 调用 `cancel` 时，仍存活的 context 收到 code 为 `cancelled` 的失败结果。
- service 断开时必须先停定时器，再取消 API/repository 请求。

### 3.2 错误分类和默认行为

| HTTP/code | category | retryable | 默认行为 |
| --- | --- | --- | --- |
| `401 unauthorized` | Authentication | 否 | 会话进入 AuthenticationFailed，等待重新配置 Token |
| `403 *_disabled` | CapabilityDisabled | 否 | 显示板端未开放能力，不伪装成功 |
| `400 invalid_*` | Validation | 否 | 显示输入错误，不原样自动重试 |
| `404 event_not_found` | NotFound | 否 | 保留 PC 数据，标记板端记录已不存在 |
| `409 evidence_unavailable` | Temporary | 是 | 图片进入 RetryWait 并退避 |
| `409 config_revision_conflict` | Conflict | 否 | 重新 GET，让用户确认合并后再 PUT |
| 网络超时、连接失败 | Network | 是 | 会话进入 Degraded，按退避恢复 |
| `5xx` | Temporary | 是 | 保留本地状态并退避 |
| JSON 缺字段或类型错误 | Protocol | 否 | 记录脱敏诊断，禁止使用部分 DTO |

客户端业务判断只使用 `error.code`，不能匹配英文 message。

## 4. 公共值对象

### 4.1 时间

`NormalizedTime` 精确包含：

```text
epochMs
sourceEpochMs
offsetAppliedMs
quality.value
quality.rawValue
```

`TimeQuality` 支持 `NativeUtc`、`ConfiguredOffset`、`BoardEpochUnverified` 和 `Unknown`。
所有协议、领域和数据库时间均保存 `qint64` UTC epoch 毫秒。显示北京时间只在 UI 格式化层执行，
不能修改原始值，也不能在客户端固定减 8 小时。

### 4.2 事件身份

正式唯一键：

```text
(deviceId, eventId, trackId)
```

`EventIdentity` 提供相等、排序和 `qHash`。任何 repository、缓存和 UI model 更新都必须使用该对象，
不能以车牌、文件名或单一 event ID 作为唯一键。

`EventSortKey` 为：

```text
(sourceEpochMs, eventId, trackId)
```

它只用于确定性排序和重连锚点，不替代 `EventIdentity`。

### 4.3 线协议枚举

`WireEnum<T>` 同时保存已知枚举和原始字符串。解析到未知 v1 扩展值时：

- `value` 设为 `Unknown`。
- `rawValue` 保存服务端原值。
- 不因未知响应值导致整个响应失败。
- 未知 OCR 状态不得擅自判定为终态。

## 5. 传输 DTO

### 5.1 发现和健康状态

`DiscoveredDeviceDto` 完整保存 magic、version、type、nonce、设备身份、IPv4、API URL、版本、
认证要求、capabilities 和 raw JSON。

discovery codec 必须校验：

- magic 等于 `RV1126B_DISCOVERY`。
- version 等于 1。
- type 等于 `discover_response`。
- nonce 与当前扫描一致。
- `api_url` 是合法 HTTP URL。

capabilities 只作为提示保存。当前板端未完整声明 FTP、配置和时间能力，UI 不能据此隐藏页面。

`HealthDto` 强类型保存设备身份、server time、pipeline 可用性和 application API 状态，同时保存
pipeline 安全子集和完整 raw JSON。

### 5.2 事件

`EventSummaryDto` 强类型保存契约已冻结的列表字段：

- event/track ID 和完整事件时间。
- 方向、速度值、速度有效性和 speed status。
- OCR 状态、车牌文本、ASCII 文本和颜色。
- evidence 状态、可用性、详情相对 URL 和图片相对 URL。

`EventPageDto` 保存 items、count、hasMore 和可空 nextCursor。cursor 是不透明字符串，只允许在
当前分页链中原样回传。

`EventDetailDto` 的身份和摘要字段强类型保存；契约未冻结内部结构的 `vehicle`、`line_region`、
`radar`、`ocr` 和 `images` 使用 `QJsonObject`，并保存完整 raw JSON。

### 5.3 evidence 和时间配置

`EvidenceConfigUpdate` 只包含五个可写字段。codec 必须执行与服务端一致的客户端预校验：

- siteName 最多 128 UTF-8 字节。
- roadDirection、statusText 最多 64 UTF-8 字节。
- codeText 最多 128 UTF-8 字节。
- speedLimitKmh 为 `0..300`。
- 文本拒绝控制字符。

`TimeUpdate` 只包含 `utcEpochMs`，范围为 2020-01-01 至 2099-12-31。不能序列化本地日期字符串。

### 5.4 FTP

FTP 配置快照和更新请求必须分开：

- snapshot 只包含 `passwordConfigured`。
- update 使用 `passwordAction = keep/replace/clear`。
- 只有 replace 允许并要求 `replacementPassword`。
- 密码不能进入响应模型、领域模型、数据库、QSettings、日志或调试输出。
- targets 最多 8 个，host 当前只接受 IPv4，port 为 `1..65535`。

任务状态只识别 queued、running、done、failed，未知值按 `Unknown + rawValue` 保存。每个 FTP 目标
的状态和计数独立保存，不能用一个失败目标覆盖其他目标的成功结果。

### 5.5 JSON codec 规则

`IApiCodec` 的具体实现必须遵循：

- 响应忽略未知字段，但严格检查已知必填字段的存在和 JSON 类型。
- 所有请求通过专用 DTO 序列化，不允许透传任意 `QJsonObject`。
- 不允许把 Token、FTP 密码或 Authorization 写入解析错误文本。
- HTTP 错误先解析统一 error 对象，再由稳定 code 分类。
- UTF-8 输入和输出不得通过本地 8-bit 编码中转。

## 6. 领域模型

| 类型 | 职责 |
| --- | --- |
| `DeviceProfile` | 可持久化设备身份、endpoint、版本、capabilities 和 credential 引用 |
| `DeviceSessionSnapshot` | 当前连接状态、最后 health 时间和最后错误 |
| `VehicleEvent` | UI、同步和 SQLite 共用的事件摘要，不含 raw transport 对象 |
| `EventDetailSnapshot` | 最近成功详情和 raw JSON |
| `EvidenceCacheEntry` | 图片角色、路径、长度、状态和失败原因 |
| `StoredFtpTask` | 可恢复的 FTP 任务概要和各目标状态 |
| `SyncAnchor` | 上次成功首页的 head sort key，不保存 cursor |

`VehicleEvent` 的 upsert 规则：

- 首次发现设置 firstSeenEpochMs。
- 后续响应更新同一复合身份的 OCR、车牌、速度、evidence 和 lastUpdatedEpochMs。
- queued 转终态更新原行，禁止新增第二条记录。
- 未在新响应出现的字段不能用默认值无条件覆盖最近成功值。

## 7. 端口和应用服务

### 7.1 `IBoardApiClient`

每个 `DeviceSession` 使用独立的 API context，但底层实现可共享一个网络运行时。端口覆盖 health、
事件列表/详情、evidence 文件、evidence/time 配置以及全部 FTP 接口。

安全 URL 规则：

- API base URL 取 discovery 响应或显式批准的设备配置。
- 事件返回 URL 必须为相对 URL。
- resolve 后必须与 base URL 的 scheme、host 和 port 相同。
- 路径必须位于 `/api/v1`，拒绝跨主机、跨端口和任意板端路径。

默认超时为连接 3 秒、JSON 10 秒；图片超时由下载大小策略单独配置。

### 7.2 `IEventRepository`

repository 提供设备、事件、详情、图片、同步锚点和 FTP 任务的异步操作。具体 SQLite 实现必须：

- 在一个专用存储线程创建和使用数据库连接。
- 串行化写事务。
- 通过 context 将 completion 投递回调用线程。
- 不跨线程传递或复用 `QSqlDatabase` 对象。
- 支持按完整 `EventIdentity` 精确加载和删除本地事件；删除仅作用于 PC 仓储，不代表删除板端事件。

### 7.3 `DeviceDiscoveryService`

- 枚举所有启用、非 loopback 的 IPv4 接口及广播地址。
- 每次 1500 ms 扫描创建新 nonce，并向每个适用子网广播一次。
- 同一扫描按 deviceId 去重，响应顺序不作为业务依据。
- 取消扫描后关闭 socket，不再发出 deviceFound。

### 7.4 `DeviceSession` 和 `DeviceFleetService`

会话状态机：

```text
Disconnected -> Connecting -> Online
                         |-> AuthenticationFailed
                         |-> Degraded
Online <---------------> Degraded
任意活动状态 -> Disconnecting -> Disconnected
```

- 只有 health 200 且 deviceId 匹配才能进入 Online。
- 401 进入 AuthenticationFailed，不自动重试。
- 网络或 5xx 进入 Degraded，HTTP 退避恢复。
- 断开取消 HTTP、事件、详情和图片任务，但保留 SQLite 和正式图片。
- fleet 最多启动 8 个数据会话。
- 切换视频只停止旧 RTSP，不停止旧设备的数据同步。

### 7.5 `EventSyncService`

- 首页默认每 1000 ms 轮询，允许配置 `500..60000 ms`。
- 单设备最多一个未完成列表请求，不允许定时器堆积请求。
- 首页 page size 固定默认 50。
- queued 事件进入待完成集合，通过详情请求独立刷新。
- 重连分页直到遇到上次成功首页的 `EventSortKey` 或 hasMore=false。
- 分页 cursor 只在当前链路内存中使用。
- 网络退避为 1、2、4、8、16、30 秒，30 秒封顶；成功请求后重置。

### 7.6 `EvidenceCache`

- 每设备最多 1 个下载，全局最多 4 个下载。
- 写同目录 `.part` 文件。
- 验证 200、`image/jpeg`、Content-Length 和 JPEG 可解码性。
- 验证成功后原子替换正式文件。
- 409 进入 RetryWait，网络错误按退避重试。
- 断开清理当前设备未完成的 `.part`，不删除正式文件。
- 用户明确删除本地事件时，先通过 `removeLocal` 删除正式图片，成功后再删除 repository 记录；普通
  断开和应用退出不得调用该接口。

正式路径：

```text
<AppData>/RV1126B/events/<device_id>/<YYYYMMDD>/
  event_<event_id>_track_<track_id>.evidence.jpg
```

### 7.7 `FtpService`

保存并开启新事件自动下发必须由 service 串行完成：

```text
PUT /ftp/config
-> 读取响应的新 revision
-> PUT /ftp/control enabled=true, scope=new_events_only
```

`FtpActivationResult` 必须区分：

- configSaved=false：配置未保存。
- configSaved=true、autoEnabled=false：目标已保存，但自动下发未开启。
- 两项均 true：才显示自动下发已启用。

### 7.8 `DeviceOperationsController`

软件 C 的设备操作控制器通过按 `device_id` 解析的 `IBoardApiClient` 和 `FtpService` 组合展示配置、
时间、FTP 配置/控制与历史任务，不拥有 A/B 的生产实现。控制器负责客户端预校验、稳定错误码映射、
同类请求不重叠、切换设备后的迟到回调隔离，以及关闭时的请求取消。

`MainWindowDependencies` 暴露 `BoardApiResolver` 和 `FtpServiceResolver`。resolver 返回的对象归装配层
所有，必须至少存活到设备断开或应用关闭；UI 不得缓存 Token、FTP 密码或 `QNetworkReply`。

FTP revision 冲突必须重新读取远端快照并由用户确认后重提。冲突比较只能包含非敏感字段；已提交的
replace 密码立即清空，不能跨冲突保留或回显。历史任务页面每页请求 50 条，cursor 仅用于当前分页链，
轮询期间同类请求最多一个在途。

## 8. 凭据和 RTSP

### 8.1 `ISecretStore`

Windows 正式实现使用 Credential Manager，建议 target name：

```text
RV1126B/<device_id>
```

SQLite/QSettings 只保存 credentialRef。`SecretValue` 禁止复制，析构和覆盖时尽力清零内部字节。
Token 只能放入 Authorization header，不能进入 URL、普通日志或 Git。

### 8.2 `IRtspPlayer`

统一状态：Idle、Opening、Playing、Reconnecting、Stopped、Error。端口只冻结 open、stop、state、
outputWidget 和状态/错误信号，不暴露具体解码帧类型。

正式后端冻结为 Qt 6.11.1 Multimedia 的 FFmpeg backend，独立验证入口为
`Rv1126bRtspSpike`。正式 `QtMultimediaRtspPlayer` 与 spike 共用一套实现，负责首帧超时、卡流检测、
退避重连、状态上报和播放代次隔离；Qt Multimedia 对象、计时器和状态机留在 GUI 线程，实际解码线程
由媒体后端创建和回收。公共调用必须发生在播放器所属线程，跨线程消费者使用 queued signal；UI 仍只
依赖 `IRtspPlayer`。固定错误码为 `invalid_rtsp_spec`、`rtsp_open_timeout`、
`rtsp_frame_stalled`、`rtsp_authentication_failed`、`rtsp_format_unsupported` 和
`rtsp_backend_error`。真机量化数据必须归档到阶段验证报告，不得用自动化测试结果替代。

## 9. SQLite v2

数据库初始化执行：

```sql
PRAGMA foreign_keys = ON;
PRAGMA user_version = 2;
```

建议正式表结构：

```text
rv_devices
  PK(device_id)
  device_model, release_version, ipv4, api_url, http_port,
  discovery_port, credential_ref, capabilities_json, last_online_epoch_ms

rv_events
  PK(device_id, event_id, track_id)
  epoch_ms, source_epoch_ms, offset_applied_ms, time_quality,
  motion_direction, speed_kmh, speed_valid, speed_status,
  ocr_status, ocr_status_raw, plate_text, plate_ascii, plate_color,
  evidence_status, evidence_available, detail_url, evidence_url,
  first_seen_epoch_ms, last_updated_epoch_ms

rv_event_details
  PK/FK(device_id, event_id, track_id)
  trigger_mode, capture_reason, detail_json, fetched_epoch_ms

rv_evidence
  PK/FK(device_id, event_id, track_id, role)
  remote_url, local_path, content_length, status, failure_code, updated_epoch_ms

rv_sync_state
  PK/FK(device_id)
  head_source_epoch_ms, head_event_id, head_track_id, saved_epoch_ms

rv_ftp_tasks
  PK(device_id, task_id)
  start_epoch_ms, end_epoch_ms, state, state_raw,
  created_epoch_ms, refreshed_epoch_ms

rv_ftp_task_targets
  PK/FK(device_id, task_id, target_id)
  state, state_raw, total, pending, uploading, done, failed,
  attempts, last_error
```

迁移策略：

1. 检查 `PRAGMA user_version` 并在事务内创建 v2 表和索引。
2. 保留现有 `capture_records` 表及全部数据，不修改、不迁移、不删除。
3. Mock 查询继续使用旧表；真实事件查询只使用 `rv_*` 表。
4. migration 失败时回滚事务并返回 Storage 错误，不能创建半套 schema。
5. 数据库永远不保存 Token 或 FTP 密码。

## 10. 线程、限流和资源释放

| 资源 | 所有权 |
| --- | --- |
| UDP socket、HTTP reply、网络定时器 | 网络对象所在线程，首版建议 Qt 主事件线程 |
| SQLite connection 和事务 | 单独 storage thread |
| RTSP 解码线程 | 具体 media backend 管理 |
| UI model 和 QWidget | Qt 主线程 |

全局限制：

- 在线数据会话最多 8 个。
- 活动视频最多 1 路。
- evidence 下载全局最多 4 个、每设备最多 1 个。
- 每设备最多 1 个事件列表请求；详情请求使用有界队列。
- 所有定时器回调启动请求前必须检查上一请求是否完成。

用户断开设备时按顺序执行：停止 RTSP、停止轮询定时器、取消 HTTP/下载、清理 `.part`、释放内存
model；SQLite 和正式 evidence 保留。不得调用任何板端 service stop。

## 11. 头文件与模块所有权

| 路径 | 内容 | 建议负责人 |
| --- | --- | --- |
| `core/Result.h`、`core/ValueTypes.h` | 共同类型和异步结果 | 协议负责人，三人评审 |
| `protocol/*`、`ports/IBoardApiClient.h` | JSON 契约和 HTTP 端口 | 软件 A |
| `domain/Models.h`、`ports/IEventRepository.h` | 领域和本地存储端口 | 软件 B |
| `ports/IRtspPlayer.h` | 媒体后端隔离 | 软件 C |
| `services/*` | 跨模块应用接口 | 对应负责人，集成负责人终审 |
| `application/DeviceOperationsController.*`、设备操作 UI | 配置、时间和 FTP 应用编排 | 软件 C |

任何公共字段或方法变更必须同步修改本文、头文件和相关测试夹具，并由至少两名成员评审。

## 12. 后续实现验收

1. 文档中的发现、health、事件、配置、时间和 FTP JSON 示例全部能严格解析。
2. 未知响应字段被忽略；缺失必填字段返回 Protocol 错误。
3. 复合事件身份的哈希、SQLite upsert 和 queued 到终态更新正确。
4. 时间对象完整往返，不发生客户端静默时区修正。
5. 初始化 v2 后旧 `capture_records` 表及内容不变。
6. 8 个模拟数据会话同时运行且不堆积列表请求，只绑定一路视频。
7. 401、403、400、404、409、5xx 和网络超时按本文策略处理。
8. Token 和 FTP 密码不出现在数据库、QSettings、日志或测试快照中。
9. 图片只在校验成功后成为正式文件，取消后没有残留 `.part`。
10. 断开设备不删除本地数据，也不影响板端继续检测和 FTP 下发。
