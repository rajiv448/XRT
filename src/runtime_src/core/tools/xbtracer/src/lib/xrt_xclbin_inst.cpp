// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.

#define XRT_API_SOURCE         // in same dll as api
#include "capture.h"
#include "logger.h"

#include <iostream>

using namespace xrt::tools::xbtracer;

// NOLINTNEXTLINE(cert-err58-cpp)
static const xrt_ftbl& dtbl = xrt_ftbl::get_instance();

/*
 *  Xclbin class instrumented methods
 * */
namespace xrt {
xclbin::xclbin(const std::string& fnm)
{
  auto func = "xrt::xclbin::xclbin(const std::string&)";
  try {
    XRT_TOOLS_XBT_CALL_CTOR(dtbl.xclbin.ctor_fnm, this, fnm);
    /* As pimpl will be updated only after ctor call*/
    XRT_TOOLS_XBT_FUNC_ENTRY(func, fnm);
  }
  catch (const std::exception& ex) {
    std::cout << "Exception: " << ex.what() << '\n';
  }
  XRT_TOOLS_XBT_FUNC_EXIT(func);
}

xclbin::xclbin(const std::vector<char>& data)
{
  auto func = "xrt::xclbin::xclbin(const std::vector<char>&)";
  XRT_TOOLS_XBT_CALL_CTOR(dtbl.xclbin.ctor_raw, this, data);
  /* As pimpl will be updated only after ctor call*/
  XRT_TOOLS_XBT_FUNC_ENTRY(func, &data);
  XRT_TOOLS_XBT_FUNC_EXIT(func);
}

xclbin::xclbin(const axlf* maxlf)
{
  auto func = "xrt::xclbin::xclbin(const axlf*)";
  XRT_TOOLS_XBT_CALL_CTOR(dtbl.xclbin.ctor_axlf, this, maxlf);
  /* As pimpl will be updated only after ctor call*/
  XRT_TOOLS_XBT_FUNC_ENTRY(func, &maxlf);
  XRT_TOOLS_XBT_FUNC_EXIT(func);
}
}  // namespace xrt
