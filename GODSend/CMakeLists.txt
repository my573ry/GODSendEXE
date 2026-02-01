cmake_minimum_required(VERSION 3.15)
project(godsend_installer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add executable
add_executable(godsend_installer WIN32 godsend_installer.cpp)

# Link required libraries
target_link_libraries(godsend_installer 
    winhttp
    shell32
)

# Embed the manifest
if(MSVC)
    set_target_properties(godsend_installer PROPERTIES
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\" /SUBSYSTEM:CONSOLE"
    )
endif()

# Enable Unicode
target_compile_definitions(godsend_installer PRIVATE UNICODE _UNICODE)
