# RV1126B 应用接入 API 契约 v1

状态：v1 线协议已于 2026-07-14 冻结。主机集成测试、RV1126B 交叉编译、板端部署、
HTTP/UDP 检查和 Supervisor 恢复测试均已通过。Qt 端到端联调仍待验证。

## 1. 传输方式与端口

| 用途 | 传输协议 | 端口 | 方向 |
| --- | --- | ---: | --- |
| 设备发现 | UDP/IPv4 | 18081 | PC 广播请求，板端单播响应 |
| 应用 API | HTTP/1.1 over TCP/IPv4 | 18080 | PC 到板端 |
| 实时视频 | RTSP | 沿用现有 rkipc 端口 | PC 到板端 |
| 事件下发 | FTP | 按目标配置 | 板端到 FTP 服务器 |

`18080/TCP` 和 `18081/UDP` 是 v1 默认端口，也是线协议冻结端口。板端仍允许修改端口，
但非默认部署必须在设备发现响应中返回实际 HTTP 端口，并在 Qt 端显式配置。

v1 使用明文 HTTP。Bearer Token 可以阻止未认证访问，但不能防止局域网抓包获取凭据或
事件数据，因此只能部署在受控局域网中。TLS 或可信隧道需要在后续协议版本中增加。

## 2. 认证

每个 HTTP 接口都必须携带：

```http
Authorization: Bearer <token>
```

Token 从 `APP_API_TOKEN_FILE` 读取，不得写入 production profile、URL、设备发现响应、
API 响应正文或普通日志。板端 Token 文件权限必须为 `0600`。启用认证但 Token 文件缺失
时，受管服务必须拒绝启动。UDP 设备发现不认证，但响应中不得包含任何秘密。

## 3. 设备发现

PC 向每个适用子网的广播地址发送一个 UTF-8 JSON 数据报，目标 UDP 端口为 `18081`。
数据报不得超过 1023 字节。

```json
{"magic":"RV1126B_DISCOVERY","version":1,"type":"discover","nonce":"client-generated-value"}
```

`nonce` 必填，长度为 1 到 64 字节，客户端和板端都将其视为不透明文本。板端收到合法
请求后，向请求来源 IP 和来源端口发送单播响应：

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

Qt 必须校验响应中的 `nonce`、`magic` 和 `version`，并按 `device_id` 去重，不能依赖响应
顺序。非法数据报不返回响应。同一来源 IPv4 每 500 ms 最多收到一次响应。

## 4. HTTP 通用规则

- API 前缀固定为 `/api/v1`。
- 只读接口使用 `GET`，P0-10 配置和校时接口使用 `PUT`。
- 请求目标最长 2048 字节，完整请求头最长 8192 字节。
- 服务端每次响应后关闭连接。
- JSON 响应类型为 `application/json; charset=utf-8`。
- 融合证据图响应类型为 `image/jpeg`，必须返回准确的 `Content-Length`。
- 客户端建议使用 3 秒连接超时和 10 秒 JSON 请求超时；图片超时应按文件大小和局域网
  速度设置。
- 为兼容后续 v1 扩展，客户端必须忽略不认识的响应字段。

所有错误使用统一结构：

```json
{"api_version":"v1","error":{"code":"event_not_found","message":"event does not exist"}}
```

当前已实现的错误码：

```text
unauthorized
invalid_request_line
request_too_large
invalid_header
invalid_target
invalid_query
invalid_limit
invalid_cursor
invalid_event_identity
route_not_found
event_not_found
evidence_unavailable
file_missing
method_not_allowed
request_body_too_large
invalid_content_type
invalid_evidence_config
config_write_disabled
config_read_failed
config_write_failed
invalid_time
time_set_disabled
clock_set_failed
ftp_config_unavailable
invalid_ftp_config
ftp_config_write_disabled
config_revision_conflict
ftp_config_write_failed
invalid_ftp_control
ftp_config_backup_unavailable
ftp_task_write_disabled
invalid_ftp_task
ftp_task_target_unavailable
ftp_task_write_failed
ftp_task_read_failed
ftp_task_not_found
ftp_task_retry_failed
```

Qt 应按 `error.code` 映射用户提示，不得依赖英文 `message` 文本做业务判断。

## 5. 时间契约

每个 API 时间对象必须使用以下精确结构：

```json
{
  "epoch_ms": 1783957881984,
  "source_epoch_ms": 1783986681984,
  "offset_applied_ms": -28800000,
  "quality": "configured_offset"
}
```

