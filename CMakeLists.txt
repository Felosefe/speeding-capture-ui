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

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network Test)

option(BUILD_RTSP_SPIKE "Build the manual RV1126B RTSP validation tool" ON)

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
    src/rv1126b/services/EventSyncService.h
    src/rv1126b/services/EvidenceCache.h
    src/rv1126b/services/FtpService.h
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

if(BUILD_RTSP_SPIKE)
    find_package(Qt6 REQUIRED COMPONENTS Multimedia MultimediaWidgets)

    qt_add_executable(Rv1126bRtspSpike
        tools/rtsp_spike/main.cpp
        tools/rtsp_spike/QtMultimediaRtspPlayer.cpp
        tools/rtsp_spike/QtMultimediaRtspPlayer.h
        tools/rtsp_spike/SpikeWindow.cpp
        tools/rtsp_spike/SpikeWindow.h
        src/rv1126b/core/Result.h
        src/rv1126b/ports/IRtspPlayer.h
    )

    target_link_libraries(Rv1126bRtspSpike
        PRIVATE
            Qt6::Widgets
            Qt6::Network
            Qt6::Multimedia
            Qt6::MultimediaWidgets
    )

    if(WIN32)
        target_link_libraries(Rv1126bRtspSpike PRIVATE psapi)
    endif()

    target_compile_options(Rv1126bRtspSpike
        PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    )
endif()

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
