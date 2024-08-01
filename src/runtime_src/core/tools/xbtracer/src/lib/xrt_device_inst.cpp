// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.

#include "capture.h"
#include "logger.h"

#include <filesystem>
#include <iostream>

using namespace xrt::tools::xbtracer;
namespace fs = std::filesystem;

// NOLINTNEXTLINE(cert-err58-cpp)
static const xrt_ftbl& dtbl = xrt_ftbl::get_instance();

/*
 *  Device class instrumented methods
 * */
namespace xrt {
device::device(unsigned int index)
{
  XRT_TOOLS_XBT_CALL_CTOR(dtbl.device.ctor, this, index);
   /* As pimpl will be updated only after ctor call*/
  XRT_TOOLS_XBT_FUNC_ENTRY(index);
  handle = this->get_handle();
  XRT_TOOLS_XBT_FUNC_EXIT();
}

uuid device::load_xclbin(const std::string& fnm)
{
  XRT_TOOLS_XBT_FUNC_ENTRY(fnm);
  uuid muuid;
  XRT_TOOLS_XBT_CALL_METD_RET(dtbl.device.load_xclbin_fnm, muuid, fnm);
  unsigned int file_size = fs::file_size(fnm);
  std::vector<unsigned char> buffer(file_size);
  std::ifstream file(fnm);
  // NOLINTNEXTLINE (cppcoreguidelines-pro-type-reinterpret-cast)
  if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
    std::cerr << "Failed to read " << fnm << "\n";
  }
  membuf xclbin(buffer.data(), (unsigned int)file_size);
  XRT_TOOLS_XBT_FUNC_EXIT_RET(muuid, "xclbin", xclbin);
  return muuid;
}

device::~device()
{
  long count = handle.use_count();
  if (count < 2)
  {
    XRT_TOOLS_XBT_FUNC_ENTRY();
    /*
    * TODO:
    * Ideally we need invoke exit trace and then invoke call to dtor.
    * since pimpl will be destroyed after dtor call. so we need to invoke
    * first exit trace and then call dtor. This is working fine in windows
    *
    * In linux, we are seeing crash. This we need to investigate why it is
    * happeneing. For now adding switch for windows and linux.
    */
#ifdef _WIN32
      XRT_TOOLS_XBT_FUNC_EXIT("count", count);
      XRT_TOOLS_XBT_CALL_METD(dtbl.device.dtor);
#else
      XRT_TOOLS_XBT_CALL_METD(dtbl.device.dtor);
      XRT_TOOLS_XBT_FUNC_EXIT("count", count);
#endif /* #ifdef _WIN32 */
  }
}
}  // namespace xrt