- `source_epoch_ms`：板端或事件文件中的原始值，不得修改。
- `epoch_ms = source_epoch_ms + offset_applied_ms`。
- `native_utc`：原始值已经验证为标准 Unix UTC epoch。
- `configured_offset`：现场已经验证并配置了明确修正量。
- `board_epoch_unverified`：`epoch_ms` 不能作为已验证 UTC 展示。

Qt 缓存事件时必须同时保存 `quality`，不得在客户端自行静默减 8 小时。所有时间归一化由
板端负责，这样历史错误时间和未来修正后的固件才能在没有隐藏客户端规则的情况下共存。

当前板端测试 profile 已实测厂商时间链会增加 8 小时，因此显式配置：

```text
APP_API_TIME_OFFSET_MS=-28800000
APP_API_TIME_QUALITY=configured_offset
```

默认 production profile 仍保持 `board_epoch_unverified`，直到厂商时间链本身完成修复和验证。

## 6. 健康状态接口

```http
GET /api/v1/health
```

返回设备身份、`server_time`、应用 API 状态，以及 `event_pipeline_health.sh` 输出的安全子集。
响应不得包含板端 `/userdata/...` 绝对路径。

主要结构：

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

## 7. 事件列表接口

```http
GET /api/v1/events?limit=50
GET /api/v1/events?limit=50&cursor=v1.1783986681984.131.1994
```

- `limit` 默认值为 50，上限由 `APP_API_MAX_PAGE_SIZE` 限制。
- 排序规则为 `(source_event_epoch_ms, event_id, track_id)` 降序。
- `cursor` 对客户端不透明，客户端只能原样使用服务端返回值。
- 响应包含 `items`、`count`、`has_more` 和 `next_cursor`。
- 没有下一页时 `next_cursor` 为 `null`。
- Qt 通过轮询第一页获取新事件和 OCR 状态变化，再按 `(event_id, track_id)` 合并。
- 分页 cursor 只用于历史翻页，不是永久事件订阅游标。

每个列表项包含：事件身份、事件时间、运动方向、速度状态、当前 OCR 状态、车牌文本、
车牌颜色、融合图状态，以及按事件身份构造的详情和图片链接。

## 8. 事件详情接口

```http
GET /api/v1/events/{event_id}/{track_id}
```

响应由 formal JSON 和 OCR JSON 归一化生成，包含 `vehicle`、`line_region`、`radar`、
`ocr` 和 `images`。接口不得暴露或要求 PC 解析 `/userdata/...` 路径。

正式事件身份始终是：

```text
(event_id, track_id)
```

Qt 不得通过车牌号、文件名排序或 `latest.json` 推断事件身份。

## 9. 融合证据图下载接口

```http
GET /api/v1/events/{event_id}/{track_id}/images/evidence
```

服务端只根据数字事件身份和固定 `evidence` 角色构造文件名。客户端不能提交任意板端路径，
服务端也不能把 OCR JSON 中的 `evidence_image_path` 直接当作下载路径。符号链接必须拒绝。

- `200 image/jpeg`：融合图已经生成且文件存在。
- `404 event_not_found`：formal 事件不存在。
- `409 evidence_unavailable`：OCR 尚未完成、渲染未生成融合图，或预期文件缺失。

## 10. 展示配置与时间接口

### 10.1 读取展示配置

```http
GET /api/v1/config/evidence
```

返回当前 profile 中已经持久化的展示字段：

```json
{
  "api_version": "v1",
  "evidence": {
    "site_name": "测试点位",
    "road_direction": "由南向北",
    "speed_limit_kmh": 60,
    "status_text": "正常",
    "code_text": "TEST-0001"
  },
  "restart_required": false,
  "apply_mode": "next_pipeline_restart",
  "effective_scope": "new_ocr_jobs_only",
  "existing_events_unchanged": true
}
```

### 10.2 修改展示配置

```http
PUT /api/v1/config/evidence
Content-Type: application/json
```

请求必须完整包含以下五个字段，不接受未知字段：

```json
{
  "site_name": "测试点位",
  "road_direction": "由南向北",
  "speed_limit_kmh": 60,
  "status_text": "正常",
  "code_text": "TEST-0001"
}
```

约束：

