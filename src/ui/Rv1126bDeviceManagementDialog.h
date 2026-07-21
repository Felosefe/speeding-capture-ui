#pragma once

#include "../rv1126b/application/DeviceOperationsController.h"

#include <QDialog>
#include <QVector>

#include <optional>

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTimer;

class Rv1126bDeviceManagementDialog final : public QDialog
{
    Q_OBJECT

public:
    enum class InitialPage { Evidence, Time, FtpConfig, FtpTasks };

    explicit Rv1126bDeviceManagementDialog(
        const QString& deviceId,
        rv1126b::DeviceOperationsController* controller,
        InitialPage initialPage = InitialPage::Evidence,
        QWidget* parent = nullptr,
        bool deviceOnline = true);
    ~Rv1126bDeviceManagementDialog() override;

protected:
    void reject() override;

private:
    QWidget* createEvidencePage();
    QWidget* createTimePage();
    QWidget* createFtpConfigPage();
    QWidget* createFtpTasksPage();
    void connectController();
    void showError(const QString& code, const QString& message);
    void applyEvidence(const rv1126b::EvidenceConfigDto& config);
    void applyTime(const rv1126b::TimeStatusDto& time);
    void applyFtpConfig(const rv1126b::FtpConfigSnapshotDto& config);
    void addFtpTargetRow(const std::optional<rv1126b::FtpTargetSnapshotDto>& target = std::nullopt);
    rv1126b::FtpConfigUpdate collectFtpConfig() const;
    bool validateFtpRowsInline();
    void clearPasswordEditors();
    void applyFtpControl(const rv1126b::FtpControlDto& control);
    void applyTaskPage(const rv1126b::FtpTaskPageDto& page, const QString& requestedCursor);
    void applyTaskDetail(const rv1126b::FtpTaskDetailDto& detail);
    void applyLocalTaskSnapshots(const QVector<rv1126b::StoredFtpTask>& tasks);
    void applyLocalTaskDetail(const rv1126b::StoredFtpTask& task);
    void createTask();
    void refreshTasks();
    void updateTaskRefreshState();
    void showRevisionConflict(const rv1126b::FtpConfigSnapshotDto& remote,
                              const rv1126b::FtpConfigUpdate& local,
                              const QStringList& passwordTargetIds);
    QString conflictSummary(const rv1126b::FtpConfigSnapshotDto& remote,
                            const rv1126b::FtpConfigUpdate& local) const;

    QString deviceId_;
    rv1126b::DeviceOperationsController* controller_ = nullptr;
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

    QLabel* ftpRevisionLabel_ = nullptr;
    QSpinBox* retryMaxSpin_ = nullptr;
    QSpinBox* retryIntervalSpin_ = nullptr;
    QSpinBox* connectTimeoutSpin_ = nullptr;
    QSpinBox* transferTimeoutSpin_ = nullptr;
    QSpinBox* scanIntervalSpin_ = nullptr;
    QTableWidget* ftpTargetsTable_ = nullptr;
    QCheckBox* autoEnabledCheck_ = nullptr;
    QComboBox* autoScopeCombo_ = nullptr;
    QLabel* ftpStatus_ = nullptr;
    QPushButton* saveFtpButton_ = nullptr;
    QString ftpRevision_;

    QDateTimeEdit* taskStartEdit_ = nullptr;
    QDateTimeEdit* taskEndEdit_ = nullptr;
    QListWidget* taskTargets_ = nullptr;
    QTableWidget* taskTable_ = nullptr;
    QTableWidget* taskDetailTable_ = nullptr;
    QPushButton* previousTasksButton_ = nullptr;
    QPushButton* nextTasksButton_ = nullptr;
    QPushButton* retryTaskButton_ = nullptr;
    QPushButton* createTaskButton_ = nullptr;
    QLabel* taskPageLabel_ = nullptr;
    QTimer* taskRefreshTimer_ = nullptr;
    QStringList taskPageCursors_ {QString()};
    int taskPageIndex_ = 0;
    std::optional<QString> nextTaskCursor_;
    QString selectedTaskId_;
    rv1126b::FtpTaskState selectedTaskState_ = rv1126b::FtpTaskState::Unknown;
    QVector<rv1126b::StoredFtpTask> localTaskSnapshots_;
    bool deviceOnline_ = true;
};

