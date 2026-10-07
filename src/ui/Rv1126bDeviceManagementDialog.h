#pragma once

#include "../rv1126b/application/DeviceOperationsController.h"

#include <QDialog>
#include <QVector>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace rv1126b {
class EmbeddedFtpReceiveServer;
class EventSyncService;
class IBoardApiClient;
class IEventRepository;
class DetectionResultWriter;
class BoardDataPullService;

// 「一键把板端数据拉回本机文件夹」需要的两个依赖（都允许为空，为空时按钮置灰）。
struct DetectionPullDependencies {
    IEventRepository* repository = nullptr;
    DetectionResultWriter* writer = nullptr;
};
}

class Rv1126bDeviceManagementDialog final : public QDialog
{
    Q_OBJECT

public:
    /* 只剩三个页签：展示配置 / 时间 / 事件同步。ISP、FTP 配置、FTP 历史任务已删除。 */
    enum class InitialPage { Evidence, Time, EventSync };

    explicit Rv1126bDeviceManagementDialog(
        const QString& deviceId,
        rv1126b::DeviceOperationsController* controller,
        InitialPage initialPage = InitialPage::Evidence,
        QWidget* parent = nullptr,
        bool deviceOnline = true,
        rv1126b::EmbeddedFtpReceiveServer* ftpReceiveServer = nullptr,
        const QString& localFtpRootPath = QString(),
        rv1126b::EventSyncService* eventSyncService = nullptr,
        const QString& evidenceRootPath = QString(),
        const QString& deviceEndpointText = QString(),
        rv1126b::IBoardApiClient* boardApi = nullptr,
        std::function<rv1126b::IBoardApiClient*(const QString&)> boardApiForHost = {},
        std::function<QString(const QString&)> deviceIdForHost = {},
        const QString& storageRootPath = QString(),
        std::function<bool(const QString&)> storageRootChangeHandler = {},
        rv1126b::DetectionPullDependencies detectionPull = {});
    ~Rv1126bDeviceManagementDialog() override;

protected:
    void reject() override;

private:
    QWidget* createEvidencePage();
    QWidget* createTimePage();
    QWidget* createEventSyncPage();
    /* 目标电脑（指定 IP / UNC 共享）的选择与保存 */
    void browseEventSyncTargetFolder();
    void applyEventSyncTargetFolder();
    void connectController();
    void showError(const QString& code, const QString& message);
    void applyEvidence(const rv1126b::EvidenceConfigDto& config);
    void applyTime(const rv1126b::TimeStatusDto& time);
    QString defaultEventStorageRoot() const;
    QString currentEventStorageRoot() const;
    // 「一键拉回本机文件夹」：按板端 cursor 翻页读事件 + 详情，写成用户可读资料包。
    void startBoardDataPull();
    void cancelBoardDataPull();
    void handleBoardDataPullProgress(int seen, int written, int failed);
    void handleBoardDataPullFinished(int seen, int written, int failed, bool cancelled);
    QString currentEventExportRoot() const;
    QStringList eventExportHosts() const;
    void browseEventStorageRoot();
    void saveEventStorageRoot();
    void startEventExport();
    void requestEventExportPage(const std::optional<QString>& cursor = std::nullopt);
    void handleEventExportPage(rv1126b::ApiResult<rv1126b::EventPageDto> result);
    void startNextEventExportTarget();
    void exportNextEvent();
    void handleExportEventDetail(
        const rv1126b::EventSummaryDto& summary,
        rv1126b::ApiResult<rv1126b::EventDetailDto> result);
    void downloadNextExportFile();
    void handleExportFileDownloaded(rv1126b::ApiResult<rv1126b::EvidenceDownloadResult> result);
    void finishEventExport();

    QString deviceId_;
    rv1126b::DeviceOperationsController* controller_ = nullptr;
    /*
     * 内置 FTP 接收服务与本地接收目录：ISP / FTP 配置 / FTP 历史任务三页删掉以后，
     * 这个对话框已经不用它们了。构造参数与成员暂时保留，因为 MainWindow 和
     * Rv1126bApplicationRuntime 的依赖装配仍在提供它们（清服务层是另一项独立重构）。
     */
    rv1126b::EmbeddedFtpReceiveServer* ftpReceiveServer_ = nullptr;
    rv1126b::EventSyncService* eventSyncService_ = nullptr;
    rv1126b::IBoardApiClient* boardApi_ = nullptr;
    rv1126b::IBoardApiClient* currentExportApi_ = nullptr;
    std::function<rv1126b::IBoardApiClient*(const QString&)> boardApiForHost_;
    std::function<QString(const QString&)> deviceIdForHost_;
    std::function<bool(const QString&)> storageRootChangeHandler_;
    QString localFtpRootPath_;
    QString evidenceRootPath_;
    QString storageRootPath_;
    QString deviceEndpointText_;
    QTabWidget* tabs_ = nullptr;
    QLabel* globalMessage_ = nullptr;

