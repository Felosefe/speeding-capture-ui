#include <QFile>
#include <QtTest>

class SecurityBoundaryTest final : public QObject
{
    Q_OBJECT

private slots:
    void networkAndFtpSourcesDoNotEmitSensitivePayloads();
};

void SecurityBoundaryTest::networkAndFtpSourcesDoNotEmitSensitivePayloads()
{
    const QString root = QStringLiteral(CAMERA_MANAGER_SOURCE_DIR);
    const QStringList sources {
        root + QStringLiteral("/src/rv1126b/network/BoardApiClient.cpp"),
        root + QStringLiteral("/src/rv1126b/services/BoardFtpService.cpp"),
        root + QStringLiteral("/src/rv1126b/security/WindowsCredentialStore.cpp"),
    };
    const QStringList loggingMarkers {
        QStringLiteral("qDebug("),
        QStringLiteral("qInfo("),
        QStringLiteral("qWarning("),
        QStringLiteral("qCritical("),
        QStringLiteral("QLoggingCategory"),
    };

    for (const QString& path : sources) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        const QString source = QString::fromUtf8(file.readAll());
        for (const QString& marker : loggingMarkers) {
            QVERIFY2(!source.contains(marker), qPrintable(path + QStringLiteral(" contains ") + marker));
        }
    }

    QFile apiClientFile(sources.front());
    QVERIFY(apiClientFile.open(QIODevice::ReadOnly));
    const QString apiClientSource = QString::fromUtf8(apiClientFile.readAll());
    QVERIFY(apiClientSource.contains(QStringLiteral("authorization.fill('\\0')")));
    QVERIFY(apiClientSource.contains(QStringLiteral("token.clear()")));
}

QTEST_MAIN(SecurityBoundaryTest)

#include "SecurityBoundaryTest.moc"
