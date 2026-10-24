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

# headless gameplay checks (sim/ only, no graphics): ./build/dq-test-gameplay
add_executable(dq-test-gameplay tools/test_gameplay.cpp)
target_link_libraries(dq-test-gameplay PRIVATE dqcore)

if(WIN32)
  set_target_properties(dokuquest dokuquest-launcher PROPERTIES WIN32_EXECUTABLE ON)
  target_link_libraries(dqclient PUBLIC SDL2::SDL2main)
endif()

# Runtime data next to the executables (data/, assets/)
foreach(t dokuquest dokuquest-launcher dokuquest-server dq-test-gameplay)
  add_custom_command(TARGET ${t} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:${t}>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${t}>/assets)
endforeach()
