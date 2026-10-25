cmake_minimum_required(VERSION 3.20)
project(DokuQuest VERSION 1.0.0 LANGUAGES C CXX)
# DokuQuest C++ — game client, launcher (with dedicated server + Mod Studio) and headless server.
set(DQ_VERSION "1.0.0-alpha.1" CACHE STRING "Full game version shown in game and launcher")
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
else()
  set(DQ_WARN -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare)
endif()

file(GLOB_RECURSE DQ_CORE_SRC CONFIGURE_DEPENDS
  src/common/*.cpp src/core/*.cpp src/world/*.cpp src/gen/*.cpp src/sim/*.cpp src/net/*.cpp src/mod/*.cpp src/server/*.cpp)
add_library(dqcore STATIC ${DQ_CORE_SRC})
target_include_directories(dqcore PUBLIC src)
target_compile_definitions(dqcore PUBLIC DQ_VERSION="${DQ_VERSION}")
target_compile_options(dqcore PRIVATE ${DQ_WARN})
target_link_libraries(dqcore PUBLIC dq_headers miniz enet quickjs Threads::Threads ${CMAKE_DL_LIBS})

file(GLOB_RECURSE DQ_CLIENT_SRC CONFIGURE_DEPENDS
  src/render/*.cpp src/audio/*.cpp src/ui/*.cpp src/input/*.cpp src/assets/*.cpp src/client/*.cpp)
list(FILTER DQ_CLIENT_SRC EXCLUDE REGEX ".*/client/main\\.cpp$")
add_library(dqclient STATIC ${DQ_CLIENT_SRC})
target_compile_options(dqclient PRIVATE ${DQ_WARN})
target_link_libraries(dqclient PUBLIC dqcore glad imgui SDL2::SDL2-static)

add_executable(dokuquest src/client/main.cpp)
target_link_libraries(dokuquest PRIVATE dqclient)

file(GLOB_RECURSE DQ_LAUNCHER_SRC CONFIGURE_DEPENDS src/launcher/*.cpp)
add_executable(dokuquest-launcher ${DQ_LAUNCHER_SRC})
target_compile_options(dokuquest-launcher PRIVATE ${DQ_WARN})
target_link_libraries(dokuquest-launcher PRIVATE dqclient)

add_executable(dokuquest-server tools/server_main.cpp)
target_link_libraries(dokuquest-server PRIVATE dqcore)

# terrain generator checks (tools/port/gen_compare.py, timing); not shipped
add_executable(dq-gentool tools/port/gentool.cpp)
target_link_libraries(dq-gentool PRIVATE dqcore)

if(WIN32)
  set_target_properties(dokuquest dokuquest-launcher PROPERTIES WIN32_EXECUTABLE ON)
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

# Runtime data next to the executables (data/, assets/)
foreach(t dokuquest dokuquest-launcher dokuquest-server dq-gentool)
  add_custom_command(TARGET ${t} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:${t}>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${t}>/assets)
endforeach()

# Install = one folder with the three programs, data/ and assets/ (tools/package.sh packs it):
#   cmake --install build --prefix dist/DokuQuest
install(TARGETS dokuquest dokuquest-launcher dokuquest-server RUNTIME DESTINATION .)
install(DIRECTORY data assets DESTINATION .)
# Multiplayer + mods test (headless): a dedicated server and two scripted clients. Run: build/test_net
add_library(dq_sample_plugin MODULE tools/sample_plugin.c)
target_include_directories(dq_sample_plugin PRIVATE src)
set_target_properties(dq_sample_plugin PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
# Magic, enchanting, brewing, skills and RPG rules (headless). Run: build/test_magic
add_executable(test_magic tools/test_magic.cpp)
target_link_libraries(test_magic PRIVATE dqcore)
add_custom_command(TARGET test_magic POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:test_magic>/data)

add_executable(test_net tools/test_net.cpp)
target_link_libraries(test_net PRIVATE dqcore)
target_compile_definitions(test_net PRIVATE DQ_SAMPLE_PLUGIN="$<TARGET_FILE:dq_sample_plugin>")
add_dependencies(test_net dq_sample_plugin)
add_custom_command(TARGET test_net POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:test_net>/data)
# dq-sfx: offline sound renderer for checking the audio (tools/sfx_render.cpp)
add_executable(dq-sfx tools/sfx_render.cpp)
target_link_libraries(dq-sfx PRIVATE dqclient)
# Content features test (headless): maps, fishing, camping, travel, elytra, the stable, places, farming. Run: build/test_content
add_executable(test_content tools/test_content.cpp)
target_link_libraries(test_content PRIVATE dqcore)
add_custom_command(TARGET test_content POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:test_content>/data)
