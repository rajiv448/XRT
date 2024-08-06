// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.

#include "replay_xrt.hpp"

namespace xrt_core::tools::xbreplay {

/**
 * Replay maintains a map where each member function of the XRT classes is associated
 * with a corresponding lambda function. This API adds a lambda function entry for each
 * corresponding member of the xrt::hw_context class.
 */
void replay_xrt::register_hwctxt_class_func()
{
  m_api_map["xrt::hw_context::hw_context(const xrt::device&, const xrt::uuid&, xrt::hw_context::access_mode)"] =
  [this](replay_xrt& handle, std::shared_ptr<utils::message> msg)
  {
    const std::vector<std::pair<std::string, std::string>>& args = msg->m_args;

    auto dev_ref = std::stoull(args[0].second, nullptr, utils::base_hex);
    auto acc_md = static_cast<xrt::hw_context::access_mode>(std::stoul(args[2].second));
    const std::string& uuid_str = args[1].second;
    xrt::uuid input_uid(uuid_str);

    auto dev = m_device_hndle_map[dev_ref];

    if (dev != nullptr)
    {
      auto hwctxt_hdl =
          std::make_shared<xrt::hw_context>(*dev, input_uid, acc_md);

      m_hwctx_hndle_map[msg->m_handle] = hwctxt_hdl;
    }
    else
      throw std::runtime_error("Failed to get Dev Handle ");
  };
}
}// end of namespace

