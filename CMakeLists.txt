cmake_minimum_required(VERSION 3.16)
project(bq25792_utils C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(BQ25792_BUILD_CLI "Build bqctl CLI" ON)
option(BQ25792_BUILD_DAEMON "Build bq25792d daemon" ON)

add_library(bq25792 SHARED
    src/bq25792.c
)

target_include_directories(bq25792 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_path(I2CDEV_INCLUDE_DIR linux/i2c-dev.h)
find_library(I2C_LIB i2c)

if (NOT I2CDEV_INCLUDE_DIR)
  message(FATAL_ERROR "linux/i2c-dev.h not found. Install linux headers (usually already present).")
endif()

# libi2c provides i2c_smbus_* helpers on Debian (package: libi2c-dev)
target_include_directories(bq25792 PRIVATE ${I2CDEV_INCLUDE_DIR})
target_link_libraries(bq25792 PRIVATE ${I2C_LIB})

set_target_properties(bq25792 PROPERTIES OUTPUT_NAME "bq25792")

if (BQ25792_BUILD_CLI)
  add_executable(bqctl src/bqctl.c)
  target_link_libraries(bqctl PRIVATE bq25792)
endif()

if (BQ25792_BUILD_DAEMON)
  add_executable(bq25792d src/bq25792d.c)
  target_link_libraries(bq25792d PRIVATE bq25792)
endif()

include(GNUInstallDirs)
install(TARGETS bq25792
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if (BQ25792_BUILD_CLI)
  install(TARGETS bqctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if (BQ25792_BUILD_DAEMON)
  install(TARGETS bq25792d RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# systemd unit install (Debian/RPi OS usually uses /lib/systemd/system)
set(SYSTEMD_UNIT_DIR "/lib/systemd/system" CACHE PATH "systemd unit install dir")
install(FILES systemd/bq25792d.service DESTINATION ${SYSTEMD_UNIT_DIR})
