cmake_minimum_required(VERSION 3.16)

project(NanoBrowser VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick WebEngineQuick WebEngineCore)

qt_standard_project_setup(REQUIRES 6.8)

# Which Qt Quick backend to use when NANOBROWSER_RENDERER says nothing.
#   auto   - Vulkan when the GPU has a usable 1.2+ driver, OpenGL otherwise
#   opengl - OpenGL, which shares one GL context with Chromium (zero copy)
#   vulkan - Vulkan
# The board build picks opengl: there the Vulkan backend composites Chromium's
# tiles in software and the driver does not survive the window teardown.
set(NANOBROWSER_DEFAULT_RENDERER "auto" CACHE STRING
    "Default graphics backend: auto, opengl or vulkan")
if(NOT NANOBROWSER_DEFAULT_RENDERER MATCHES "^(auto|opengl|vulkan)$")
    message(FATAL_ERROR "NANOBROWSER_DEFAULT_RENDERER must be auto, opengl or vulkan")
endif()


qt_add_executable(NanoBrowser
    src/main.cpp
)

qt_add_qml_module(NanoBrowser
    URI NanoBrowser
    VERSION 1.0
    QML_FILES
        qml/FullscreenButton.qml
        qml/IconButton.qml
        qml/Main.qml
        qml/NavButton.qml
        qml/WindowButton.qml
)

set_target_properties(NanoBrowser PROPERTIES
    OUTPUT_NAME nanobrowser
)

target_link_libraries(NanoBrowser PRIVATE Qt6::Quick Qt6::WebEngineQuick Qt6::WebEngineCore)

target_compile_definitions(NanoBrowser PRIVATE
    NANOBROWSER_VERSION="${PROJECT_VERSION}"
    NANOBROWSER_DEFAULT_RENDERER="${NANOBROWSER_DEFAULT_RENDERER}"
)