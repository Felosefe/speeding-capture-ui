# RV1126B 车辆事件系统 Qt 应用端开发交接文档

版本：v1.0  
更新日期：2026-07-14  
适用范围：与 RV1126B 位于同一受控局域网、不能访问互联网的 Windows Qt 应用

## 1. 文档目的与交付状态

本文是 Qt 应用端的开发入口，说明设备发现、连接、实时视频、事件同步、本地保存、融合图回看、
FTP 自动下发、FTP 手动任务和断开设备的完整流程。

线协议的最终字段、限制和错误码以
[应用 API 契约 v1](RV1126B%20应用接入%20API.md) 为准。本文不要求 Qt 读取板端文件系统，也不允许 Qt
根据板端内部路径猜测事件文件。

当前状态必须按以下边界理解：

- UDP 发现、HTTP API、事件查询、融合图下载、FTP 配置及任务已经完成主机集成测试和 RV1126B
  真机验证。
- RTSP `/live/0` 和 `/live/1` 已有板端播放验证记录，但尚未完成正式 Qt 播放器联调。
- Qt 的本地数据库、融合图缓存、实时轮询、断点补齐、历史界面和断开资源释放尚待应用端实现。
- production 默认配置可能关闭 API 或写接口。收到 `403` 时必须按错误码提示，不得绕过板端开关。
- 整板开机自启动所有权和板端时间持久化仍是部署待办，不能由 Qt 假定已经解决。

## 2. 应用端需要拿到的交付物

应用端开发人员至少需要以下材料：

1. 本文档。
2. `docs/RV1126B 应用接入 API.md`，作为 v1 线协议权威来源。
3. 每台设备的 `device_id`、设备型号和 Bearer Token。Token 必须通过安全渠道提供，不得提交 Git。
4. 一台已启用 App API 的 RV1126B 联调设备。
5. 一段可以稳定触发车辆事件的测试视频或真实车辆测试条件。
6. 一个或多个局域网 FTP 测试服务器账号及可写目录。
7. 最终确认的 RTSP 主、辅码流用途和 Qt 解码方案。

不要向应用端交付板端 FTP 密码明文文件、板端绝对事件路径或开发机 `.env.app_api` 文件。

## 3. 总体架构和职责边界

```text
Qt 应用
  +-- UDP 18081：发现设备
  +-- HTTP 18080：health、事件、图片、配置和 FTP 任务
  +-- RTSP：实时视频播放
  +-- SQLite/文件缓存：PC 本地历史记录

RV1126B
  +-- rkipc：实时视频、车辆检测、跟踪、过线触发、抓拍
  +-- event_plate_ocr_worker：车牌检测/OCR、融合图生成
  +-- event_app_api：UDP 发现和 HTTP API
  +-- event_ftp_uploader：自动或按任务向多个 FTP 目标上传
```

三条业务链路必须相互独立：

```text
界面展示：板端事件 -> HTTP API -> Qt 列表和图片
本地历史：HTTP API -> Qt SQLite + PC 图片缓存
FTP 下发：板端事件 -> FTP uploader -> 指定 FTP 服务器
```

没有配置 FTP、FTP 服务器离线或 FTP 上传失败时，Qt 仍必须显示并保存检测记录。

## 4. 网络、端口和认证

| 用途 | 协议 | 默认端口 | 说明 |
| --- | --- | ---: | --- |
| 设备发现 | UDP/IPv4 | 18081 | PC 广播，板端单播响应 |
| 应用 API | HTTP/1.1 | 18080 | PC 请求板端 |
| 实时视频 | RTSP | 沿用 rkipc，历史验证为 554 | PC 播放板端码流 |
| FTP 下发 | FTP | 目标配置决定，通常 21 | 板端主动连接 FTP 服务器 |

全部功能只依赖局域网，不依赖互联网。UDP 广播通常不能跨路由器或 VLAN；PC 有多个网卡时，
Qt 应向每个处于启用状态的 IPv4 子网广播地址分别发送发现请求。

