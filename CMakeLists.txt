cmake_minimum_required(VERSION 3.24)

project(
    NvidiaAppCleaner
    VERSION 0.1.0
    DESCRIPTION "Cleanup utility for NVIDIA App driver installation leftovers"
    LANGUAGES CXX
)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(nvidia_app_cleaner_core
    src/core/cache_cleaner.cpp
    src/core/cache_scanner.cpp
    src/core/default_scan_targets.cpp
    src/core/driver_download_repair.cpp
)

target_include_directories(nvidia_app_cleaner_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(nvidia_app_cleaner_core PUBLIC cxx_std_20)

add_library(nvidia_app_cleaner_localization
    src/app/localization.cpp
)
target_include_directories(nvidia_app_cleaner_localization
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nvidia_app_cleaner_localization PUBLIC cxx_std_20)

add_library(nvidia_app_cleaner_application
    src/application/startup_options.cpp
)
target_include_directories(nvidia_app_cleaner_application
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(nvidia_app_cleaner_application PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(nvidia_app_cleaner_core PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(nvidia_app_cleaner_localization PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(nvidia_app_cleaner_application PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(nvidia_app_cleaner_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nvidia_app_cleaner_localization PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nvidia_app_cleaner_application PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_executable(NvidiaAppCleaner WIN32
        src/app/app.rc
        src/app/strings.rc
        src/app/main.cpp
        src/platform/windows/app_settings.cpp
        src/platform/windows/windows_platform.cpp
        src/ui/main_window.cpp
        src/ui/theme_manager.cpp
    )

    target_compile_features(NvidiaAppCleaner PRIVATE cxx_std_20)
    target_include_directories(NvidiaAppCleaner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(NvidiaAppCleaner PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(NvidiaAppCleaner PRIVATE
        nvidia_app_cleaner_core
        nvidia_app_cleaner_localization
        nvidia_app_cleaner_application
        advapi32
        comctl32
        dwmapi
        ole32
        shell32
        windowsapp
    )

    if(MSVC)
        target_compile_options(NvidiaAppCleaner PRIVATE /W4 /permissive- /utf-8)
        target_link_options(NvidiaAppCleaner PRIVATE "/MANIFEST:NO")
    endif()

    install(TARGETS NvidiaAppCleaner RUNTIME DESTINATION .)
else()
    message(STATUS "The Win32 GUI target is only built on Windows")
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(nvidia_app_cleaner_tests
        tests/cache_scanner_tests.cpp
    )
    add_executable(nvidia_app_cleaner_repair_tests
        tests/driver_download_repair_tests.cpp
    )
    add_executable(nvidia_app_cleaner_localization_tests
        tests/localization_tests.cpp
    )
    add_executable(nvidia_app_cleaner_startup_options_tests
        tests/startup_options_tests.cpp
    )
    target_compile_features(nvidia_app_cleaner_tests PRIVATE cxx_std_20)
    target_compile_features(nvidia_app_cleaner_repair_tests PRIVATE cxx_std_20)
    target_compile_features(nvidia_app_cleaner_localization_tests PRIVATE cxx_std_20)
    target_compile_features(nvidia_app_cleaner_startup_options_tests PRIVATE cxx_std_20)
    target_link_libraries(nvidia_app_cleaner_tests PRIVATE nvidia_app_cleaner_core)
    target_link_libraries(nvidia_app_cleaner_repair_tests PRIVATE nvidia_app_cleaner_core)
    target_link_libraries(nvidia_app_cleaner_localization_tests PRIVATE
        nvidia_app_cleaner_localization
    )
    target_link_libraries(nvidia_app_cleaner_startup_options_tests PRIVATE
        nvidia_app_cleaner_application
    )

    if(WIN32)
        target_sources(nvidia_app_cleaner_localization_tests PRIVATE src/app/strings.rc)
    endif()

    if(MSVC)
        target_compile_options(nvidia_app_cleaner_tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(nvidia_app_cleaner_repair_tests PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(nvidia_app_cleaner_localization_tests PRIVATE
            /W4 /permissive- /utf-8
        )
        target_compile_options(nvidia_app_cleaner_startup_options_tests PRIVATE
            /W4 /permissive- /utf-8
        )
    else()
        target_compile_options(nvidia_app_cleaner_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(nvidia_app_cleaner_repair_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(nvidia_app_cleaner_localization_tests PRIVATE
            -Wall -Wextra -Wpedantic
        )
        target_compile_options(nvidia_app_cleaner_startup_options_tests PRIVATE
            -Wall -Wextra -Wpedantic
        )
    endif()

    add_test(NAME cache_scanner COMMAND nvidia_app_cleaner_tests)
    add_test(NAME driver_download_repair COMMAND nvidia_app_cleaner_repair_tests)
    add_test(NAME localization COMMAND nvidia_app_cleaner_localization_tests)
    add_test(NAME startup_options COMMAND nvidia_app_cleaner_startup_options_tests)
endif()