- `site_name`：有效 UTF-8，最多 128 字节。
- `road_direction`：有效 UTF-8，最多 64 字节。
- `status_text`：有效 UTF-8，最多 64 字节。
- `code_text`：有效 UTF-8，最多 128 字节；当前仅作为普通显示文字，不代表防伪签名。
- `speed_limit_kmh`：整数，范围 `0..300`。
- 所有文本拒绝控制字符。
- `APP_API_CONFIG_WRITE_ENABLED=1` 时才允许写入，否则返回
  `403 config_write_disabled`。

服务端使用同目录临时文件、`fsync` 和原子 rename 更新当前 profile，并将更新前内容保存为：

```text
<active-profile>.app_api.bak
```

rkipc 在初始化时把展示字段复制到 probe，之后每个 OCR job 再复制当时的 probe 值。因此 PUT
成功后如果值发生变化，响应中的 `restart_required=true`。运维端重启完整 pipeline 后，
新 OCR job 使用新值，`restart_required` 变为 false；已有事件 JSON、OCR JSON 和 evidence 图片
不得追溯修改。

### 10.3 读取板端时间状态

```http
GET /api/v1/time
```

响应包含标准时间对象，以及：

```json
{
  "timezone_contract": "UTC epoch milliseconds",
  "ntp_status": "vendor_time_chain_unverified",
  "time_set_enabled": false
}
```

`ntp_status` 只描述当前已确认状态，不得把厂商 `+8h` 时间链标记为已同步 NTP。

### 10.4 设置板端时间

```http
PUT /api/v1/time
Content-Type: application/json

{"utc_epoch_ms":1784000000000}
```

- 请求只允许一个整数 `utc_epoch_ms`，范围为 2020-01-01 至 2099-12-31。
- `APP_API_TIME_SET_ENABLED=1` 时才允许执行，否则返回 `403 time_set_disabled`。
- 输入始终表示归一化 UTC。服务端按
  `source_epoch_ms = utc_epoch_ms - APP_API_TIME_OFFSET_MS` 设置系统时钟，确保设置后 API
  返回的 `epoch_ms` 与请求 UTC 一致。
- Linux 板端使用 `clock_settime(CLOCK_REALTIME)`，不拼接 shell 命令。
- 成功和失败日志不得包含 Token 或展示文字内容。

## 11. FTP 远程配置、控制和任务接口

### 11.1 读取 FTP 配置

```http
GET /api/v1/ftp/config
```

响应包含 `revision`、全局重试参数和 `targets`。目标只返回
`password_configured`，不得返回密码、密码摘要或可逆凭据。`revision` 由去除 PASSWORD 值
后的配置结构和文件修改时间生成，只作为并发令牌，不得受密码内容直接影响，也不作为安全摘要。

### 11.2 完整更新 FTP 配置

```http
PUT /api/v1/ftp/config
Content-Type: application/json
```

```json
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
      "password_action": "keep",
      "remote_dir": "/vehicle_events",
      "passive": true
    }
  ]
}
```

- 请求必须完整包含上述字段，不接受未知字段，最多 8 个目标。
- `password_action` 只能为 `keep`、`replace`、`clear`；仅 `replace` 时必须同时提供
  `password`。密码不得出现在响应或审计日志中。
- 当前 uploader 只接受 IPv4 地址；端口范围为 `1..65535`。
- `expected_revision` 不匹配返回 `409 config_revision_conflict`，客户端必须重新 GET 后合并。
- 写入使用同目录临时文件、`fsync`、`0600` 和原子 rename；旧配置保存为
  `<ftp-config>.app_api.bak`。
- `POST /api/v1/ftp/config/rollback` 接受唯一字段 `expected_revision`，恢复最近备份，并将
  回滚前配置保存为 `<ftp-config>.app_api.redo`。
- 更新后 uploader 在下一扫描周期热加载，`restart_required=false`，不得重启 rkipc、OCR
  Worker 或 FTP uploader。

### 11.3 自动下发控制

```http
GET /api/v1/ftp/control
PUT /api/v1/ftp/control
```

启用请求：

```json
{
  "expected_revision": "v1-0123456789abcdef",
  "enabled": true,
  "scope": "new_events_only"
}
```

`scope` 只能为 `new_events_only` 或 `all_existing`。前者把
`FTP_MIN_EVENT_EPOCH_MS` 设置为请求时刻，后者设置为 0。禁用请求使用
`enabled=false`、`scope=preserve`。禁用只暂停自动发现和自动 job，不取消手工任务。

### 11.4 创建历史下发任务

```http
POST /api/v1/ftp/tasks
```

```json
{
  "start_epoch_ms": 1783900800000,
  "end_epoch_ms": 1783987200000,
  "target_ids": ["server1", "server2"]
}
```