除 UDP 发现外，所有 HTTP 请求必须携带：

```http
Authorization: Bearer <token>
```

JSON 写请求还必须携带：

```http
Content-Type: application/json
```

当前 v1 是受控局域网内的明文 HTTP。Bearer Token 能阻止未认证调用，但不能抵抗同网段抓包。

## 5. 设备发现与连接

### 5.1 发现请求

Qt 向 UDP `18081` 发送不超过 1023 字节的 UTF-8 JSON：

```json
{"magic":"RV1126B_DISCOVERY","version":1,"type":"discover","nonce":"client-generated-value"}
```

`nonce` 长度为 1 到 64 字节。每次搜索使用新的不可预测文本。

### 5.2 发现响应

板端向请求来源 IP 和端口单播响应：

```json
{
  "magic": "RV1126B_DISCOVERY",
  "version": 1,
  "type": "discover_response",
  "nonce": "client-generated-value",
  "device_id": "rv1126b_001",
  "device_model": "RV1126B",
  "ipv4": "192.168.137.73",
  "api_version": "v1",
  "api_url": "http://192.168.137.73:18080/api/v1",
  "release_version": "development",
  "auth_required": true,
  "capabilities": ["health", "events", "evidence_download"]
}
```

Qt 必须同时校验 `magic`、`version` 和 `nonce`，并按 `device_id` 去重。用户不需要手工知道 IP；
选择设备后，Qt 使用响应中的 `api_url`。设备 IP 变化后应重新搜索，不要长期把 IP 当设备身份。

当前板端 `capabilities` 尚未声明已经实现的 FTP 和配置接口。此版本中不能仅因为发现响应没有
FTP 字样就隐藏 FTP 页面；该能力列表后续会由板端补全。

### 5.3 建立逻辑连接

HTTP 是无状态协议，没有单独的“登录会话”。Qt 选择设备后执行：

```http
GET /api/v1/health
```

收到 `200` 且 `device_id` 与所选设备一致，才进入已连接状态。错误 Token 返回：

```json
{"api_version":"v1","error":{"code":"unauthorized","message":"a valid bearer token is required"}}
```

Qt 必须按 `error.code` 判断，不得匹配英文 `message`。

## 6. 实时视频

RTSP 不经过 `event_app_api`。已验证的地址格式为：

```text
rtsp://<device-ip>/live/0
rtsp://<device-ip>/live/1
```

- `/live/0` 是主码流，清晰度和带宽较高。
- `/live/1` 是辅码流，适合应用端实时预览。
- 具体分辨率、帧率和编码参数受当前 rkipc profile 控制，Qt 不得写死为固定分辨率。

Qt 播放器必须独立处理打开、超时、重连和关闭。RTSP 失败不等于 HTTP API 离线；HTTP API
失败也不应立即删除本地已保存的历史记录。

## 7. HTTP 通用契约

API 前缀为：

```text
/api/v1
```

建议客户端超时：连接 3 秒，JSON 请求 10 秒；图片下载超时根据文件大小和局域网带宽单独设置。
服务端每次响应后关闭连接，Qt 不得依赖永久 HTTP 长连接。

统一错误结构：

```json
{
  "api_version": "v1",
  "error": {
    "code": "event_not_found",
    "message": "event does not exist"
  }
}
```

应用端必须忽略不认识的 v1 响应字段，以便板端在兼容范围内增加字段。

### 7.1 接口总览

