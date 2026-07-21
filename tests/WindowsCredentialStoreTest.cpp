#include "../src/rv1126b/security/WindowsCredentialStore.h"

#include <QUuid>
#include <QtTest>

using namespace rv1126b;

class WindowsCredentialStoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void storesLoadsAndRemovesCredential();
    void rejectsEmptyDeviceId();
};

void WindowsCredentialStoreTest::storesLoadsAndRemovesCredential()
{
#ifndef Q_OS_WIN
    QSKIP("Windows Credential Manager is only available on Windows.");
#else
    WindowsCredentialStore store;
    const QString deviceId = QStringLiteral("codex-a5-%1").arg(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString credentialRef = store.credentialKeyForDevice(deviceId);
    QCOMPARE(credentialRef, QStringLiteral("RV1126B/") + deviceId);
    QVERIFY(store.removeToken(credentialRef).isSuccess());

    const QByteArray expectedToken = QByteArrayLiteral("credential-manager-test-token");
    const auto stored = store.storeToken(deviceId, SecretValue(expectedToken));
    QVERIFY(stored.isSuccess());
    QCOMPARE(stored.value(), credentialRef);

    const auto loaded = store.loadToken(credentialRef);
    QVERIFY(loaded.isSuccess());
    const QByteArrayView tokenView = loaded.value().view();
    QCOMPARE(QByteArray(tokenView.data(), tokenView.size()), expectedToken);

    QVERIFY(store.removeToken(credentialRef).isSuccess());
    const auto missing = store.loadToken(credentialRef);
    QVERIFY(!missing.isSuccess());
    QCOMPARE(missing.error().code, QStringLiteral("credential_not_found"));
#endif
}

void WindowsCredentialStoreTest::rejectsEmptyDeviceId()
{
    WindowsCredentialStore store;
    QCOMPARE(store.credentialKeyForDevice(QString()), QString());
    const auto stored = store.storeToken(QString(), SecretValue(QByteArrayLiteral("test")));
    QVERIFY(!stored.isSuccess());
    QCOMPARE(stored.error().code, QStringLiteral("credential_invalid_device_id"));
}

QTEST_MAIN(WindowsCredentialStoreTest)

#include "WindowsCredentialStoreTest.moc"
