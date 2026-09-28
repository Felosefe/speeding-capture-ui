#pragma once

#include <QHostAddress>
#include <QObject>

class QTcpServer;

namespace rv1126b {

struct EmbeddedFtpReceiveServerConfig {
    QString rootPath;
    QString userName = QStringLiteral("upload");
    QString password;
    QHostAddress listenAddress = QHostAddress::AnyIPv4;
    QHostAddress advertisedAddress;
    quint16 controlPort = 0;
    quint16 passivePortStart = 0;
    quint16 passivePortEnd = 0;
};

class EmbeddedFtpReceiveServer final : public QObject
{
    Q_OBJECT

public:
    explicit EmbeddedFtpReceiveServer(QObject* parent = nullptr);
    ~EmbeddedFtpReceiveServer() override;

    bool start(const EmbeddedFtpReceiveServerConfig& config);
    void stop();

    bool isListening() const;
    quint16 controlPort() const;
    QString lastError() const;
    EmbeddedFtpReceiveServerConfig config() const;

signals:
    void fileStored(const QString& relativePath, qint64 bytes);

private:
    class Session;

    friend class Session;

    QString resolvePath(const QString& cwd, const QString& ftpPath, bool* ok, QString* relativePath) const;
    QTcpServer* createPassiveServer(QString* errorMessage) const;
    QHostAddress passiveReplyAddress() const;
    void removeSession(Session* session);

    EmbeddedFtpReceiveServerConfig config_;
    QTcpServer* controlServer_ = nullptr;
    QList<Session*> sessions_;
    QString lastError_;
};

} // namespace rv1126b
