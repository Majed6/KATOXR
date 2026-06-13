cmake_minimum_required(VERSION 4.2)
project(KATOXR)

set(CMAKE_CXX_STANDARD 17)

add_library(KATOXR SHARED src/layer_main.cpp src/KATNativeSDK.cpp src/Treadmill.cpp)

target_include_directories(KATOXR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/OpenXR-SDK/OpenXR-SDK-main/include
)

if(WIN32)
    target_link_options(KATOXR PRIVATE -static)
    set_target_properties(KATOXR PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        PREFIX ""
    )
    # Copy the layer DLL and KAT SDK DLL to the project root so the manifest can find them
    add_custom_command(TARGET KATOXR POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:KATOXR> ${CMAKE_CURRENT_SOURCE_DIR}/KATOXR.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/src/KATNativeSDK.dll" ${CMAKE_CURRENT_SOURCE_DIR}/KATNativeSDK.dll
        COMMENT "Copying DLLs to project root"
    )
endif()