| 方法 | 路由 | 用途 |
| --- | --- | --- |
| `GET` | `/api/v1/health` | 设备身份、时间和 pipeline 健康状态 |
| `GET` | `/api/v1/events` | 实时首页轮询和历史 cursor 分页 |
| `GET` | `/api/v1/events/{event_id}/{track_id}` | 查询事件详情和最新 OCR 状态 |
| `GET` | `/api/v1/events/{event_id}/{track_id}/images/evidence` | 下载融合 JPEG |
| `GET/PUT` | `/api/v1/config/evidence` | 读取或修改融合图展示文字 |
| `GET/PUT` | `/api/v1/time` | 读取或设置归一化 UTC 时间 |
| `GET/PUT` | `/api/v1/ftp/config` | 读取或完整更新 FTP 目标配置 |
| `POST` | `/api/v1/ftp/config/rollback` | 回滚最近一次 FTP 配置 |
| `GET/PUT` | `/api/v1/ftp/control` | 查询或启停 FTP 自动下发 |
| `POST/GET` | `/api/v1/ftp/tasks` | 创建历史任务或分页查询任务 |
| `GET` | `/api/v1/ftp/tasks/{task_id}` | 查询任务和每目标状态 |
| `POST` | `/api/v1/ftp/tasks/{task_id}/retry` | 重新排队该任务的失败 job |

## 8. 设备健康状态

```http
GET /api/v1/health
```

主要响应结构：

```json
{
  "api_version": "v1",
  "device_id": "rv1126b_001",
  "device_model": "RV1126B",
  "release_version": "api_v1_test",
  "server_time": {
    "epoch_ms": 1784002847389,
    "source_epoch_ms": 1784031647389,
    "offset_applied_ms": -28800000,
    "quality": "configured_offset"
  },
  "pipeline_health_available": true,
  "pipeline": {},
  "application_api": {
    "alive": true,
    "http_port": 18080,
    "discovery_port": 18081,
    "auth_required": true
  }
}
```

Qt 必须保存时间对象的 `quality`。当其为 `board_epoch_unverified` 时，不得把时间静默当作可靠
UTC，也不得自行固定减 8 小时。

## 9. 实时事件同步

### 9.1 获取事件列表

```http
GET /api/v1/events?limit=50
GET /api/v1/events?limit=50&cursor=<opaque>
```

响应：

```json
{
  "api_version": "v1",
  "items": [
    {
      "event_id": 131,
      "track_id": 1994,
      "event_time": {
        "epoch_ms": 1783957881984,
        "source_epoch_ms": 1783986681984,
        "offset_applied_ms": -28800000,
        "quality": "configured_offset"
      },
      "motion_direction": "down",
      "speed_kmh": 0,
      "speed_valid": false,
      "speed_status": "radar_disabled",
      "ocr_status": "matched",
      "plate_text": "测试车牌",
      "plate_ascii": "TEST123",
      "plate_color": "blue",
      "evidence_status": "generated",
      "evidence_available": true,
      "detail_url": "/api/v1/events/131/1994",
      "evidence_url": "/api/v1/events/131/1994/images/evidence"
    }
  ],
  "count": 1,
  "has_more": false,
  "next_cursor": null
}
```

以上值仅为结构示例。Qt 必须使用 `(device_id,event_id,track_id)` 作为 PC 本地唯一键。
`cursor` 是不透明分页值，只能原样回传，不得解析或持久化为永久订阅游标。

### 9.2 状态合并

板端可消费 OCR 状态模型：

```text
queued -> matched | no_plate | ambiguous | no_vehicle | failed | timed_out | queue_full
```

同一 `(event_id,track_id)` 从 `queued` 变为最终状态时，Qt 必须更新原有数据库行，不能新增
第二条记录。对尚未终态的事件，除了轮询第一页，还应保留待完成集合并请求事件详情，避免事件
在大量新记录产生后离开第一页而永远停留在 `queued`。

轮询周期必须做成可配置项。联调初期可使用 1000 ms，最终值必须根据真机负载和界面延迟验收
后冻结。网络失败时使用退避重试，不要并发堆积多个未完成请求。

### 9.3 断线补齐

Qt 重连后执行：

```text
请求第一页
-> upsert 每条事件
-> 按 next_cursor 继续翻页
-> 遇到本地已确认的连续历史边界或 has_more=false 后停止
-> 对本地非终态事件单独刷新详情
```

不能只保存“最后一个 event_id”，因为正式身份包含 `track_id`，排序还包含事件时间。

