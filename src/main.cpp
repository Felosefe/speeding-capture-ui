#include "ui/MainWindow.h"
#include "rv1126b/infrastructure/video/QtMultimediaRtspPlayer.h"
#include "rv1126b/application/Rv1126bApplicationRuntime.h"
#include "services/SystemSettingsService.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QMessageBox>

int main(int argc, char* argv[])
{
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    if (qgetenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES").isEmpty()) {
        // 注意是 dxva2，不是 dx2va（后者 Qt/FFmpeg 都不认识，会被静默忽略，
        // 等于只剩 d3d11va 一个后端）。见 doc/实时视频性能评估 20260928.md。
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "d3d11va,dxva2");
    }
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName("CameraTools");
    QCoreApplication::setApplicationName("CameraManagerApp");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("RV1126B 车牌识别雷达测速摄像机管理软件"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption mockOption(QStringLiteral("mock"),
                                  QStringLiteral("启用旧模拟设备开发模式"));
    parser.addOption(mockOption);
    /*
     * P1: 开机自启时带这个参数 —— 启动后直接驻留托盘、开始同步，不弹窗口。
     */
    QCommandLineOption minimizedOption(QStringLiteral("minimized"),
                                       QStringLiteral("启动后最小化到系统托盘（后台同步事件）"));
    parser.addOption(minimizedOption);
    parser.process(app);

    const bool mockMode = parser.isSet(mockOption);
    const bool startMinimized = parser.isSet(minimizedOption);

    /*
     * P1: 事件同步跑在这个进程里，关掉窗口不应该把同步一起关掉。关窗口的行为由
     * MainWindow::closeEvent 决定（有托盘就只隐藏），所以这里不能让"最后一个窗口
     * 关闭"自动退出进程；真正退出走托盘菜单，或 closeEvent 里那条显式 quit。
     */
    QApplication::setQuitOnLastWindowClosed(false);

    if (mockMode) {
        MainWindowDependencies dependencies;
        dependencies.mockMode = true;
        auto* window = new MainWindow(dependencies);
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->setStartMinimized(startMinimized);
        if (!startMinimized) {
            window->show();
        }
    } else {
        SystemSettingsService settingsService;
        auto* runtime = new rv1126b::Rv1126bApplicationRuntime(settingsService.load(), &app);
        runtime->initialize(&app, [runtime, &app, startMinimized](rv1126b::ApiResult<void> result) {
            if (!result) {
                QMessageBox::critical(nullptr, QStringLiteral("RV1126B 初始化失败"),
                                      QStringLiteral("无法初始化设备数据库或生产服务：%1")
                                          .arg(result.error().message));
                QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
                return;
            }
            auto* window = new MainWindow(runtime->mainWindowDependencies());
            window->setAttribute(Qt::WA_DeleteOnClose);
            window->setStartMinimized(startMinimized);
            if (!startMinimized) {
                window->show();
            }
        });
    }

    return app.exec();
}
