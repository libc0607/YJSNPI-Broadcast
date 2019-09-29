cmake_minimum_required (VERSION 2.8)

project (ezwifibroadcast)

add_definitions(-Wall -D _GNU_SOURCE)

add_executable(ftee ftee.c)
target_link_libraries(ftee rt pcap)
install (TARGETS ftee DESTINATION bin)

add_executable(uftees uftees.c)
target_link_libraries(uftees rt pcap)
install (TARGETS uftees DESTINATION bin)

add_executable(utees utees.c)
target_link_libraries(utees rt pcap)
install (TARGETS utees DESTINATION bin)

add_executable(suftees suftees.c)
target_link_libraries(suftees rt pcap)
install (TARGETS suftees DESTINATION bin)

add_executable(rssilogger  rssilogger.c)
target_link_libraries(rssilogger rt pcap)
install (TARGETS rssilogger DESTINATION bin)

add_executable(syslogger  syslogger.c)
target_link_libraries(syslogger rt pcap)
install (TARGETS syslogger DESTINATION bin)

add_executable(rssi_forward_stdout  rssi_forward_stdout.c )
target_link_libraries(rssi_forward_stdout rt pcap iniparser)
install (TARGETS rssi_forward_stdout DESTINATION bin)

add_executable(rssi_write_shmem  rssi_write_shmem.c )
target_link_libraries(rssi_write_shmem rt pcap iniparser)
install (TARGETS rssi_write_shmem DESTINATION bin)

add_executable(rx_telemetry_buf  rx_telemetry_buf.c  lib.c  radiotap.c)
target_link_libraries(rx_telemetry_buf rt pcap iniparser)
install (TARGETS rx_telemetry_buf DESTINATION bin)

add_executable(rx_rc_telemetry  rx_rc_telemetry.c  lib.c  radiotap.c)
target_link_libraries(rx_rc_telemetry rt pcap iniparser)
install (TARGETS rx_rc_telemetry DESTINATION bin)