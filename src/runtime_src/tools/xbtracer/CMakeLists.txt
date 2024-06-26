# SPDX-License-Identifier: Apache-2.0

# -----------------------------------------------------------------------------
# Include the generated header files (e.g., version.h)
include_directories(${XRT_BINARY_DIR}/gen)

set(XBTRACER_NAME "xbtracer")

set(XRT_LOADER_SCRIPTS ${XBTRACER_NAME})
if(WIN32)
  # Add the command shell wrapper in addition to the bash shell wrapper.
  # Note: The bash shell wrapper is smart and will call this wrapper
  list(APPEND XRT_LOADER_SCRIPTS ${XBTRACER_NAME}.bat)
endif()

set(SRCS src/app/launcher.cpp)
if (WIN32)
  list(APPEND SRCS src/app/getopt.c)
endif()

add_executable(${XBTRACER_NAME} ${SRCS})

if (WIN32)
  target_link_libraries(${XBTRACER_NAME} Shlwapi.lib)
endif()

install (TARGETS ${XBTRACER_NAME} RUNTIME DESTINATION ${XRT_INSTALL_UNWRAPPED_DIR})
#install (PROGRAMS ${XRT_LOADER_SCRIPTS} DESTINATION ${XRT_INSTALL_BIN_DIR})
