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
target_include_directories(imgui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/glad/include)
# Use the game's GL entry points (glad) instead of ImGui's own loader. MSVC has no -include, so the
# Windows build of alpha.17 never saw a GL header and stopped with GLuint unknown.
set_property(SOURCE ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp APPEND PROPERTY COMPILE_DEFINITIONS IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
if(MSVC)
  set_property(SOURCE ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp APPEND PROPERTY COMPILE_OPTIONS "/FIglad/gl.h")
else()
  set_property(SOURCE ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp APPEND PROPERTY COMPILE_OPTIONS "-include" "glad/gl.h")
endif()
target_link_libraries(imgui PUBLIC SDL2::SDL2-static)

add_library(enet STATIC enet/callbacks.c enet/compress.c enet/host.c enet/list.c enet/packet.c enet/peer.c enet/protocol.c enet/unix.c enet/win32.c)
target_include_directories(enet PUBLIC enet/include)
if(WIN32)
  target_link_libraries(enet PUBLIC ws2_32 winmm)
else()
  target_compile_definitions(enet PRIVATE HAS_SOCKLEN_T=1 HAS_POLL=1 HAS_FCNTL=1 HAS_MSGHDR_FLAGS=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1 HAS_INET_PTON=1 HAS_INET_NTOP=1)
endif()

# miniupnpc 2.3.5 (BSD-3): asks the home router to forward the game's port (UPnP IGD, and IGDv2 IPv6 pinholes)
add_library(miniupnpc STATIC miniupnpc/src/addr_is_reserved.c miniupnpc/src/connecthostport.c miniupnpc/src/igd_desc_parse.c
  miniupnpc/src/minisoap.c miniupnpc/src/minissdpc.c miniupnpc/src/miniupnpc.c miniupnpc/src/miniwget.c miniupnpc/src/minixml.c
  miniupnpc/src/portlistingparse.c miniupnpc/src/receivedata.c miniupnpc/src/upnpcommands.c miniupnpc/src/upnpdev.c
  miniupnpc/src/upnperrors.c miniupnpc/src/upnpreplyparse.c)
# (its version header, which its own build makes: a generic client name, nothing about the player or their computer)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/miniupnpc_gen/miniupnpcstrings.h "#ifndef MINIUPNPCSTRINGS_H_INCLUDED\n#define MINIUPNPCSTRINGS_H_INCLUDED\n#define OS_STRING \"Game/1.0\"\n#define MINIUPNPC_VERSION_STRING \"2.3.5\"\n#define UPNP_VERSION_MAJOR 1\n#define UPNP_VERSION_MINOR 1\n#define UPNP_VERSION_STRING \"UPnP/1.1\"\n#endif\n")
target_include_directories(miniupnpc PUBLIC miniupnpc/include PRIVATE miniupnpc/src ${CMAKE_CURRENT_BINARY_DIR}/miniupnpc_gen)
target_compile_definitions(miniupnpc PUBLIC MINIUPNP_STATICLIB)
if(WIN32)
  target_compile_definitions(miniupnpc PRIVATE _WIN32_WINNT=0x0601 MINIUPNPC_SET_SOCKET_TIMEOUT _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)   # (a router that never answers: 3 s, not 21)
  target_link_libraries(miniupnpc PUBLIC ws2_32 iphlpapi)
else()
  target_compile_definitions(miniupnpc PRIVATE MINIUPNPC_SET_SOCKET_TIMEOUT MINIUPNPC_GET_SRC_ADDR _BSD_SOURCE _DEFAULT_SOURCE)
  if(NOT APPLE)
    target_compile_definitions(miniupnpc PRIVATE _XOPEN_SOURCE=600)
  endif()
  if(NOT MSVC)
    target_compile_options(miniupnpc PRIVATE -w)
  endif()
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
else()
  # quickjs-ng uses C11 atomics: MSVC needs C11 mode and its (still "experimental") <stdatomic.h>
  set_target_properties(quickjs PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_compile_options(quickjs PRIVATE /experimental:c11atomics /J /W0)
  target_compile_definitions(quickjs PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
endif()
find_package(Threads REQUIRED)
target_link_libraries(quickjs PUBLIC Threads::Threads)
if(NOT WIN32)
  target_link_libraries(quickjs PUBLIC m)
endif()

add_library(dq_headers INTERFACE)
target_include_directories(dq_headers SYSTEM INTERFACE glm json stb miniaudio)
target_compile_definitions(dq_headers INTERFACE GLM_ENABLE_EXPERIMENTAL)
