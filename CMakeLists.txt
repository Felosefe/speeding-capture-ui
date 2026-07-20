cmake_minimum_required(VERSION 3.20)

project(CameraManagerApp
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Sql Network Test)

qt_add_executable(CameraManagerApp
    src/main.cpp
    src/device/IDeviceClient.cpp
    src/device/IDeviceClient.h
    src/device/MockDeviceClient.cpp
    src/device/MockDeviceClient.h
    src/models/CaptureRecord.h
    src/models/Device.h
    src/models/DeviceConfig.h
    src/models/DeviceStatus.h
    src/models/OperationLog.h
    src/models/SystemSettings.h
    src/models/table_models/CaptureRecordTableModel.cpp
    src/models/table_models/CaptureRecordTableModel.h
    src/models/table_models/DevicePropertyModel.cpp
    src/models/table_models/DevicePropertyModel.h
    src/models/table_models/DeviceTableModel.cpp
    src/models/table_models/DeviceTableModel.h
    src/rv1126b/core/Result.h
    src/rv1126b/core/ValueTypes.h
    src/rv1126b/domain/Models.h
    src/rv1126b/ports/IBoardApiClient.h
    src/rv1126b/ports/IEventRepository.h
    src/rv1126b/ports/IRtspPlayer.h
    src/rv1126b/ports/ISecretStore.h
    src/rv1126b/protocol/ApiCodec.h
    src/rv1126b/protocol/ApiDtos.h
    src/rv1126b/services/DeviceDiscoveryService.h
    src/rv1126b/services/DeviceFleetService.h
    src/rv1126b/services/DeviceSession.h
    src/rv1126b/services/BoardEventSyncService.cpp
    src/rv1126b/services/BoardEventSyncService.h
    src/rv1126b/services/BoardEvidenceCache.cpp
    src/rv1126b/services/BoardEvidenceCache.h
    src/rv1126b/services/BoardFtpTaskSnapshotService.cpp
    src/rv1126b/services/BoardFtpTaskSnapshotService.h
    src/rv1126b/services/EventSyncService.h
    src/rv1126b/services/EvidenceCache.h
    src/rv1126b/services/EvidenceCacheMaintenanceService.cpp
    src/rv1126b/services/EvidenceCacheMaintenanceService.h
    src/rv1126b/services/FtpService.h
    src/rv1126b/services/LocalEvidenceService.cpp
    src/rv1126b/services/LocalEvidenceService.h
    src/rv1126b/storage/SqliteEventRepository.cpp
    src/rv1126b/storage/SqliteEventRepository.h
    src/services/DeviceManager.cpp
    src/services/DeviceManager.h
    src/services/CaptureRecordService.cpp
    src/services/CaptureRecordService.h
    src/services/CaptureStorageService.cpp
    src/services/CaptureStorageService.h
    src/services/MaintenanceController.cpp
    src/services/MaintenanceController.h
    src/services/SystemSettingsService.cpp
    src/services/SystemSettingsService.h
    src/ui/DeviceConfigDialog.cpp
    src/ui/DeviceConfigDialog.h
    src/ui/SystemSettingsDialog.cpp
    src/ui/SystemSettingsDialog.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/video/VideoWidget.cpp
    src/video/VideoWidget.h
)

target_link_libraries(CameraManagerApp
    PRIVATE
        Qt6::Widgets
        Qt6::Sql
        Qt6::Network
)

enable_testing()

qt_add_executable(CaptureRecordServiceTest
    tests/CaptureRecordServiceTest.cpp
    src/services/CaptureRecordService.cpp
    src/services/CaptureRecordService.h
    src/models/CaptureRecord.h
)

target_link_libraries(CaptureRecordServiceTest
    PRIVATE
        Qt6::Core
        Qt6::Sql
        Qt6::Test
)

