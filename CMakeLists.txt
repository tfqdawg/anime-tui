cmake_minimum_required(VERSION 3.13)
project(animetui C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

add_executable(animetui src/animetui.c)

target_compile_options(animetui PRIVATE -Wall -Wextra)

target_link_libraries(animetui PRIVATE
  PkgConfig::NCURSESW
  PkgConfig::SQLITE3
)
