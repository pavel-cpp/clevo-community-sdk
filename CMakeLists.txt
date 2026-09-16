cmake_minimum_required(VERSION 3.21)

project(ClevoCommunitySDK
    VERSION 0.1.0
    DESCRIPTION "C++ SDK for Clevo laptops driven through the Insyde DCHU driver"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "ClevoCommunitySDK talks to the Insyde DCHU driver and only builds for Windows.")
endif()

option(CLEVO_SDK_BUILD_TESTS "Build the ClevoCommunitySDK unit tests" ${PROJECT_IS_TOP_LEVEL})
option(CLEVO_SDK_BUILD_EXAMPLES "Build the clevoctl example" ${PROJECT_IS_TOP_LEVEL})

add_library(ClevoCommunitySDK STATIC
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

target_include_directories(ClevoCommunitySDK
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(ClevoCommunitySDK PUBLIC cxx_std_23)
target_compile_definitions(ClevoCommunitySDK PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(ClevoCommunitySDK PRIVATE user32)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ClevoCommunitySDK PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(ClevoCommunitySDK PRIVATE /W4 /permissive-)
endif()

# The vendor driver interface is loaded at runtime, so it has to sit next to
# every executable that uses the SDK.
function(clevo_sdk_deploy_runtime target)
    set(runtime_dll "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/runtime/InsydeDCHU.dll")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${runtime_dll}" "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying InsydeDCHU.dll next to ${target}"
        VERBATIM
    )
    install(FILES "${runtime_dll}" DESTINATION .)
endfunction()

if(CLEVO_SDK_BUILD_TESTS)
    enable_testing()
    add_executable(ClevoCommunitySDKTests tests/SdkTests.cpp tests/RecordingTransport.hpp)
    target_link_libraries(ClevoCommunitySDKTests PRIVATE ClevoCommunitySDK::ClevoCommunitySDK)
    add_test(NAME ClevoCommunitySDKTests COMMAND ClevoCommunitySDKTests)
endif()

if(CLEVO_SDK_BUILD_EXAMPLES)
    add_executable(clevoctl examples/clevoctl.cpp)
    target_link_libraries(clevoctl PRIVATE ClevoCommunitySDK::ClevoCommunitySDK)
    clevo_sdk_deploy_runtime(clevoctl)
endif()
