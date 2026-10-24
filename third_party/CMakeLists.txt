# Vendored dependencies (all static). Pinned versions are noted in third_party/VERSIONS.md.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_STATIC_PIC ON CACHE BOOL "" FORCE)
add_subdirectory(SDL2 EXCLUDE_FROM_ALL)

add_library(glad STATIC glad/src/gl.c)
target_include_directories(glad PUBLIC glad/include)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_demo.cpp ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp)
target_link_libraries(imgui PUBLIC SDL2::SDL2-static)

add_library(enet STATIC enet/callbacks.c enet/compress.c enet/host.c enet/list.c enet/packet.c enet/peer.c enet/protocol.c enet/unix.c enet/win32.c)
target_include_directories(enet PUBLIC enet/include)
if(WIN32)
  target_link_libraries(enet PUBLIC ws2_32 winmm)
else()
  target_compile_definitions(enet PRIVATE HAS_SOCKLEN_T=1 HAS_POLL=1 HAS_FCNTL=1 HAS_MSGHDR_FLAGS=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1 HAS_INET_PTON=1 HAS_INET_NTOP=1)
endif()

add_library(miniz STATIC miniz/miniz.c miniz/miniz_tdef.c miniz/miniz_tinfl.c miniz/miniz_zip.c)
target_include_directories(miniz PUBLIC miniz ${CMAKE_CURRENT_BINARY_DIR}/miniz_export)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/miniz_export/miniz_export.h "#pragma once\n#define MINIZ_EXPORT\n")
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES=1)

add_library(quickjs STATIC quickjs/quickjs.c quickjs/libregexp.c quickjs/libunicode.c quickjs/cutils.c quickjs/libbf.c)
target_include_directories(quickjs PUBLIC quickjs)
if(NOT MSVC)
  target_compile_options(quickjs PRIVATE -w -funsigned-char)
  target_compile_definitions(quickjs PRIVATE _GNU_SOURCE)
endif()
find_package(Threads REQUIRED)
target_link_libraries(quickjs PUBLIC Threads::Threads)
if(NOT WIN32)
  target_link_libraries(quickjs PUBLIC m)
endif()

add_library(dq_headers INTERFACE)
target_include_directories(dq_headers SYSTEM INTERFACE glm json stb miniaudio)
target_compile_definitions(dq_headers INTERFACE GLM_ENABLE_EXPERIMENTAL)
