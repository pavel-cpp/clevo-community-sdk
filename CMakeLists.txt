cmake_minimum_required(VERSION 3.21)

project(ClevoCommunitySDK
    VERSION 0.1.0
    DESCRIPTION "C++ SDK for Clevo laptops driven through the Insyde DCHU driver"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "ClevoCommunitySDK talks to the Insyde DCHU driver and only builds for Windows.")
endif()

include(GenerateExportHeader)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Follows the standard BUILD_SHARED_LIBS unless set explicitly, so a parent
# project can pick the SDK's linkage independently of its own libraries.
set(_clevo_sdk_default_shared OFF)
if(BUILD_SHARED_LIBS)
    set(_clevo_sdk_default_shared ON)
endif()
option(CLEVO_SDK_BUILD_SHARED "Build ClevoCommunitySDK as a shared library (DLL)" ${_clevo_sdk_default_shared})
option(CLEVO_SDK_BUILD_TESTS "Build the ClevoCommunitySDK unit tests" ${PROJECT_IS_TOP_LEVEL})
option(CLEVO_SDK_BUILD_EXAMPLES "Build the clevoctl example" ${PROJECT_IS_TOP_LEVEL})
option(CLEVO_SDK_INSTALL "Generate install rules for ClevoCommunitySDK" ${PROJECT_IS_TOP_LEVEL})

if(CLEVO_SDK_BUILD_SHARED)
    set(CLEVO_SDK_LIBRARY_TYPE SHARED)
else()
    set(CLEVO_SDK_LIBRARY_TYPE STATIC)
endif()

add_library(ClevoCommunitySDK ${CLEVO_SDK_LIBRARY_TYPE}
    include/clevo/Capabilities.hpp
    include/clevo/Clevo.hpp
    include/clevo/Device.hpp
    include/clevo/Error.hpp
    include/clevo/Fans.hpp
    include/clevo/Keyboard.hpp
    include/clevo/LightingEffects.hpp
    include/clevo/Power.hpp
    include/clevo/System.hpp
    include/clevo/Transport.hpp
    src/Capabilities.cpp
    src/Device.cpp
    src/Fans.cpp
    src/InsydeTransport.cpp
    src/Keyboard.cpp
    src/LightingEffects.cpp
    src/Power.cpp
    src/Protocol.hpp
    src/System.cpp
)
add_library(ClevoCommunitySDK::ClevoCommunitySDK ALIAS ClevoCommunitySDK)

set(CLEVO_SDK_EXPORT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/include/clevo/Export.hpp")
generate_export_header(ClevoCommunitySDK
    BASE_NAME CLEVO_SDK
    EXPORT_FILE_NAME "${CLEVO_SDK_EXPORT_HEADER}"
)
target_sources(ClevoCommunitySDK PRIVATE "${CLEVO_SDK_EXPORT_HEADER}")

set_target_properties(ClevoCommunitySDK PROPERTIES
    VERSION ${PROJECT_VERSION}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

if(NOT CLEVO_SDK_BUILD_SHARED)
    # Consumers of the static library must not see dllimport declarations.
    target_compile_definitions(ClevoCommunitySDK PUBLIC CLEVO_SDK_STATIC_DEFINE)
endif()

target_include_directories(ClevoCommunitySDK
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(ClevoCommunitySDK PUBLIC cxx_std_23)
target_compile_definitions(ClevoCommunitySDK PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(ClevoCommunitySDK PRIVATE user32)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ClevoCommunitySDK PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(ClevoCommunitySDK PRIVATE /W4 /permissive-)
endif()

set_property(GLOBAL PROPERTY CLEVO_SDK_RUNTIME_DLL "${CMAKE_CURRENT_SOURCE_DIR}/driver/InsydeDCHU.dll")
include(cmake/ClevoCommunitySDKRuntime.cmake)

if(CLEVO_SDK_BUILD_TESTS)
    enable_testing()
    add_executable(ClevoCommunitySDKTests tests/SdkTests.cpp tests/RecordingTransport.hpp)
    target_link_libraries(ClevoCommunitySDKTests PRIVATE ClevoCommunitySDK::ClevoCommunitySDK)
    clevo_sdk_deploy_runtime(ClevoCommunitySDKTests)
    add_test(NAME ClevoCommunitySDKTests COMMAND ClevoCommunitySDKTests)
endif()

if(CLEVO_SDK_BUILD_EXAMPLES)
    add_executable(clevoctl examples/clevoctl.cpp)
    target_link_libraries(clevoctl PRIVATE ClevoCommunitySDK::ClevoCommunitySDK)
    clevo_sdk_deploy_runtime(clevoctl)
endif()

if(CLEVO_SDK_INSTALL)
    set(CLEVO_SDK_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/ClevoCommunitySDK")
    set(CLEVO_SDK_INSTALL_RUNTIME_DLL "${CMAKE_INSTALL_BINDIR}/InsydeDCHU.dll")

    install(TARGETS ClevoCommunitySDK
        EXPORT ClevoCommunitySDKTargets
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" # the DLL of a shared build
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" # static library or import library
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
    install(DIRECTORY include/clevo DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(FILES "${CLEVO_SDK_EXPORT_HEADER}" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/clevo")
    install(FILES driver/InsydeDCHU.dll DESTINATION "${CMAKE_INSTALL_BINDIR}")

    if(CLEVO_SDK_BUILD_EXAMPLES)
        install(TARGETS clevoctl RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()

    install(EXPORT ClevoCommunitySDKTargets
        NAMESPACE ClevoCommunitySDK::
        DESTINATION "${CLEVO_SDK_CMAKE_DIR}"
    )

    configure_package_config_file(cmake/ClevoCommunitySDKConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/ClevoCommunitySDKConfig.cmake"
        INSTALL_DESTINATION "${CLEVO_SDK_CMAKE_DIR}"
        PATH_VARS CLEVO_SDK_INSTALL_RUNTIME_DLL
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/ClevoCommunitySDKConfigVersion.cmake"
        COMPATIBILITY SameMinorVersion
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/ClevoCommunitySDKConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ClevoCommunitySDKConfigVersion.cmake"
        cmake/ClevoCommunitySDKRuntime.cmake
        DESTINATION "${CLEVO_SDK_CMAKE_DIR}"
    )
endif()
