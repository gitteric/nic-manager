# Post-build step: signs EXE with PFX. Run by CMake with
#   -DEXE=<file> -DPFX=<file> -DOSSLSIGNCODE=<tool or empty> -DSIGNTOOL=<tool or empty>
# The .pfx password comes from the environment variable NICMGR_SIGN_PASSWORD.
set(pw "$ENV{NICMGR_SIGN_PASSWORD}")
if(pw STREQUAL "")
    message(WARNING "NICMGR_SIGN_PASSWORD is not set; ${EXE} stays unsigned.")
    return()
endif()

if(OSSLSIGNCODE)
    execute_process(
        COMMAND "${OSSLSIGNCODE}" sign -pkcs12 "${PFX}" -pass "${pw}" -n "NIC Manager" -h sha256
                -in "${EXE}" -out "${EXE}.signed"
        RESULT_VARIABLE rc OUTPUT_QUIET)
    if(rc EQUAL 0)
        file(RENAME "${EXE}.signed" "${EXE}")
    endif()
elseif(SIGNTOOL)
    execute_process(
        COMMAND "${SIGNTOOL}" sign /q /f "${PFX}" /p "${pw}" /fd SHA256 /d "NIC Manager" "${EXE}"
        RESULT_VARIABLE rc)
else()
    message(WARNING "Neither osslsigncode nor signtool found; ${EXE} stays unsigned. "
                    "MSYS2: pacman -S mingw-w64-ucrt-x86_64-osslsigncode")
    return()
endif()
if(NOT rc EQUAL 0)
    file(REMOVE "${EXE}.signed")
    message(FATAL_ERROR "Signing ${EXE} failed (wrong password?).")
endif()
message(STATUS "Signed ${EXE}")