## 10. 事件详情和融合图

### 10.1 详情

```http
GET /api/v1/events/{event_id}/{track_id}
```

详情在列表字段之外包含：

```text
trigger_mode
capture_reason
vehicle
line_region
radar
ocr
images
```

`radar` 包含当前正式雷达字段；雷达未启用或无匹配样本时，事件和 OCR 仍可正常存在。
`images.evidence` 为可下载的相对 URL 或 `null`。

### 10.2 下载融合图

```http
GET /api/v1/events/{event_id}/{track_id}/images/evidence
```

- `200 image/jpeg`：保存响应字节。
- `404 event_not_found`：事件不存在。
- `409 evidence_unavailable`：OCR 或渲染尚未完成，稍后重试。

Qt 不能向板端提交任意文件路径，也不能从详情 JSON 推断 `/userdata/...` 路径。

## 11. PC 本地持久化要求

“保存本地”指保存到运行 Qt 的 PC，不是仅保存在板端。应用端至少需要持久化：

| 数据 | 必须保存的内容 |
| --- | --- |
| 设备 | `device_id`、最近 IPv4、API URL、型号、版本、最后在线时间、Token 引用 |
| 事件 | 复合身份、完整时间对象、方向、速度、OCR、车牌、融合图状态、最后更新时间 |
| 详情 | 最近一次成功获取的详情 JSON 或等价结构化字段 |
| 图片 | 融合 JPEG 本地路径、文件长度、下载状态、失败原因 |
| FTP 任务 | `task_id`、范围、目标和最近一次状态，便于界面恢复 |

推荐使用 SQLite。等价持久化方案可以替代，但不能只保存在内存模型中。

推荐图片目录：

```text
<AppData>/RV1126B/events/<device_id>/<YYYYMMDD>/
  event_<event_id>_track_<track_id>.evidence.jpg
```

下载时先写同目录 `.part` 文件，检查 HTTP 状态、`Content-Length` 和 JPEG 可解码性后原子改名。
如果 `evidence_available=true` 且本地不存在图片，应后台自动下载，不要等用户点击历史记录。

历史点击流程：

```text
查询本地 SQLite
-> 本地 evidence 存在则直接显示
-> 不存在且设备在线则从 evidence_url 补下载
-> 409 显示处理中并进入重试
-> 设备离线且本地无图则明确显示未缓存
```

## 12. 展示文字配置

读取：

```http
GET /api/v1/config/evidence
```

修改：

```http
PUT /api/v1/config/evidence
Content-Type: application/json

{
  "site_name": "测试点位",
  "road_direction": "由南向北",
  "speed_limit_kmh": 60,
  "status_text": "正常",
  "code_text": "TEST-0001"
}
```

修改成功后可能返回 `restart_required=true`。新值只作用于完整 pipeline 重启后的新 OCR job；
已有融合图不会追溯修改。`code_text` 当前只是显示文字，不是防伪签名。

## 13. 时间接口

```http
GET /api/v1/time
PUT /api/v1/time
Content-Type: application/json

{"utc_epoch_ms":1784000000000}
```

输入始终是 UTC epoch 毫秒。只有板端启用时间写权限时 PUT 才成功；否则返回
`403 time_set_disabled`。Qt 不得使用本地格式化日期或北京时间字符串代替 epoch 毫秒。
数据库保存原始毫秒值和 `quality`；界面需要北京时间时，在显示层转换为 `Asia/Shanghai`
或 UTC+08:00，不得改写协议值。

## 14. FTP 目标配置

### 14.1 读取配置

```http
GET /api/v1/ftp/config
```

响应包含 `revision`、全局重试参数和最多 8 个 `targets`。目标只返回
`password_configured`，永远不返回密码。

### 14.2 完整更新配置

先 GET 获取当前 `revision`，然后提交完整配置：

