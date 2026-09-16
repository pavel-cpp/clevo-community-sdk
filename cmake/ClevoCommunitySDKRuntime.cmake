include_guard(GLOBAL)

# clevo_sdk_deploy_runtime(<target> [INSTALL_DESTINATION <dir>])
#
# Places what an executable using the SDK needs at runtime next to it after
# every build: always InsydeDCHU.dll, which the SDK loads dynamically, plus
# the SDK's own DLL when it was built shared. With INSTALL_DESTINATION the
# same files are installed there as well.
function(clevo_sdk_deploy_runtime target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "INSTALL_DESTINATION" "")

    get_property(driver_dll GLOBAL PROPERTY CLEVO_SDK_RUNTIME_DLL)
    if(NOT driver_dll OR NOT EXISTS "${driver_dll}")
        message(FATAL_ERROR "clevo_sdk_deploy_runtime: InsydeDCHU.dll not found at '${driver_dll}'")
    endif()

    set(runtime_files "${driver_dll}")
    get_target_property(sdk_type ClevoCommunitySDK::ClevoCommunitySDK TYPE)
    if(sdk_type STREQUAL "SHARED_LIBRARY")
        list(APPEND runtime_files "$<TARGET_FILE:ClevoCommunitySDK::ClevoCommunitySDK>")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${runtime_files} "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying the ClevoCommunitySDK runtime next to ${target}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    if(arg_INSTALL_DESTINATION)
        install(FILES ${runtime_files} DESTINATION "${arg_INSTALL_DESTINATION}")
    endif()
endfunction()
