// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.

#pragma once

#include "experimental/xrt_elf.h"
#include "experimental/xrt_hw_context.h"
#include "experimental/xrt_ip.h"
#include "experimental/xrt_module.h"
#include "experimental/xrt_system.h"
#include "experimental/xrt_xclbin.h"
#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_graph.h"
#include "xrt/xrt_kernel.h"


#include "xrt_device_inst.h"

namespace xrt::tools::xbtracer {

class xrt_ftbl
{
  public:
  xrt_device_ftbl     device;

  static xrt_ftbl& get_instance()
  {
    static xrt_ftbl instance;
    return instance;
  }

  xrt_ftbl(const xrt_ftbl&) = delete;
  void operator=(const xrt_ftbl&) = delete;

  private:
  xrt_ftbl() {}
};

} // namespace xrt::tools::xbtracer