```http
PUT /api/v1/ftp/config
Content-Type: application/json

{
  "expected_revision": "v1-0123456789abcdef",
  "device_id": "rv1126b_001",
  "retry_max": 5,
  "retry_interval_sec": 30,
  "connect_timeout_sec": 10,
  "transfer_timeout_sec": 120,
  "scan_interval_sec": 5,
  "targets": [
    {
      "id": "server1",
      "enabled": true,
      "host": "192.168.137.1",
      "port": 21,
      "user": "upload",
      "password_action": "replace",
      "password": "由用户输入，不记录日志",
      "remote_dir": "/vehicle_events",
      "passive": true
    }
  ]
}
```

`password_action`：

- `keep`：保留已有密码，不传 `password`。
- `replace`：替换密码，必须传 `password`。
- `clear`：清空密码，不传 `password`。

收到 `409 config_revision_conflict` 时，重新 GET、让用户确认合并结果后再 PUT。不能覆盖其他
客户端刚写入的配置。配置成功后 uploader 热加载，不需要重启 rkipc、OCR Worker 或 App API。
当前 uploader 的 `host` 只接受 IPv4 地址。

回滚最近一次配置：

```http
POST /api/v1/ftp/config/rollback
Content-Type: application/json

{"expected_revision":"当前 GET 返回的 revision"}
```

## 15. FTP 自动下发

读取和修改自动下发状态：

```http
GET /api/v1/ftp/control
PUT /api/v1/ftp/control
```

启用后只自动处理新事件：

```json
{
  "expected_revision": "使用最近一次响应中的 revision",
  "enabled": true,
  "scope": "new_events_only"
}
```

启用并扫描全部已有事件：

```json
{
  "expected_revision": "使用最近一次响应中的 revision",
  "enabled": true,
  "scope": "all_existing"
}
```

禁用：

```json
{
  "expected_revision": "使用最近一次响应中的 revision",
  "enabled": false,
  "scope": "preserve"
}
```

本项目要求“保存 FTP 目标后自动更新新检测数据”。Qt 的保存流程必须是：

```text
PUT /ftp/config 成功
-> 取响应中的新 revision
-> PUT /ftp/control enabled=true, scope=new_events_only
-> 两步都成功才显示“已启用自动下发”
```

如果第二步失败，必须提示“目标已保存，但自动下发未开启”。自动下发状态不得影响 HTTP 事件
展示和 PC 本地缓存。

## 16. FTP 手动历史下发

手动下发与自动下发是两个独立功能。当前 v1 按 UTC 时间范围和目标创建任务：

```http
POST /api/v1/ftp/tasks
Content-Type: application/json

{
  "start_epoch_ms": 1783900800000,
  "end_epoch_ms": 1783987200000,
  "target_ids": ["server1", "server2"]
}
```

范围语义是 `[start_epoch_ms,end_epoch_ms)`，最长 366 天。目标必须存在且启用。任务创建后持久化，
板端或 uploader 重启后继续。

当前接口不支持提交任意 `(event_id,track_id)` 列表。如果产品要求在历史页面勾选几条记录精确
下发，需要另行增加协议，应用端不得用极窄时间范围猜测事件。

查询和重试：

```http
GET /api/v1/ftp/tasks?limit=50&cursor=<opaque>
GET /api/v1/ftp/tasks/{task_id}
POST /api/v1/ftp/tasks/{task_id}/retry
```

每个目标独立返回：

```json
{
  "target_id": "server1",
  "state": "running",
  "total": 10,
  "pending": 2,
  "uploading": 1,
  "done": 7,
  "failed": 0,
  "attempts": 3,
  "last_error": ""
}
```

任务状态只使用 `queued`、`running`、`done`、`failed`。某个目标失败不能覆盖其他目标的成功
状态，也不能让 Qt 隐藏对应检测记录。

## 17. 断开设备

板端没有也不需要“注销”或“停止检测”接口。用户点击断开时，Qt 必须：

