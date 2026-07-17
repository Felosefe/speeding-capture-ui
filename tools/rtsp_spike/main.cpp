#include "SpikeWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QMessageBox>

#include <limits>
#include <utility>

namespace {

QUrl parseRtspUrl(const QString& value, QString* error)
{
    if (value.isEmpty()) {
        return {};
    }
    QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || url.scheme().compare(QStringLiteral("rtsp"), Qt::CaseInsensitive) != 0) {
        *error = QStringLiteral("无效 RTSP URL：必须使用 rtsp:// 协议");
        return {};
    }
    if (!url.userInfo().isEmpty() || !url.query().isEmpty()) {
        *error = QStringLiteral("RTSP URL 不得包含凭据或查询参数；请使用 RV1126B_RTSP_USER/RV1126B_RTSP_PASSWORD 环境变量");
        return {};
    }
    const QByteArray user = qgetenv("RV1126B_RTSP_USER");
    const QByteArray password = qgetenv("RV1126B_RTSP_PASSWORD");
    if (!user.isEmpty()) {
        url.setUserName(QString::fromUtf8(user));
    }
    if (!password.isEmpty()) {
        url.setPassword(QString::fromUtf8(password));
    }
    return url;
}

} // namespace

int main(int argc, char* argv[])
{
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Rv1126bRtspSpike"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("RV1126B Qt Multimedia RTSP 阶段 0 技术验证工具"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("device-id"), QStringLiteral("设备稳定标识"), QStringLiteral("id"), QStringLiteral("rv1126b-spike")});
    parser.addOption({QStringLiteral("main-url"), QStringLiteral("主码流 URL，例如 rtsp://192.168.1.10/live/0"), QStringLiteral("url")});
    parser.addOption({QStringLiteral("sub-url"), QStringLiteral("辅码流 URL，例如 rtsp://192.168.1.10/live/1"), QStringLiteral("url")});
    parser.addOption({QStringLiteral("health-url"), QStringLiteral("并行 health URL；Token 从 RV1126B_BEARER_TOKEN 读取"), QStringLiteral("url")});
    parser.addOption({QStringLiteral("start"), QStringLiteral("启动时播放 main 或 sub"), QStringLiteral("role"), QStringLiteral("main")});
    parser.addOption({QStringLiteral("duration-minutes"), QStringLiteral("自动测试时长；0 表示手工结束"), QStringLiteral("minutes"), QStringLiteral("30")});
    parser.addOption({QStringLiteral("open-timeout-ms"), QStringLiteral("首帧打开超时"), QStringLiteral("milliseconds"), QStringLiteral("3000")});
    parser.addOption({QStringLiteral("health-interval-ms"), QStringLiteral("health 轮询间隔"), QStringLiteral("milliseconds"), QStringLiteral("5000")});
    parser.addOption({QStringLiteral("output"), QStringLiteral("脱敏 CSV 结果文件"), QStringLiteral("path")});
    parser.process(app);

    QString parseError;
    QUrl mainUrl = parseRtspUrl(parser.value(QStringLiteral("main-url")), &parseError);
    if (!parseError.isEmpty()) {
        qCritical().noquote() << parseError;
        parser.showHelp(2);
    }
    QUrl subUrl = parseRtspUrl(parser.value(QStringLiteral("sub-url")), &parseError);
    if (!parseError.isEmpty()) {
        qCritical().noquote() << parseError;
        parser.showHelp(2);
    }
    if (!mainUrl.isValid() && !subUrl.isValid()) {
        qCritical("至少需要提供 --main-url 或 --sub-url");
        parser.showHelp(2);
    }

    bool numberOk = false;
    const double durationMinutes = parser.value(QStringLiteral("duration-minutes")).toDouble(&numberOk);
    if (!numberOk || durationMinutes < 0.0) {
        qCritical("--duration-minutes 必须是非负数");
        return 2;
    }
    const int openTimeoutMs = parser.value(QStringLiteral("open-timeout-ms")).toInt(&numberOk);
    if (!numberOk || openTimeoutMs <= 0) {
        qCritical("--open-timeout-ms 必须是正整数");
        return 2;
    }
    const int healthIntervalMs = parser.value(QStringLiteral("health-interval-ms")).toInt(&numberOk);
    if (!numberOk || healthIntervalMs < 1000) {
        qCritical("--health-interval-ms 不得小于 1000");
        return 2;
    }

    const QString startRole = parser.value(QStringLiteral("start")).toLower();
    if (startRole != QStringLiteral("main") && startRole != QStringLiteral("sub")) {
        qCritical("--start 只能是 main 或 sub");
        return 2;
    }

    QUrl healthUrl;
    if (!parser.value(QStringLiteral("health-url")).isEmpty()) {
        healthUrl = QUrl(parser.value(QStringLiteral("health-url")), QUrl::StrictMode);
        if (!healthUrl.isValid() || healthUrl.scheme() != QStringLiteral("http")
            || !healthUrl.userInfo().isEmpty() || !healthUrl.query().isEmpty()) {
            qCritical("--health-url 必须是无凭据、无查询参数的 http URL");
            return 2;
        }
    }

    RtspSpikeOptions options;
    options.deviceId = parser.value(QStringLiteral("device-id"));
    options.mainUrl = std::move(mainUrl);
    options.subUrl = std::move(subUrl);
    options.healthUrl = std::move(healthUrl);
    options.initialRole = startRole == QStringLiteral("main")
        ? rv1126b::RtspStreamRole::Main : rv1126b::RtspStreamRole::Sub;
    if ((options.initialRole == rv1126b::RtspStreamRole::Main && !options.mainUrl.isValid())
        || (options.initialRole == rv1126b::RtspStreamRole::Sub && !options.subUrl.isValid())) {
        options.initialRole = options.mainUrl.isValid()
            ? rv1126b::RtspStreamRole::Main : rv1126b::RtspStreamRole::Sub;
    }
    options.durationMs = static_cast<int>(qMin(durationMinutes * 60.0 * 1000.0,
                                                static_cast<double>(std::numeric_limits<int>::max())));
    options.openTimeoutMs = openTimeoutMs;
    options.healthIntervalMs = healthIntervalMs;
    options.outputPath = parser.value(QStringLiteral("output"));
    if (options.outputPath.isEmpty()) {
        options.outputPath = QDir::current().filePath(
            QStringLiteral("rtsp-spike-results/rtsp-spike-%1.csv")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    }

    SpikeWindow window(std::move(options));
    if (!window.isReady()) {
        QMessageBox::critical(nullptr, QStringLiteral("RTSP spike"), window.initializationError());
        return 3;
    }
    window.show();
    return app.exec();
}
