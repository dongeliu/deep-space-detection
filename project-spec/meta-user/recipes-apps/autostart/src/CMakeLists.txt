add_library(coding SHARED coding.cpp arithmetic_coding.cpp)
install(TARGETS coding LIBRARY)
add_library(crc SHARED crc.c)
install(TARGETS crc LIBRARY)
add_library(transmission_protocol SHARED transmission_protocol.c)
target_link_libraries(transmission_protocol crc)
install(TARGETS transmission_protocol LIBRARY)

add_executable(main main.c)
target_link_libraries(main PRIVATE transmission_protocol)
find_package(Threads REQUIRED)
target_link_libraries(main PRIVATE coding Threads::Threads)
install(TARGETS main RUNTIME)
add_executable(master master.c)
target_link_libraries(master PRIVATE transmission_protocol)