- 时间范围为 `[start_epoch_ms, end_epoch_ms)`，输入是归一化 UTC epoch 毫秒。
- 任务文件同时保存归一化范围和按 `APP_API_TIME_OFFSET_MS` 换算的 source 范围；uploader
  使用 source 范围匹配 formal JSON 中未经改写的 `event_epoch_ms`。
- 起止时间必须位于 2020-01-01 至 2099-12-31，跨度不超过 366 天。
- 目标必须存在且启用。任务创建后不可修改，任务和状态持久化在
  `EVENT_ROOT/upload_tasks`，板端或 uploader 重启后继续。
- 手工任务会为匹配的终态事件创建带 `TASK_ID` 的独立 job；即使同一事件已由自动下发
  成功，仍允许再次幂等上传到相同远端事件目录。

### 11.5 查询和重试任务

```http
GET /api/v1/ftp/tasks?limit=50&cursor=<opaque>
GET /api/v1/ftp/tasks/{task_id}
POST /api/v1/ftp/tasks/{task_id}/retry
```

列表按创建时间倒序。详情中的每个目标必须返回：

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

任务状态只使用 `queued`、`running`、`done`、`failed`。重试接口只把该任务的 failed job
恢复为 pending，并清零 job 的 attempts、next retry 和 last error；不得影响其他任务或自动
job。

写配置、自动控制分别要求 `APP_API_FTP_WRITE_ENABLED=1`；创建和重试任务要求
`APP_API_FTP_TASK_ENABLED=1`。关闭时返回 `403 ftp_config_write_disabled` 或
`403 ftp_task_write_disabled`。

## 12. 板端配置

```text
APP_API_ENABLED=0
APP_API_BIN=/userdata/rv1126b_deploy/bin/event_app_api
APP_API_BIND_ADDRESS=0.0.0.0
APP_API_ADVERTISED_IPV4=
APP_API_HTTP_PORT=18080
APP_API_DISCOVERY_PORT=18081
APP_API_AUTH_REQUIRED=1
APP_API_TOKEN_FILE=/userdata/rv1126b_deploy/production/config/app_api.token
APP_API_DEVICE_ID=rv1126b_001
APP_API_DEVICE_MODEL=RV1126B
APP_API_RELEASE_VERSION=development
APP_API_MAX_PAGE_SIZE=100
APP_API_CONFIG_WRITE_ENABLED=0
APP_API_TIME_SET_ENABLED=0
APP_API_FTP_WRITE_ENABLED=0
APP_API_FTP_TASK_ENABLED=0
APP_API_TIME_OFFSET_MS=0
APP_API_TIME_QUALITY=board_epoch_unverified
```

修改端口、Token、设备身份或时间策略后必须重启 `event_app_api`。当
`APP_API_ENABLED=1` 时，该进程由 production Supervisor 管理。

当前真机测试使用的 profile：

```text
/userdata/rv1126b_deploy/production/config/vehicle_event_4k_zero_copy_strict_evidence_api_test.conf
```

## 13. 验证状态

### 13.1 主机集成测试已确认

- UDP 请求校验、nonce 回显和 API URL 广告。
- Bearer Token 拒绝和接受。
- health 响应不泄露板端路径。
- 事件列表确定性分页。
- 按 `(event_id, track_id)` 合并事件详情。
- evidence JPEG 字节和 Content-Type。
- evidence 缺失错误响应。
- 展示配置读取、UTF-8/单引号编码、非法值拒绝、profile 备份和原子替换。
- 配置写入后 `restart_required=true`，服务重启后变为 false。
- 校时写入默认关闭并返回稳定错误码。

### 13.2 2026-07-14 RV1126B 真机已确认

- 产物为 ARM32 EABI5 hard-float，可由板端 `/lib/ld-linux-armhf.so.3` 和标准 C++ 运行库加载。
- 部署前 `18080/TCP`、`18081/UDP` 未被占用；部署后由单个受管
  `event_app_api` 进程监听。
- 错误 Token 返回 `401`；health、事件列表、详情、分页、`404` 和真实缺图 `409`
  均符合 v1 契约。
- 通过 HTTP 下载的 2,540,703 字节 evidence JPEG 与板端文件 MD5 完全一致。
- UDP 子网广播发现返回正确板端 IPv4、API URL、设备 ID 和匹配 nonce。
- 板端保留 41 条事件时，health 响应为 41-52 ms，10 条事件列表为 36-43 ms。
- Supervisor 保持单实例。只终止 `event_app_api` 后，API PID 改变，而 rkipc、OCR Worker、
  FTP uploader PID 均未变化；HTTP 和 UDP 随后恢复。
