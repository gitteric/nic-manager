cmake_minimum_required(VERSION 3.20)
# Four-part version (major.minor.patch.build). The release workflow passes
# it from the git tag, e.g. v1.0.0.0 -> -DNICMGR_VERSION=1.0.0.0.
set(NICMGR_VERSION "1.0.0.0" CACHE STRING "Version embedded in the executable")
project(NicManager VERSION ${NICMGR_VERSION} LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Changing adapter settings needs admin rights. Turn this OFF for a
# debug build that you want to start from a non-elevated VS Code (the UI,
# ping list and presets work; applying changes then fails with a message).
option(NICMGR_REQUIRE_ADMIN "Embed a manifest that requests elevation" ON)
if(NICMGR_REQUIRE_ADMIN)
    set(NICMGR_EXEC_LEVEL requireAdministrator)
else()
    set(NICMGR_EXEC_LEVEL asInvoker)
endif()

# Self-signed code signing certificate (tools/make-cert.ps1). The public
# res/nicmanager.cer is embedded so the app can install it; the private
# signing/nicmanager.pfx stays out of git and signs the exe after linking.
set(NICMGR_CERT "${CMAKE_CURRENT_SOURCE_DIR}/res/nicmanager.cer")
if(EXISTS "${NICMGR_CERT}")
    set(NICMGR_CERT_RC "IDR_SIGNING_CERT RCDATA \"${NICMGR_CERT}\"")
else()
    set(NICMGR_CERT_RC "")
endif()
set(NICMGR_SIGN_PFX "${CMAKE_CURRENT_SOURCE_DIR}/signing/nicmanager.pfx" CACHE FILEPATH
    "PKCS#12 file used to sign the exe; password in env NICMGR_SIGN_PASSWORD")

configure_file(res/app.manifest.in ${CMAKE_CURRENT_BINARY_DIR}/app.manifest @ONLY)
configure_file(res/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)

add_executable(NicManager WIN32
    src/main.cpp
    src/mainwindow.cpp
    src/dialogs.cpp
    src/netinfo.cpp
    src/netcmd.cpp
    src/pinger.cpp
    src/settings.cpp
    src/json.cpp
    src/ipaddr.cpp
    src/actions.cpp
    src/cert.cpp
    src/starttask.cpp
    src/scan.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/app.rc
)
target_include_directories(NicManager PRIVATE src res)
target_compile_definitions(NicManager PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000000
    NICMGR_VERSION_WSTR=L"${PROJECT_VERSION}")

# All system libraries that ship with Windows; no third-party dependencies.
target_link_libraries(NicManager PRIVATE
    iphlpapi ws2_32 setupapi cfgmgr32 comctl32 comdlg32 shell32 advapi32 crypt32 uxtheme dwmapi ole32 uuid)

set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/app.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/res/app.ico;${CMAKE_CURRENT_SOURCE_DIR}/res/oui.bin")
if(EXISTS "${NICMGR_CERT}")
    target_compile_definitions(NicManager PRIVATE NICMGR_HAS_CERT=1)
    set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/app.rc APPEND PROPERTY OBJECT_DEPENDS "${NICMGR_CERT}")
endif()

if(EXISTS "${NICMGR_SIGN_PFX}")
    find_program(NICMGR_OSSLSIGNCODE osslsigncode)
    find_program(NICMGR_SIGNTOOL signtool)
    add_custom_command(TARGET NicManager POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:NicManager> -DPFX=${NICMGR_SIGN_PFX}
                "-DOSSLSIGNCODE=${NICMGR_OSSLSIGNCODE}" "-DSIGNTOOL=${NICMGR_SIGNTOOL}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sign.cmake
        VERBATIM)
endif()

if(MSVC)
    target_compile_options(NicManager PRIVATE /W4 /permissive- /utf-8 /MP)
    target_link_options(NicManager PRIVATE /MANIFEST:NO)  # manifest comes from app.rc
    # Static CRT: the exe runs without the VC++ redistributable.
    set_property(TARGET NicManager PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()  # MinGW-w64
    target_compile_options(NicManager PRIVATE -Wall -Wextra -Wno-cast-function-type -Wno-missing-field-initializers)
    target_link_options(NicManager PRIVATE -municode -static)
endif()
