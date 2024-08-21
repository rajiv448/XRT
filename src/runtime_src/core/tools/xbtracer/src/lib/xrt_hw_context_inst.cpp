// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.

#define XRT_API_SOURCE         // in same dll as api
#include "capture.h"
#include "logger.h"

#include <iostream>

namespace xtx = xrt::tools::xbtracer;

// NOLINTNEXTLINE(cert-err58-cpp)
static const xtx::xrt_ftbl& dtbl = xtx::xrt_ftbl::get_instance();

/*
 * HW Context Class instrumented methods
 * */
namespace xrt {
hw_context::hw_context(const xrt::device& device, const xrt::uuid& xclbin_id,
                       const cfg_param_type& cfg_param)
{
  auto func = "xrt::hw_context::hw_context(const xrt::device&, const xrt::uuid&, const xrt::hw_context::cfg_param_type&)";
  XRT_TOOLS_XBT_CALL_CTOR(dtbl.hw_context.ctor_frm_cfg, this, device,
    xclbin_id, cfg_param);
  /* As pimpl will be updated only after ctor call*/
  XRT_TOOLS_XBT_FUNC_ENTRY(func,
    device.get_handle().get(), xclbin_id, &cfg_param);
  XRT_TOOLS_XBT_FUNC_EXIT(func);
}

hw_context::hw_context(const xrt::device& device, const xrt::uuid& xclbin_id,
                       access_mode mode)
{
  auto func = "xrt::hw_context::hw_context(const xrt::device&, const xrt::uuid&, xrt::hw_context::access_mode)";
  std::string xclbin_id_str = xclbin_id.to_string();  // Convert UUID to string
  XRT_TOOLS_XBT_CALL_CTOR(dtbl.hw_context.ctor_frm_mode, this, device,
    xclbin_id, mode);
  /* As pimpl will be updated only after ctor call*/
  XRT_TOOLS_XBT_FUNC_ENTRY(func,
    device.get_handle().get(), xclbin_id_str.c_str(), (int)mode);
  XRT_TOOLS_XBT_FUNC_EXIT(func);
}

void hw_context::update_qos(const cfg_param_type& qos)
{
  auto func = "xrt::hw_context::update_qos(const xrt::hw_context::cfg_param_type&)";
  XRT_TOOLS_XBT_FUNC_ENTRY(func, &qos);
  XRT_TOOLS_XBT_CALL_METD(dtbl.hw_context.update_qos, qos);
  XRT_TOOLS_XBT_FUNC_EXIT(func);
}
}  // namespace xrt
