cmake_minimum_required(VERSION 3.20)
project(blindside VERSION 4.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform Compiler Optimization Flags
if(MSVC)
    add_compile_options(/O2 /fp:fast /W4 /permissive-)
else()
    add_compile_options(-O3 -ffast-math -Wall -Wextra -Wpedantic)
endif()

# Include Directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /usr/local/include
)

# Find Dependencies
find_package(Threads REQUIRED)

# OpenCV Search
find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio highgui objdetect calib3d)
if(OpenCV_FOUND)
    message(STATUS "OpenCV found version: ${OpenCV_VERSION}")
    include_directories(${OpenCV_INCLUDE_DIRS})
endif()

# Platform specific UI & OS libraries
set(PLATFORM_LIBS Threads::Threads)
set(PLATFORM_SOURCES "")

if(WIN32)
    list(APPEND PLATFORM_LIBS User32.lib Gdi32.lib Dwmapi.lib Comctl32.lib)
    list(APPEND PLATFORM_SOURCES src/platform/windows.cpp src/platform/windows_paths.cpp)
elseif(UNIX AND NOT APPLE)
    # Check for Wayland
    set(WAYLAND_DETECTED OFF)
    if(DEFINED ENV{WAYLAND_DISPLAY})
        set(WAYLAND_DETECTED ON)
        message(STATUS "Wayland environment detected. Using Wayland fallback platform.")
        list(APPEND PLATFORM_SOURCES src/platform/linux_wayland.cpp src/platform/linux_paths.cpp)
    else()
        find_package(X11 QUIET)
        if(X11_FOUND)
            message(STATUS "X11 environment detected.")
            include_directories(${X11_INCLUDE_DIR})
            list(APPEND PLATFORM_LIBS ${X11_LIBRARIES})
            add_compile_definitions(HAVE_X11)
            list(APPEND PLATFORM_SOURCES src/platform/linux_x11.cpp src/platform/linux_paths.cpp)
        else()
            message(WARNING "Neither Wayland nor X11 detected. Falling back to Wayland empty implementation.")
            list(APPEND PLATFORM_SOURCES src/platform/linux_wayland.cpp src/platform/linux_paths.cpp)
        endif()
    endif()
endif()

if(OpenCV_FOUND)
    list(APPEND PLATFORM_LIBS ${OpenCV_LIBS})
endif()

# Core Blindside Library
add_library(blindside_lib
    src/core/camera.cpp
    src/core/logger.cpp
    src/engine/face_detector.cpp
    src/engine/pose_estimator.cpp
    src/engine/eavesdropper_detector.cpp
    src/trigger/privacy_trigger.cpp
    src/daemon/daemon.cpp
    ${PLATFORM_SOURCES}
)

target_link_libraries(blindside_lib PUBLIC ${PLATFORM_LIBS})

# Main Executable
add_executable(blindside_daemon src/main.cpp)
target_link_libraries(blindside_daemon PRIVATE blindside_lib)

# Copy Models to output directory for all platforms
add_custom_command(TARGET blindside_daemon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/models" "$<TARGET_FILE_DIR:blindside_daemon>/models"
)

# Package Runtime DLLs for Windows
if(WIN32 AND OpenCV_FOUND)
    # Recursively find OpenCV DLLs and copy them next to the executable
    add_custom_command(TARGET blindside_daemon POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${OpenCV_DIR}/x64/vc16/bin" "$<TARGET_FILE_DIR:blindside_daemon>" 
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${OpenCV_DIR}/bin" "$<TARGET_FILE_DIR:blindside_daemon>" 
    )
endif()

# Enable CTest Testing
enable_testing()

add_executable(test_ring_buffer tests/test_ring_buffer.cpp)
target_link_libraries(test_ring_buffer PRIVATE blindside_lib)
add_test(NAME test_ring_buffer COMMAND test_ring_buffer)

add_executable(test_gaze_math tests/test_gaze_math.cpp)
target_link_libraries(test_gaze_math PRIVATE blindside_lib)
add_test(NAME test_gaze_math COMMAND test_gaze_math)

add_executable(test_eavesdropper tests/test_eavesdropper.cpp)
target_link_libraries(test_eavesdropper PRIVATE blindside_lib)
add_test(NAME test_eavesdropper COMMAND test_eavesdropper)

add_executable(test_liveness tests/test_liveness.cpp)
target_link_libraries(test_liveness PRIVATE blindside_lib)
add_test(NAME test_liveness COMMAND test_liveness)

add_executable(test_pipeline tests/test_pipeline.cpp)
target_link_libraries(test_pipeline PRIVATE blindside_lib)
add_test(NAME test_pipeline COMMAND test_pipeline)

add_executable(test_camera_failure tests/test_camera_failure.cpp)
target_link_libraries(test_camera_failure PRIVATE blindside_lib)
add_test(NAME test_camera_failure COMMAND test_camera_failure)

add_executable(test_platform tests/test_platform.cpp)
target_link_libraries(test_platform PRIVATE blindside_lib)
add_test(NAME test_platform COMMAND test_platform)
