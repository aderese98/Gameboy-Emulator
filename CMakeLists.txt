cmake_minimum_required(VERSION 3.16)
project(gbemu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  if (MSVC)
    add_compile_options(/W4 /permissive-)
  else()
    add_compile_options(-Wall -Wextra -Wpedantic)
  endif()
endif()

add_executable(gbemu
  src/main.cpp
  src/bus.cpp
  src/cpu.cpp
  src/ppu.cpp
  src/joypad.cpp
  src/cartridge.cpp
)

# Find and link SDL2
find_package(SDL2 REQUIRED)
target_link_libraries(gbemu PRIVATE SDL2::SDL2)