1. 停止 RTSP 播放和解码线程。
2. 停止 health、事件和 FTP 任务轮询定时器。
3. 取消未完成的 HTTP 和图片下载，清理 `.part` 文件。
4. 关闭 UDP/HTTP/RTSP socket 和 `QNetworkReply`。
5. 清空当前设备的内存模型，但保留 SQLite 和已下载图片。
6. 不调用板端 service stop，不停止 rkipc、OCR Worker、FTP uploader 或 App API。

板端必须在 Qt 断开后继续检测、保存和按配置执行 FTP 自动下发。

## 18. 错误处理要求

| HTTP/错误码 | Qt 行为 |
| --- | --- |
| `401 unauthorized` | 标记认证失败，要求重新配置 Token，不要无限重试 |
| `403 *_disabled` | 显示板端未开放该写能力，不得伪装成功 |
| `404 event_not_found` | 标记板端记录已不存在，保留 PC 本地缓存 |
| `409 evidence_unavailable` | 保持处理中，退避后重试 |
| `409 config_revision_conflict` | 重新 GET 配置并处理并发冲突 |
| `400 invalid_*` | 显示输入校验错误，禁止原样重复提交 |
| `5xx` | 保留本地数据并退避重试，同时记录诊断日志 |
| 网络超时 | 设备状态变为离线/退化，不删除本地历史 |

应用日志不得记录 Bearer Token、FTP 密码或完整 Authorization 请求头。

## 19. 推荐的 Qt 模块划分

```text
DeviceDiscoveryService   UDP 广播、响应校验和设备去重
DeviceSession            当前设备状态、Token 引用、连接和断开
BoardApiClient           HTTP、认证、JSON 解析和错误码映射
RtspPlayer               RTSP 播放、重连和资源释放
EventSyncService         首页轮询、分页补齐和非终态刷新
EventRepository          SQLite upsert、查询和事务
EvidenceCache            JPEG 下载、校验、原子保存和清理
FtpConfigService         目标配置、revision 和自动启停
FtpTaskService           历史任务创建、轮询和失败重试
```

网络层、存储层和 UI 层必须分离。UI 不应直接拼接板端内部路径、FTP 命令或 shell 命令。

## 20. 最小端到端验收

应用端交付前必须保留以下测试记录：

1. 不知道板端 IP 时通过 UDP 搜索并连接正确 `device_id`。
2. 错误 Token 返回认证错误，正确 Token 可以读取 health。
3. 打开 RTSP 后持续播放，HTTP 查询同时可用。
4. 未配置 FTP 时，新车辆事件仍进入 Qt 列表并自动保存融合图到 PC。
5. 同一事件从 `queued` 更新为最终 OCR 状态，没有重复记录。
6. Qt 断开期间产生事件，重连后通过分页补齐。
7. 点击历史记录时优先读取 PC 本地融合图。
8. 配置两个 FTP 目标并启用自动下发，两个目标分别显示状态。
9. 一个 FTP 目标离线时，另一个目标和界面展示不受影响。
10. 按时间范围创建手动任务，任务在 uploader 重启后仍可查询并继续。
11. 断开 Qt 后板端继续检测和 FTP 自动下发。
12. 板端重启后重新搜索，IP 发生变化也能按 `device_id` 识别同一设备。

## 21. 当前已知限制和待确认项

- discovery `capabilities` 尚未加入 FTP、展示配置和时间能力标识。
- 正式 Qt 尚未验证 RTSP 解码库、硬件解码和长时间重连行为。
- PC 本地缓存保留天数、容量上限和清理策略尚需产品确认。
- 板端目前没有“PC 已持久化确认”接口。PC 长期离线超过板端保留期时，无法保证补齐已删除事件。
- 手动 FTP 任务当前只支持时间范围，不支持勾选任意事件身份。
- 板端时间重启回退及厂商 `+8h` 行为尚未完成根治。
- 整板自启动所有权尚未冻结，不能假定断电重启后 production pipeline 一定自动接管。
- production 默认关闭 App API 和多个写开关，交付设备必须使用经过批准的最终 profile。

上述限制在解决或完成真机/Qt 联调前，不得在应用端验收报告中标记为已完成。
