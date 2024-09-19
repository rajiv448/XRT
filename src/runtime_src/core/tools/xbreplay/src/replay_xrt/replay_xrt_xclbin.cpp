// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.

#include "replay_xrt.hpp"

namespace xrt_core::tools::xbreplay {

/**
 * Replay maintains a map where each member function of the XRT classes is associated
 * with a corresponding lambda function. This API adds a lambda function entry for each
 * corresponding member of the xrt::xclbin class.
 */
void replay_xrt::register_xclbin_class_func()
{
  m_api_map["xrt::xclbin::xclbin(const std::string&)"] =
  [this] (replay_xrt& handle, std::shared_ptr<utils::message>msg)
  {
    //const std::vector <std::pair<std::string, std::string>> &args = msg->m_args;
    std::string str_xclbin = get_file_path(msg, ".xclbin");
    auto xclbin_hdl = std::make_shared<xrt::xclbin>(str_xclbin);
    m_xclbin_hndle_map[msg->m_handle] = xclbin_hdl;
  };

  m_api_map["xrt::xclbin::~xclbin()"] =
  [this] (replay_xrt& handle, std::shared_ptr<utils::message>msg)
  {
    auto ptr = m_xclbin_hndle_map[msg->m_handle];

    if (ptr)
      m_xclbin_hndle_map.erase(msg->m_handle);
    else
      throw std::runtime_error("Failed to get xclbin handle");
  };
}

}// end of namespace