    QLineEdit* siteNameEdit_ = nullptr;
    QLineEdit* roadDirectionEdit_ = nullptr;
    QSpinBox* speedLimitSpin_ = nullptr;
    QLineEdit* statusTextEdit_ = nullptr;
    QLineEdit* codeTextEdit_ = nullptr;
    QLabel* evidenceStatus_ = nullptr;

    QLabel* utcTimeLabel_ = nullptr;
    QLabel* localTimeLabel_ = nullptr;
    QLabel* sourceEpochLabel_ = nullptr;
    QLabel* offsetLabel_ = nullptr;
    QLabel* timeQualityLabel_ = nullptr;
    QLabel* ntpStatusLabel_ = nullptr;
    QLabel* timeWriteLabel_ = nullptr;
    QPushButton* syncTimeButton_ = nullptr;
    bool timeSetEnabled_ = false;

    QLabel* eventSyncStatus_ = nullptr;
    /*
     * 「已同步 N 条 · 最后同步 HH:mm:ss」。以前是文件级静态指针，多个设备的对话框
     * 会互相覆盖，对话框析构后还会留下悬空指针——现在是本对话框自己的成员。
     */
    QLabel* eventSyncCountLabel_ = nullptr;
    QLabel* eventSyncModeLabel_ = nullptr;
    QLabel* eventSyncRootLabel_ = nullptr;
    QPushButton* eventSyncStartButton_ = nullptr;
    QPushButton* eventSyncStopButton_ = nullptr;
    QPushButton* eventSyncPollButton_ = nullptr;
    QPushButton* eventSyncAllButton_ = nullptr;
    QPushButton* eventSyncFromNowButton_ = nullptr;
    QPushButton* boardPullButton_ = nullptr;
    /* 拉取的时间范围（2026-10-07）：今天 / 最近一周 / 最近一月 / 全部 */
    QComboBox* boardPullRangeCombo_ = nullptr;
    QLabel* boardPullStatus_ = nullptr;
    rv1126b::BoardDataPullService* boardPullService_ = nullptr;
    rv1126b::DetectionPullDependencies detectionPull_;
    /*
     * 目标电脑（2026-10-07）：拉取结果的落地目录。留空 = 本机默认目录；
     * 填 UNC（\\对方IP\共享名\子目录）就是把资料包直接写到局域网内指定 IP 的电脑上。
     */
    QLineEdit* eventSyncTargetEdit_ = nullptr;
    QSpinBox* eventExportDaysSpin_ = nullptr;
    QLineEdit* eventStorageRootEdit_ = nullptr;
    QPushButton* eventStorageBrowseButton_ = nullptr;
    QPushButton* eventStorageSaveButton_ = nullptr;
    QComboBox* eventExportRangeCombo_ = nullptr;
    QCheckBox* exportEvidenceCheck_ = nullptr;
    QCheckBox* exportSnapshotCheck_ = nullptr;
    QCheckBox* exportFormalCheck_ = nullptr;
    QCheckBox* exportOcrCheck_ = nullptr;
    QCheckBox* exportDetailCheck_ = nullptr;
    QCheckBox* exportSummaryCheck_ = nullptr;
    QCheckBox* exportTrackMetaCheck_ = nullptr;
    QLabel* eventExportStatus_ = nullptr;
    QPushButton* eventExportButton_ = nullptr;
    struct ExportFile {
        QString url;
        QString finalPath;
        QString partPath;
        bool optional = true;
    };
    QVector<rv1126b::EventSummaryDto> exportEvents_;
    QVector<ExportFile> exportFiles_;
    QStringList exportTargetHosts_;
    QString exportRunRoot_;
    QString exportTargetRoot_;
    QString exportTargetDeviceId_;
    QString exportTargetHost_;
    int exportEventIndex_ = 0;
    int exportFileIndex_ = 0;
    int exportTargetIndex_ = 0;
    int exportSucceeded_ = 0;
    int exportFailed_ = 0;
    bool exportInFlight_ = false;

    bool deviceOnline_ = true;
};

