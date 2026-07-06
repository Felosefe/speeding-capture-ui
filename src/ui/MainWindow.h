#pragma once

#include <QMainWindow>

class QAction;
class QLabel;
class QMenu;
class QTableWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void addDevice();
    void connectSelectedDevice();
    void disconnectSelectedDevice();
    void openDeviceConfig();
    void triggerCapture();
    void showActionMessage();
    void updateDeviceProperties();
    void showDeviceContextMenu(const QPoint& position);

private:
    void createActions();
    void createToolBar();
    void createCentralLayout();
    void createStatusBar();
    void createDeviceContextMenu();
    void populateEmptyTables();
    void resetPropertyTable();

    QWidget* createPreviewPanel(const QString& title, const QString& subtitle) const;
    QTableWidget* createDeviceTable();
    QTableWidget* createPropertyTable();
    QTableWidget* createCaptureTable();

    QAction* addDeviceAction_ = nullptr;
    QAction* connectAction_ = nullptr;
    QAction* disconnectAction_ = nullptr;
    QAction* configAction_ = nullptr;
    QAction* captureAction_ = nullptr;
    QAction* globalSettingsAction_ = nullptr;
    QAction* refreshAction_ = nullptr;
    QAction* exportAction_ = nullptr;

    QTableWidget* deviceTable_ = nullptr;
    QTableWidget* propertyTable_ = nullptr;
    QTableWidget* captureTable_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QMenu* deviceContextMenu_ = nullptr;
};
