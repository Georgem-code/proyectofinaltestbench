cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(banco_de_pruebas LANGUAGES C)

target_sources(app PRIVATE
  src/main.c
  src/actuadores.c
  src/comms.c
  src/encoder.c
  src/fsm.c
  src/imu.c
  src/indicadores.c
  src/telemetria.c
  src/crc16.c
)

target_include_directories(app PRIVATE src)
