cmake_minimum_required(VERSION 3.16)
project(CameraStreamingApp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network WebEngineWidgets)

# Enable Qt MOC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Source files
set(SOURCES
    main.cpp
    mainwindow.cpp
    httpserver.cpp
    janusconnector.cpp
    cameramanager.cpp
)

# Header files
set(HEADERS
    mainwindow.h
    cameraparams.h
    httpserver.h
    janusconnector.h
    cameramanager.h
)

# UI files (if you have any)
set(UI_FILES
    mainwindow.ui
)

# Resource files
set(RESOURCES
    resources.qrc  # Contains janus.js in :/scripts/janus.js
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${UI_FILES} ${RESOURCES}



)

# Link Qt libraries
target_link_libraries(${PROJECT_NAME}
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::WebEngineWidgets
)
