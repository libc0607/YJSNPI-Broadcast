cmake_minimum_required (VERSION 2.8)

project (ezwifibroadcast)

add_definitions(-Wall -D _GNU_SOURCE)

add_executable(rx  rx.c  lib.c  radiotap.c  fec.c xxtea.c)
target_link_libraries(rx rt pcap iniparser)
install (TARGETS rx DESTINATION bin)

add_executable(rx_rc_telemetry_buf  rx_rc_telemetry_buf.c  lib.c  radiotap.c xxtea.c)
target_link_libraries(rx_rc_telemetry_buf rt pcap iniparser)
install (TARGETS rx_rc_telemetry_buf DESTINATION bin)

add_executable(rssirx  rssirx.c  lib.c  radiotap.c xxtea.c)
target_link_libraries(rssirx rt pcap iniparser)
install (TARGETS rssirx DESTINATION bin)

add_executable(rssitx  rssitx.c  lib.c  radiotap.c xxtea.c)
target_link_libraries(rssitx rt pcap iniparser)
install (TARGETS rssitx DESTINATION bin)

add_executable(tx_rawsock  tx_rawsock.c  lib.c  fec.c xxtea.c)
target_link_libraries(tx_rawsock rt pcap iniparser)
install (TARGETS tx_rawsock DESTINATION bin)

add_executable(tx_telemetry  tx_telemetry.c  lib.c  fec.c xxtea.c)
target_link_libraries(tx_telemetry rt pcap iniparser)
install (TARGETS tx_telemetry DESTINATION bin)

add_executable(tx_measure  tx_measure.c  lib.c  fec.c xxtea.c)
target_link_libraries(tx_measure rt pcap iniparser)
install (TARGETS tx_measure DESTINATION bin)

add_executable(rx_status  rx_status.c)
target_link_libraries(rx_status rt pcap)
install (TARGETS rx_status DESTINATION bin)

add_executable(check_alive  check_alive.c )
target_link_libraries(check_alive rt pcap iniparser)
install (TARGETS check_alive DESTINATION bin)

add_executable(check_alive_in  check_alive_in.c )
target_link_libraries(check_alive_in rt pcap iniparser)
install (TARGETS check_alive_in DESTINATION bin)

add_executable(rssi_forward  rssi_forward.c )
target_link_libraries(rssi_forward rt pcap iniparser)
install (TARGETS rssi_forward DESTINATION bin)

add_executable(rssi_forward_in  rssi_forward_in.c )
target_link_libraries(rssi_forward_in rt pcap iniparser)
install (TARGETS rssi_forward_in DESTINATION bin)

add_executable(sharedmem_init_rx  sharedmem_init_rx.c  lib.c)
target_link_libraries(sharedmem_init_rx rt pcap)
install (TARGETS sharedmem_init_rx DESTINATION bin)

add_executable(sharedmem_init_tx  sharedmem_init_tx.c lib.c)
target_link_libraries(sharedmem_init_tx rt pcap)
install (TARGETS sharedmem_init_tx DESTINATION bin)

add_executable(rx_rc_sbus rx_rc_sbus.c lib.c radiotap.c xxtea.c)
target_link_libraries(rx_rc_sbus rt pcap iniparser)
install (TARGETS rx_rc_sbus DESTINATION bin)

add_executable(tx_rc_sbus tx_rc_sbus.c lib.c xxtea.c)
target_link_libraries(tx_rc_sbus rt pcap iniparser)
install (TARGETS tx_rc_sbus DESTINATION bin)

add_executable(tx_test tx_test.c)
target_link_libraries(tx_test rt pcap)
install (TARGETS tx_test DESTINATION bin)

add_executable(tx_stdio tx_stdio.c)
target_link_libraries(tx_stdio rt pcap iniparser)
install (TARGETS tx_stdio DESTINATION bin)

add_executable(rx_stdio rx_stdio.c lib.c radiotap.c)
target_link_libraries(rx_stdio rt pcap iniparser)
install (TARGETS rx_stdio DESTINATION bin)

add_executable(sysair_forward  sysair_forward.c )
target_link_libraries(sysair_forward rt pcap iniparser)
install (TARGETS sysair_forward DESTINATION bin)

add_executable(sysair_forward_in  sysair_forward_in.c )
target_link_libraries(sysair_forward_in rt pcap iniparser)
install (TARGETS sysair_forward_in DESTINATION bin)
