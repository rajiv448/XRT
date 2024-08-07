link_directories($ENV{XILINX_XRT}/lib)

set(SRCS
  logger.cpp
)

add_library(xrt_capture SHARED ${SRCS})

install(TARGETS xrt_capture)
