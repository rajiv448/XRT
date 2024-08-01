link_directories($ENV{XILINX_XRT}/lib)

set(SRCS
  logger.cpp
)

if (WIN32)
  add_definitions( -DXCL_DRIVER_DLL_EXPORT )
  add_definitions( -DXRT_API_SOURCE )
endif()

add_library(xrt_capture SHARED ${SRCS})

install(TARGETS xrt_capture)
