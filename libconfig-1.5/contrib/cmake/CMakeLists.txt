#cmake_minimum_required(VERSION 2.8)

PROJECT(libconfig_test_app)

add_subdirectory(src)

#add cmake script files
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake_work")

#check if libconfig is present (FindConfig.cmake and FindConfig++.cmake files)
FIND_PACKAGE(Config REQUIRED)
FIND_PACKAGE(Config++ REQUIRED)

#add undelete support
CONFIGURE_FILE(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_work/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_work/cmake_uninstall.cmake"
      IMMEDIATE @ONLY)
ADD_CUSTOM_TARGET(uninstall
    "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_work/cmake_uninstall.cmake")


