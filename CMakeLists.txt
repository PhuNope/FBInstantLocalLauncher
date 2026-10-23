cmake_minimum_required(VERSION 3.21)
project(FBInstantLocalLauncher VERSION 1.2.5 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LAUNCHER_GITHUB_REPO "" CACHE STRING "Public GitHub repo owner/name for update checks")
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network Concurrent)
qt_standard_project_setup()
qt_add_executable(FBInstantLocalLauncher WIN32 src/main.cpp src/MainWindow.cpp src/MainWindow.h src/UploadDialog.cpp src/UploadDialog.h)
target_link_libraries(FBInstantLocalLauncher PRIVATE Qt6::Widgets Qt6::Network Qt6::Concurrent)
set_target_properties(FBInstantLocalLauncher PROPERTIES OUTPUT_NAME "FBInstantLocalLauncher")
install(TARGETS FBInstantLocalLauncher RUNTIME DESTINATION .)

if(LAUNCHER_GITHUB_REPO)
    target_compile_definitions(FBInstantLocalLauncher PRIVATE LAUNCHER_GITHUB_REPO="${LAUNCHER_GITHUB_REPO}")
endif()
target_compile_definitions(FBInstantLocalLauncher PRIVATE LAUNCHER_VERSION="${PROJECT_VERSION}")
add_custom_command(TARGET FBInstantLocalLauncher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/updater.ps1"
        "$<TARGET_FILE_DIR:FBInstantLocalLauncher>/updater.ps1")
install(FILES scripts/updater.ps1 DESTINATION .)

if(WIN32)
    target_link_libraries(FBInstantLocalLauncher PRIVATE Advapi32)
endif()
