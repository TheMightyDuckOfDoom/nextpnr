message(STATUS "Configuring Himbaechel-PCBFPGA uarch")
cmake_minimum_required(VERSION 3.5)
project(himbaechel-pcbfpga-chipdb NONE)

set(ALL_HIMBAECHEL_PCBFPGA_DEVICES test)
set(HIMBAECHEL_PCBFPGA_DEVICES test CACHE STRING
    "Include support for these PCBFPGA devices (available: ${ALL_HIMBAECHEL_PCBFPGA_DEVICES})")
message(STATUS "Enabled Himbaechel-PCBFPGA devices: ${HIMBAECHEL_PCBFPGA_DEVICES}")

set(chipdb_binaries)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/pcbfpga)
foreach(device ${HIMBAECHEL_PCBFPGA_DEVICES})
	set(device_bba ${CMAKE_BINARY_DIR}/share/himbaechel/pcbfpga/chipdb-${device}.bba)
	set(device_bin ${CMAKE_BINARY_DIR}/share/himbaechel/pcbfpga/chipdb-${device}.bin)
	add_custom_command(
		OUTPUT ${device_bin}
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/pcbfpga_arch_gen.py ${device_bba}
		COMMAND bbasm ${BBASM_ENDIAN_FLAG} ${device_bba} ${device_bin}.new
        # atomically update
        COMMAND ${CMAKE_COMMAND} -E rename ${device_bin}.new ${device_bin}
		DEPENDS
			bbasm
			${CMAKE_CURRENT_SOURCE_DIR}/pcbfpga_arch_gen.py
			${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
		VERBATIM)
	list(APPEND chipdb_binaries ${device_bin})
endforeach()

add_custom_target(chipdb-himbaechel-pcbfpga ALL DEPENDS ${chipdb_binaries})
install(DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/pcbfpga/ DESTINATION share/nextpnr/himbaechel/pcbfpga
	    PATTERN "*.bba" EXCLUDE)
