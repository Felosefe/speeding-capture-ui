#pragma once

#include "../rv1126b/ports/IRtspPlayer.h"

#include <QWidget>

class QComboBox;
class QLabel;

class LivePreviewPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit LivePreviewPanel(rv1126b::IRtspPlayer* player, QWidget* parent = nullptr);

    void setCurrentDevice(const QString& deviceId);

public slots:
    void setPlaybackState(rv1126b::RtspPlayerState state);
    void setPlaybackError(const rv1126b::ApiError& error);

signals:
    void streamRoleChanged(rv1126b::RtspStreamRole role);

private:
    QLabel* deviceLabel_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QComboBox* streamCombo_ = nullptr;
};

