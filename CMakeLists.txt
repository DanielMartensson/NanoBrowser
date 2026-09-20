cmake_minimum_required(VERSION 3.16)

project(NanoBrowser VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick WebEngineQuick WebEngineCore)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(NanoBrowser
    src/main.cpp
)

qt_add_qml_module(NanoBrowser
    URI NanoBrowser
    VERSION 1.0
    QML_FILES
        qml/FullscreenButton.qml
        qml/Main.qml
        qml/NavButton.qml
)

set_target_properties(NanoBrowser PROPERTIES
    OUTPUT_NAME nanobrowser
)

target_link_libraries(NanoBrowser PRIVATE Qt6::Quick Qt6::WebEngineQuick Qt6::WebEngineCore)