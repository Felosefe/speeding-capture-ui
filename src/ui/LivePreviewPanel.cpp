#include "LivePreviewPanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {

QString playbackStateText(rv1126b::RtspPlayerState state)
{
    using rv1126b::RtspPlayerState;
    switch (state) {
    case RtspPlayerState::Opening:
        return QStringLiteral("正在打开视频…");
    case RtspPlayerState::Playing:
        return QStringLiteral("正在播放");
    case RtspPlayerState::Reconnecting:
        return QStringLiteral("视频重连中…");
    case RtspPlayerState::Stopped:
        return QStringLiteral("视频已停止");
    case RtspPlayerState::Error:
        return QStringLiteral("视频播放失败");
    case RtspPlayerState::Idle:
    default:
        return QStringLiteral("请选择并连接设备");
    }
}

} // namespace

LivePreviewPanel::LivePreviewPanel(rv1126b::IRtspPlayer* player, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("livePreviewPanel"));
    setMinimumSize(320, 220);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    auto* header = new QHBoxLayout;
    deviceLabel_ = new QLabel(QStringLiteral("实时预览"), this);
    deviceLabel_->setObjectName(QStringLiteral("liveDeviceLabel"));
    stateLabel_ = new QLabel(playbackStateText(rv1126b::RtspPlayerState::Idle), this);
    stateLabel_->setObjectName(QStringLiteral("livePlaybackStateLabel"));
    streamCombo_ = new QComboBox(this);
    streamCombo_->setObjectName(QStringLiteral("rtspStreamRoleCombo"));
    streamCombo_->addItem(QStringLiteral("辅码流 /live/1"),
                          static_cast<int>(rv1126b::RtspStreamRole::Sub));
    streamCombo_->addItem(QStringLiteral("主码流 /live/0"),
                          static_cast<int>(rv1126b::RtspStreamRole::Main));

    header->addWidget(deviceLabel_);
    header->addStretch(1);
    header->addWidget(stateLabel_);
    header->addWidget(streamCombo_);
    root->addLayout(header);

    QWidget* output = player ? player->outputWidget() : nullptr;
    if (output) {
        output->setObjectName(QStringLiteral("rtspOutputWidget"));
        root->addWidget(output, 1);
    } else {
        auto* unavailable = new QLabel(QStringLiteral("实时视频播放器尚未装配"), this);
        unavailable->setObjectName(QStringLiteral("rtspUnavailableLabel"));
        unavailable->setAlignment(Qt::AlignCenter);
        unavailable->setStyleSheet(QStringLiteral("background: #121820; color: #96a5b4;"));
        root->addWidget(unavailable, 1);
    }

    connect(streamCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        emit streamRoleChanged(static_cast<rv1126b::RtspStreamRole>(
            streamCombo_->currentData().toInt()));
    });
}

void LivePreviewPanel::setCurrentDevice(const QString& deviceId)
{
    deviceLabel_->setText(deviceId.isEmpty()
                              ? QStringLiteral("实时预览")
                              : QStringLiteral("实时预览 · %1").arg(deviceId));
}

void LivePreviewPanel::setPlaybackState(rv1126b::RtspPlayerState state)
{
    stateLabel_->setText(playbackStateText(state));
    stateLabel_->setStyleSheet(state == rv1126b::RtspPlayerState::Error
                                   ? QStringLiteral("color: #b42318;")
                                   : QString());
}

void LivePreviewPanel::setPlaybackError(const rv1126b::ApiError& error)
{
    Q_UNUSED(error)
    stateLabel_->setText(QStringLiteral("视频播放失败，等待处理"));
    stateLabel_->setStyleSheet(QStringLiteral("color: #b42318;"));
}

