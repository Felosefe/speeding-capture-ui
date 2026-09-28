#pragma once

#include <QString>

// 面向用户的提示文案集中在这里。
//
// 背景：板端协议里的 Bearer Token / endpoint / device_id 对现场操作员是黑话。
// 界面统一改说「验证码」「设备」「设备 ID」，但这些字符串散落在控制器、对话框、
// 单元测试三处，各写各的必然飘。这里放唯一一份。
//
// 约定：只改文案，不改协议字段名与错误码（error.code 保持英文，日志/排查照旧）。
namespace rv1126b {
namespace text {

// 认证失败时给用户看的话（控制器与对话框共用）。
inline const QString AuthenticationFailed = QStringLiteral("认证失败，请重新填写验证码");

// 设备需要验证码但本机没有可用凭据。
inline const QString TokenRequired = QStringLiteral("该设备需要验证码，请填写后重试");

// 本机保存过凭据但已被清除/失效。
inline const QString TokenMissingForKnownDevice = QStringLiteral("该设备的验证码已失效，请重新填写");

// 手工 IP 连接路径。
inline const QString TokenRequiredForEndpoint = QStringLiteral("该地址的设备需要验证码，请填写后重试");

} // namespace text
} // namespace rv1126b
