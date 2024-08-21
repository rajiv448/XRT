link_directories($ENV{XILINX_XRT}/lib)

set(SRCS
  capture.cpp
  logger.cpp
  xrt_device_inst.cpp
  xrt_kernel_inst.cpp
  xrt_bo_inst.cpp
  xrt_hw_context_inst.cpp
  xrt_xclbin_inst.cpp
)

set (LIBS xrt_coreutil)

add_library(xrt_capture SHARED ${SRCS})

if (NOT WIN32)
  list(APPEND LIBS uuid)
endif(NOT WIN32)

target_link_libraries(xrt_capture PRIVATE ${LIBS})

install(TARGETS xrt_capture
  EXPORT xrt-targets
  LIBRARY DESTINATION ${XRT_INSTALL_LIB_DIR} ${XRT_NAMELINK_SKIP}
  RUNTIME DESTINATION ${XRT_INSTALL_BIN_DIR}
)

install(TARGETS xrt_capture
  EXPORT xrt-dev-targets
  ARCHIVE DESTINATION ${XRT_INSTALL_LIB_DIR} COMPONENT ${XRT_DEV_COMPONENT}
  LIBRARY DESTINATION ${XRT_INSTALL_LIB_DIR} COMPONENT ${XRT_DEV_COMPONENT} ${XRT_NAMELINK_ONLY}
)
