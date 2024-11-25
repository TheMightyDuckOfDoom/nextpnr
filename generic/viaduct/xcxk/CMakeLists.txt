message(STATUS "Configuring Generic-XCxk uarch")
cmake_minimum_required(VERSION 3.5)
project(generic-xcxk-projectxcxk NONE)

file(GLOB device_list LIST_DIRECTORIES true projectXCxk/results/3*)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/share/generic/xcxk)

add_custom_target(projectxcxk-generic-xcxk ALL DEPENDS ${device_list})
install(DIRECTORY ${CMAKE_BINARY_DIR}/share/generic/xcxk/ DESTINATION share/nextpnr/generic/xcxk
	    PATTERN "*" EXCLUDE)

foreach(dir ${device_list})
  get_filename_component(dir_name ${dir} NAME)
  message(STATUS "projectXCxk: Found files for ${dir_name}")
  add_custom_command(
  TARGET projectxcxk-generic-xcxk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${dir} ${CMAKE_BINARY_DIR}/share/generic/xcxk/${dir_name}
  )
endforeach()
