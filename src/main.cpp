#include "ui/MainWindow.h"
#include "rv1126b/infrastructure/video/QtMultimediaRtspPlayer.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>

int main(int argc, char* argv[])
{
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
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
    parser.process(app);

    const bool mockMode = parser.isSet(mockOption);
    auto* player = mockMode ? nullptr : new rv1126b::QtMultimediaRtspPlayer(&app);
    MainWindowDependencies dependencies;
    dependencies.player = player;
    dependencies.mockMode = mockMode;

    auto* window = new MainWindow(dependencies);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();

    return app.exec();
}
