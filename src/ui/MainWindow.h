#pragma once

#include "../models/CaptureRecord.h"
#include "../models/Device.h"
#include "../models/DeviceStatus.h"
#include "../models/SystemSettings.h"
#include "../services/CaptureStorageService.h"

#include <QMainWindow>

class QAction;
class QLabel;
class QMenu;
class QPoint;
class QSortFilterProxyModel;
class QSpinBox;
class QTableView;
class QTabWidget;
class QTimer;
class QToolButton;
class QWidget;

class CaptureRecordTableModel;
class CaptureRecordService;
enum class CaptureRecordFilter;
class DeviceManager;
class DevicePropertyModel;
class DeviceTableModel;
class MaintenanceController;
class QEvent;
class QSplitter;
class SystemSettingsService;
class VideoWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void addDevice();
    void connectSelectedDevice();
    void disconnectSelectedDevice();
    void openDeviceConfig();
    void triggerCapture();
    void rebootSelectedDevice();
    void syncSelectedDeviceTime();
    void openGlobalSettings();
    void refreshCaptureRecords();
    void toggleCapturePause(bool paused);
    void finishCapturePause();
    void exportCaptureRecords();
    void deleteSelectedCapture();
    void clearCaptureRecords();
    void showActionMessage();
    void updateDeviceProperties();
    void showDeviceContextMenu(const QPoint& position);
    void handleDeviceAdded(const Device& device);
    void handleDeviceStatusChanged(int row, const DeviceStatus& status);
    void handleDeviceConfigChanged(int row, const DeviceConfig& config);
    void handleCaptureGenerated(int row, const CaptureRecord& record);
    void handleDeviceError(int row, const QString& message);

private:
    void createActions();
    void createToolBar();
    void createCentralLayout();
    void createStatusBar();
    void createDeviceContextMenu();
    void connectDeviceManager();
    void populateInitialData();
    void updateStatusText();
    void selectDeviceRow(int row);
    void applySystemSettings(bool initialApply = false);
    void applyCaptureColumnVisibility();
    void configureMaintenanceController();
    void performDiskMaintenance();
    void runScheduledShutdown();
    void updateStartupRegistration(bool enabled);
    void updatePreviewLayout();

    int currentDeviceRow() const;
    int currentCaptureRow() const;

    QTableView* createDeviceTable();
    QTableView* createPropertyTable();
    QWidget* createCaptureRecordPanel();
    QTableView* createCaptureTable(QSortFilterProxyModel* proxyModel);
    QSortFilterProxyModel* createCaptureProxy(const QString& plateStateFilter);
    void configureTableView(QTableView* table) const;
    void updateVideoWidgets();
    void updateCaptureControls();
    QTableView* currentCaptureTable() const;
    QSortFilterProxyModel* currentCaptureProxy() const;
    CaptureRecordFilter currentCaptureFilter() const;
    CaptureAssetKind storageKindForRecord(const CaptureRecord& record) const;

    QAction* addDeviceAction_ = nullptr;
    QAction* connectAction_ = nullptr;
    QAction* disconnectAction_ = nullptr;
    QAction* configAction_ = nullptr;
    QAction* captureAction_ = nullptr;
    QAction* rebootAction_ = nullptr;
    QAction* syncTimeAction_ = nullptr;
    QAction* deleteCaptureAction_ = nullptr;
    QAction* clearCaptureAction_ = nullptr;
    QAction* globalSettingsAction_ = nullptr;
    QAction* refreshAction_ = nullptr;
    QAction* exportAction_ = nullptr;

    QTableView* deviceTable_ = nullptr;
    QTableView* propertyTable_ = nullptr;
    QTabWidget* captureTabs_ = nullptr;
    QTableView* allCaptureTable_ = nullptr;
    QTableView* validCaptureTable_ = nullptr;
    QTableView* unknownCaptureTable_ = nullptr;
    QSortFilterProxyModel* allCaptureProxy_ = nullptr;
    QSortFilterProxyModel* validCaptureProxy_ = nullptr;
    QSortFilterProxyModel* unknownCaptureProxy_ = nullptr;
    QToolButton* pauseCaptureButton_ = nullptr;
    QSpinBox* pauseSecondsSpin_ = nullptr;
    QLabel* pendingCaptureLabel_ = nullptr;
    VideoWidget* livePreview_ = nullptr;
    VideoWidget* snapshotPreview_ = nullptr;
    QSplitter* previewSplitter_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer* capturePauseTimer_ = nullptr;
    QMenu* deviceContextMenu_ = nullptr;

    DeviceManager* deviceManager_ = nullptr;
    CaptureRecordService* captureService_ = nullptr;
    SystemSettingsService* systemSettingsService_ = nullptr;
    MaintenanceController* maintenanceController_ = nullptr;
    DeviceTableModel* deviceModel_ = nullptr;
    DevicePropertyModel* propertyModel_ = nullptr;
    CaptureRecordTableModel* captureModel_ = nullptr;
    SystemSettings currentSystemSettings_;
    CaptureStorageService storageService_;
    int pendingCaptureCount_ = 0;
};