target_compile_options(CaptureRecordServiceTest
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

add_test(NAME CaptureRecordServiceTest COMMAND CaptureRecordServiceTest)

qt_add_executable(SystemSettingsServiceTest
    tests/SystemSettingsServiceTest.cpp
    src/models/SystemSettings.h
    src/services/SystemSettingsService.cpp
    src/services/SystemSettingsService.h
)

target_link_libraries(SystemSettingsServiceTest
    PRIVATE
        Qt6::Core
        Qt6::Test
)

target_compile_options(SystemSettingsServiceTest
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

add_test(NAME SystemSettingsServiceTest COMMAND SystemSettingsServiceTest)

qt_add_executable(CaptureStorageServiceTest
    tests/CaptureStorageServiceTest.cpp
    src/services/CaptureStorageService.cpp
    src/services/CaptureStorageService.h
    src/models/CaptureRecord.h
    src/models/SystemSettings.h
)

target_link_libraries(CaptureStorageServiceTest
    PRIVATE
        Qt6::Core
        Qt6::Test
)

target_compile_options(CaptureStorageServiceTest
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

add_test(NAME CaptureStorageServiceTest COMMAND CaptureStorageServiceTest)

qt_add_executable(MaintenanceControllerTest
    tests/MaintenanceControllerTest.cpp
    src/services/MaintenanceController.cpp
    src/services/MaintenanceController.h
    src/models/SystemSettings.h
)

target_link_libraries(MaintenanceControllerTest
    PRIVATE
        Qt6::Core
        Qt6::Test
)

target_compile_options(MaintenanceControllerTest
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

add_test(NAME MaintenanceControllerTest COMMAND MaintenanceControllerTest)

qt_add_executable(DeviceManagerTest
    tests/DeviceManagerTest.cpp
    src/services/DeviceManager.cpp
    src/services/DeviceManager.h
    src/device/IDeviceClient.cpp
    src/device/IDeviceClient.h
    src/device/MockDeviceClient.cpp
    src/device/MockDeviceClient.h
    src/models/CaptureRecord.h
    src/models/Device.h
    src/models/DeviceConfig.h
    src/models/DeviceStatus.h
)

target_link_libraries(DeviceManagerTest
    PRIVATE
        Qt6::Core
        Qt6::Test
)

target_compile_options(DeviceManagerTest
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_compile_definitions(DeviceManagerTest
    PRIVATE
        CAMERA_MANAGER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME DeviceManagerTest COMMAND DeviceManagerTest)

qt_add_executable(Rv1126bArchitectureTypesTest
    tests/Rv1126bArchitectureTypesTest.cpp
    src/rv1126b/core/Result.h
    src/rv1126b/core/ValueTypes.h
    src/rv1126b/domain/Models.h
    src/rv1126b/ports/IBoardApiClient.h
    src/rv1126b/ports/IEventRepository.h
    src/rv1126b/ports/IRtspPlayer.h
    src/rv1126b/ports/ISecretStore.h
    src/rv1126b/protocol/ApiCodec.h
    src/rv1126b/protocol/ApiDtos.h
    src/rv1126b/services/DeviceDiscoveryService.h
    src/rv1126b/services/DeviceFleetService.h
    src/rv1126b/services/DeviceSession.h
    src/rv1126b/services/BoardFtpTaskSnapshotService.h
    src/rv1126b/services/EventSyncService.h
    src/rv1126b/services/EvidenceCache.h
    src/rv1126b/services/FtpService.h
)

target_link_libraries(Rv1126bArchitectureTypesTest
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Widgets
        Qt6::Test
)

target_compile_options(Rv1126bArchitectureTypesTest
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

add_test(NAME Rv1126bArchitectureTypesTest COMMAND Rv1126bArchitectureTypesTest)

qt_add_executable(SqliteEventRepositoryTest
    tests/SqliteEventRepositoryTest.cpp
    src/rv1126b/core/Result.h
    src/rv1126b/core/ValueTypes.h
    src/rv1126b/domain/Models.h
    src/rv1126b/ports/IEventRepository.h
    src/rv1126b/protocol/ApiDtos.h
    src/rv1126b/storage/SqliteEventRepository.cpp
    src/rv1126b/storage/SqliteEventRepository.h
)

target_link_libraries(SqliteEventRepositoryTest
    PRIVATE
        Qt6::Core
        Qt6::Sql
        Qt6::Test
)

target_compile_options(SqliteEventRepositoryTest
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_compile_definitions(SqliteEventRepositoryTest
    PRIVATE
        CAMERA_MANAGER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME SqliteEventRepositoryTest COMMAND SqliteEventRepositoryTest)

qt_add_executable(EventSyncServiceTest
    tests/EventSyncServiceTest.cpp
    src/rv1126b/core/Result.h
    src/rv1126b/core/ValueTypes.h
    src/rv1126b/domain/Models.h
    src/rv1126b/ports/IBoardApiClient.h
    src/rv1126b/ports/IEventRepository.h
    src/rv1126b/protocol/ApiDtos.h
    src/rv1126b/services/BoardEventSyncService.cpp
    src/rv1126b/services/BoardEventSyncService.h
    src/rv1126b/services/EventSyncService.h
    src/rv1126b/storage/SqliteEventRepository.cpp
    src/rv1126b/storage/SqliteEventRepository.h
)

target_link_libraries(EventSyncServiceTest
    PRIVATE
        Qt6::Core
        Qt6::Sql
        Qt6::Test
)

target_compile_options(EventSyncServiceTest
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_compile_definitions(EventSyncServiceTest
    PRIVATE
        CAMERA_MANAGER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME EventSyncServiceTest COMMAND EventSyncServiceTest)

qt_add_executable(EvidenceCacheTest
    tests/EvidenceCacheTest.cpp
    src/rv1126b/core/Result.h
    src/rv1126b/core/ValueTypes.h
    src/rv1126b/domain/Models.h
    src/rv1126b/ports/IBoardApiClient.h
    src/rv1126b/ports/IEventRepository.h
    src/rv1126b/protocol/ApiDtos.h
    src/rv1126b/services/BoardEvidenceCache.cpp
    src/rv1126b/services/BoardEvidenceCache.h
    src/rv1126b/services/EvidenceCache.h
    src/rv1126b/storage/SqliteEventRepository.cpp
    src/rv1126b/storage/SqliteEventRepository.h
)

target_link_libraries(EvidenceCacheTest
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Sql
        Qt6::Test
)

target_compile_options(EvidenceCacheTest
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_compile_definitions(EvidenceCacheTest
    PRIVATE
        CAMERA_MANAGER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME EvidenceCacheTest COMMAND EvidenceCacheTest)

qt_add_executable(LocalEvidenceServiceTest
    tests/LocalEvidenceServiceTest.cpp
    src/rv1126b/core/Result.h
    src/rv1126b/core/ValueTypes.h
    src/rv1126b/domain/Models.h
    src/rv1126b/ports/IEventRepository.h
    src/rv1126b/protocol/ApiDtos.h
    src/rv1126b/services/EvidenceCache.h
    src/rv1126b/services/LocalEvidenceService.cpp
    src/rv1126b/services/LocalEvidenceService.h
    src/rv1126b/storage/SqliteEventRepository.cpp
    src/rv1126b/storage/SqliteEventRepository.h
)

target_link_libraries(LocalEvidenceServiceTest
    PRIVATE
        Qt6::Core
        Qt6::Sql
        Qt6::Test
)

target_compile_options(LocalEvidenceServiceTest
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_compile_definitions(LocalEvidenceServiceTest
    PRIVATE
        CAMERA_MANAGER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME LocalEvidenceServiceTest COMMAND LocalEvidenceServiceTest)

qt_add_executable(FtpTaskSnapshotServiceTest
    tests/FtpTaskSnapshotServiceTest.cpp
    src/rv1126b/core/Result.h
    src/rv1126b/core/ValueTypes.h
    src/rv1126b/domain/Models.h
    src/rv1126b/ports/IEventRepository.h
    src/rv1126b/protocol/ApiDtos.h
    src/rv1126b/services/BoardFtpTaskSnapshotService.cpp
    src/rv1126b/services/BoardFtpTaskSnapshotService.h
    src/rv1126b/storage/SqliteEventRepository.cpp
    src/rv1126b/storage/SqliteEventRepository.h
)

target_link_libraries(FtpTaskSnapshotServiceTest
    PRIVATE
        Qt6::Core
        Qt6::Sql
        Qt6::Test
)

target_compile_options(FtpTaskSnapshotServiceTest
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_compile_definitions(FtpTaskSnapshotServiceTest
    PRIVATE
        CAMERA_MANAGER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME FtpTaskSnapshotServiceTest COMMAND FtpTaskSnapshotServiceTest)

qt_add_executable(EvidenceCacheMaintenanceServiceTest
    tests/EvidenceCacheMaintenanceServiceTest.cpp
    src/models/SystemSettings.h
    src/rv1126b/services/EvidenceCacheMaintenanceService.cpp
    src/rv1126b/services/EvidenceCacheMaintenanceService.h
)

target_link_libraries(EvidenceCacheMaintenanceServiceTest
    PRIVATE
        Qt6::Core
        Qt6::Test
)

target_compile_options(EvidenceCacheMaintenanceServiceTest
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_compile_definitions(EvidenceCacheMaintenanceServiceTest
    PRIVATE
        CAMERA_MANAGER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME EvidenceCacheMaintenanceServiceTest COMMAND EvidenceCacheMaintenanceServiceTest)

target_compile_options(CameraManagerApp
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_compile_definitions(CameraManagerApp
    PRIVATE
        CAMERA_MANAGER_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if(WIN32)
    set_target_properties(CameraManagerApp PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()
