# Optional: cross-compile from Linux with MinGW-w64, e.g.
#   cmake -B build-mingw64 -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw.cmake -DMINGW_ARCH=x86_64
#   cmake -B build-mingw32 -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw.cmake -DMINGW_ARCH=i686
set(CMAKE_SYSTEM_NAME Windows)
if(NOT MINGW_ARCH)
    set(MINGW_ARCH x86_64)
endif()
set(CMAKE_C_COMPILER ${MINGW_ARCH}-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_ARCH}-w64-mingw32-g++)
set(CMAKE_RC_COMPILER ${MINGW_ARCH}-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_ARCH}-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
