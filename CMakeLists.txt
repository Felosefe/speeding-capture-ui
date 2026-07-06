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

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(CameraManagerApp
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
)

target_link_libraries(CameraManagerApp
    PRIVATE
        Qt6::Widgets
)

target_compile_options(CameraManagerApp
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

if(WIN32)
    set_target_properties(CameraManagerApp PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()
