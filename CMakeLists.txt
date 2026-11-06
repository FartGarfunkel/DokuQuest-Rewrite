cmake_minimum_required(VERSION 3.20)
project(DokuQuest VERSION 1.0.0 LANGUAGES C CXX)
# DokuQuest C++ — game client, launcher (with dedicated server + Mod Studio) and headless server.
set(DQ_VERSION "1.0.0-alpha.17" CACHE STRING "Full game version shown in game and launcher")
# the launcher's own version: raised only when the launcher itself changes. Updates install just the game (the "-game"
# packages); the whole package, launcher and all, only when this is newer than the running launcher's (launcher/updater.cpp)
set(DQ_LAUNCHER_VERSION "1.0.0-alpha.17")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
option(DQ_WERROR "Treat warnings as errors" OFF)
option(DQ_PORTABLE "Release packages: link libstdc++/libgcc statically on Linux (runs on older and newer distros)" OFF)

find_package(Threads REQUIRED)
add_subdirectory(third_party)

if(MSVC)
  add_compile_options(/utf-8 /MP)
  # windows.h's min/max macros break std::min/std::max; M_PI needs _USE_MATH_DEFINES
  add_compile_definitions(NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
else()
  set(DQ_WARN -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare)
endif()

file(GLOB_RECURSE DQ_CORE_SRC CONFIGURE_DEPENDS
  src/common/*.cpp src/core/*.cpp src/world/*.cpp src/gen/*.cpp src/sim/*.cpp src/net/*.cpp src/mod/*.cpp src/server/*.cpp)
add_library(dqcore STATIC ${DQ_CORE_SRC})
target_include_directories(dqcore PUBLIC src)
target_compile_definitions(dqcore PUBLIC DQ_VERSION="${DQ_VERSION}" DQ_LAUNCHER_VERSION="${DQ_LAUNCHER_VERSION}")
target_compile_options(dqcore PRIVATE ${DQ_WARN})
target_link_libraries(dqcore PUBLIC dq_headers miniz enet quickjs Threads::Threads ${CMAKE_DL_LIBS})

file(GLOB_RECURSE DQ_CLIENT_SRC CONFIGURE_DEPENDS
  src/render/*.cpp src/audio/*.cpp src/ui/*.cpp src/input/*.cpp src/assets/*.cpp src/client/*.cpp)
list(FILTER DQ_CLIENT_SRC EXCLUDE REGEX ".*/client/main\\.cpp$")
add_library(dqclient STATIC ${DQ_CLIENT_SRC})
target_compile_options(dqclient PRIVATE ${DQ_WARN})
# glslang is compiled from the sources tools/fetch_deps.sh clones. The optimizer needs SPIRV-Tools,
# which the game does not use (vk_backend asks glslang to skip it).
set(DQ_GLSLANG "${CMAKE_SOURCE_DIR}/third_party/vulkan/glslang")
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${DQ_GLSLANG}" "${CMAKE_BINARY_DIR}/glslang" EXCLUDE_FROM_ALL)
target_include_directories(dqclient PRIVATE
  "${CMAKE_SOURCE_DIR}/third_party/vulkan/Vulkan-Headers/include"
  "${CMAKE_SOURCE_DIR}/third_party/vulkan/volk"
  "${DQ_GLSLANG}")
# The Vulkan loader is opened at runtime (volk). A build machine does not need libvulkan installed,
# and a player without it keeps the OpenGL renderers.
target_link_libraries(dqclient PUBLIC dqcore glad imgui SDL2::SDL2-static glslang glslang-default-resource-limits)

add_executable(dokuquest src/client/main.cpp)
target_link_libraries(dokuquest PRIVATE dqclient)

file(GLOB_RECURSE DQ_LAUNCHER_SRC CONFIGURE_DEPENDS src/launcher/*.cpp)
add_executable(dokuquest-launcher ${DQ_LAUNCHER_SRC})
target_compile_options(dokuquest-launcher PRIVATE ${DQ_WARN})
target_link_libraries(dokuquest-launcher PRIVATE dqclient)

add_executable(dokuquest-server tools/server_main.cpp)
target_link_libraries(dokuquest-server PRIVATE dqcore)
if(UNIX AND NOT APPLE)   # names in crash backtraces (common.cpp installCrashHandler)
  foreach(t dokuquest dokuquest-launcher dokuquest-server)
    target_link_options(${t} PRIVATE -rdynamic)
  endforeach()
endif()

# terrain generator checks (tools/port/gen_compare.py, timing); not shipped
add_executable(dq-gentool tools/port/gentool.cpp)
target_link_libraries(dq-gentool PRIVATE dqcore)
# headless creature checks (AI, spawning, path finding): ctest or ./build/dq-creature-test
enable_testing()
add_executable(dq-creature-test tools/test/creature_test.cpp)
target_link_libraries(dq-creature-test PRIVATE dqcore)
add_test(NAME creatures COMMAND dq-creature-test)

# every item used and every block placed/used/broken through the player controller; prints anything odd (tools/sweep.cpp)
add_executable(dq-sweep tools/sweep.cpp)
target_link_libraries(dq-sweep PRIVATE dqcore)

# headless gameplay checks (sim/ only, no graphics): ./build/dq-test-gameplay
add_executable(dq-test-gameplay tools/test_gameplay.cpp)
target_link_libraries(dq-test-gameplay PRIVATE dqcore)

if(WIN32)
  set_target_properties(dokuquest dokuquest-launcher PROPERTIES WIN32_EXECUTABLE ON)
  target_link_libraries(dqcore PUBLIC iphlpapi shell32)   # net/lan.cpp: the network adapters (home address, is a VPN on)
  target_link_libraries(dqclient PUBLIC SDL2::SDL2main)
  # app icon (assets/icon.ico) for the game and the launcher
  target_sources(dokuquest PRIVATE src/launcher/dokuquest.rc)
  target_sources(dokuquest-launcher PRIVATE src/launcher/dokuquest.rc)
endif()
if(DQ_PORTABLE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  foreach(t dokuquest dokuquest-launcher dokuquest-server)
    target_link_options(${t} PRIVATE -static-libstdc++ -static-libgcc)
  endforeach()
endif()

# Runtime data next to the executables (data/, assets/), copied ONCE by one target every program waits for: a copy
# per program ran in parallel into the same folder and broke the Windows build (MSB3073 on copy_directory)
get_property(DQ_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(DQ_MULTI_CONFIG)
  set(DQ_RUNTIME_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>")
else()
  set(DQ_RUNTIME_DIR "${CMAKE_BINARY_DIR}")
endif()
add_custom_target(dq_runtime_data ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data ${DQ_RUNTIME_DIR}/data
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${DQ_RUNTIME_DIR}/assets
  COMMENT "Copying data/ and assets/ next to the programs")

# Install = one folder with the three programs, data/ and assets/ (tools/package.sh packs it):
#   cmake --install build --prefix dist/DokuQuest
install(TARGETS dokuquest dokuquest-launcher dokuquest-server RUNTIME DESTINATION .)
install(DIRECTORY data assets DESTINATION .)
file(WRITE ${CMAKE_BINARY_DIR}/launcher.version "${DQ_LAUNCHER_VERSION}\n")
install(FILES ${CMAKE_BINARY_DIR}/launcher.version DESTINATION .)
# Multiplayer + mods test (headless): a dedicated server and two scripted clients. Run: build/test_net
add_library(dq_sample_plugin MODULE tools/sample_plugin.c)
target_include_directories(dq_sample_plugin PRIVATE src)
set_target_properties(dq_sample_plugin PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
# Magic, enchanting, brewing, skills and RPG rules (headless). Run: build/test_magic
add_executable(test_magic tools/test_magic.cpp)
target_link_libraries(test_magic PRIVATE dqcore)

add_executable(test_net tools/test_net.cpp)
target_link_libraries(test_net PRIVATE dqcore)
target_compile_definitions(test_net PRIVATE DQ_SAMPLE_PLUGIN="$<TARGET_FILE:dq_sample_plugin>")
add_dependencies(test_net dq_sample_plugin)
# dq-sfx: offline sound renderer for checking the audio (tools/sfx_render.cpp)
add_executable(dq-sfx tools/sfx_render.cpp)
target_link_libraries(dq-sfx PRIVATE dqclient)
# The Music Studio's song core (headless): build/test_song <recording.wav>
add_executable(test_song tools/test_song.cpp)
target_link_libraries(test_song PRIVATE dqclient)
# Content features test (headless): maps, fishing, camping, travel, elytra, the stable, places, farming. Run: build/test_content
add_executable(test_content tools/test_content.cpp)
target_link_libraries(test_content PRIVATE dqcore)
# The End, dragons, the Deep Dark's guardian, the watcher and the story (headless). Run: build/test_endstory
add_executable(test_endstory tools/test_endstory.cpp)
target_link_libraries(test_endstory PRIVATE dqcore)
add_test(NAME endstory COMMAND test_endstory)

# Villagers test (headless): villages, schedules, trading, quests, standing, guards, paths, skins. Run: build/test_villagers
add_executable(test_villagers tools/test_villagers.cpp)
target_link_libraries(test_villagers PRIVATE dqcore)
add_test(NAME villagers COMMAND test_villagers)
# Redstone, rails/carts, boats/ships and machines test (headless). Run: build/test_redstone
add_executable(test_redstone tools/test_redstone.cpp)
target_link_libraries(test_redstone PRIVATE dqcore)

# every program finds data/ and assets/ next to it
foreach(t dokuquest dokuquest-launcher dokuquest-server dq-gentool dq-creature-test dq-sweep dq-test-gameplay test_magic test_net dq-sfx test_content test_endstory test_villagers test_redstone)
  if(TARGET ${t})
    add_dependencies(${t} dq_runtime_data)
  endif()
endforeach()