- `event_app_api` 恢复后空闲 RSS 约 2.7 MB。
- 生命周期脚本在发送信号前校验 PID 对应的预期可执行文件；板端回归证明陈旧 PID 不会误杀
  无关进程。
- API 配置 `-28800000` 修正后，归一化响应同时保留原始 `source_epoch_ms`。
- P0-10 evidence 配置 GET/PUT 已在真机验证；包含中文和单引号的值可正确往返，profile
  权限保持 `0600`，并生成 `.app_api.bak`。合法配置 PUT 响应时间为 `62 ms`，限速
  `301` 返回 `400 invalid_evidence_config`。
- evidence 配置更新后 `restart_required=true`，重启完整 pipeline 后变为 false；新值进入
  rkipc.ini 的 `vehicle_evidence_*`。已有 evidence 文件重启前后 MD5 均为
  `6be23035c2a8c20332c98ccc7a7f1f54`，没有被追溯改写。
- P0-10 校时 PUT 响应时间为 `13 ms`；设置后归一化时间与 PC UTC 相差 `16 ms`，
  `source_epoch_ms - epoch_ms = 28800000`，`quality=configured_offset`。
- 测试结束后原 evidence 配置已恢复。测试 profile 显式开启两个写开关，production 默认
  profile 仍保持 `APP_API_CONFIG_WRITE_ENABLED=0`、`APP_API_TIME_SET_ENABLED=0`。
- P0-12 至 P0-15 的 App API 和 uploader 均通过 Windows 主机严格编译及集成测试。测试覆盖
  密码不回显、配置 backup/rollback、revision 冲突、自动启停、任务分页和失败重试；真实
  FTP 测试覆盖自动关闭时手工任务继续执行、离线 pending/failed、daemon 重启恢复和每目标
  状态。
- 新 App API 真机产物 MD5 为 `3836936f7ddb59d54015878f224c3a73`；新 FTP uploader
  真机产物 MD5 为 `28497dcd01ff6d4276281d982ca105c8`，均为 ARM32 EABI5
  hard-float 并由板端 loader 验证。
- 真机配置 PUT 后 FTP config 和 `.app_api.bak` 均保持 `0600`；错误 revision 返回
  `409 config_revision_conflict`，rollback 恢复目标列表。API 响应和审计日志均未出现密码。
- 自动下发禁用、临时双目标配置和 rollback 全部由 uploader 热加载，rkipc PID `1403`、
  OCR Worker PID `1402`、App API PID `796` 未变化。
- 双目标历史任务 `ftp_task_1784038932347_000` 匹配 1 个事件，最终两个目标均为
  `done=1, failed=0`；两个 FTP 远端目录均生成 1,155 字节 `manifest.json`。
- 离线任务 `ftp_task_1784038935356_000` 进入 `failed`，目标 `attempts=1` 且保留
  `last_error`。FTP uploader 从 PID `397` 恢复为 PID `608` 后，任务状态仍在；调用 retry
  最终变为 `done=1, failed=0`。
- 历史任务扫描遇到异常 formal JSON 时只跳过该事件，不再中止整个任务；有效事件仍生成
  job 和 `.status`。任务时间范围已验证按 `-28800000` 偏移换算到 source epoch。
- 验证结束后原 FTP 配置 MD5 恢复为 `bb1e59bcfd240f4bbfea143244bf30fd`；四个受管
  进程均为单实例且 alive。
- revision 安全修正版已在真机执行同值 PUT：revision 正常变化，响应不含 `password` 字段，
  rollback 后 FTP 配置 MD5 再次恢复原值。最终 App API PID 为 `1416`，FTP uploader PID
  为 `608`，rkipc 和 OCR Worker PID 仍为 `1403`、`1402`。

### 13.3 仍需验证或单独处理

- Qt 按本契约完成正式联调。
- 新事件从 `queued` 变化到最终 OCR/evidence 状态时，Qt 按事件身份正确合并更新。
- 板端 UTC 持久化。板端重启后时间回到 2021，厂商时间链启动后又将时间增加 8 小时。
- 整板重启后的自动启动。厂商 `S21appinit -> RkLunch.sh` 仍直接启动 rkipc，因此当前没有
  安装 `/etc/init.d/S95_vehicle_event_pipeline`，以避免两个 rkipc 的启动竞态。
