#pragma once

/**
 * \file pal/net/ip/host_name.hpp
 * Current machine host name
 */

#include <pal/result.hpp>
#include <string_view>

namespace pal::net::ip
{

/// Return host name for the current machine
result<std::string_view> host_name () noexcept;

} // namespace pal::net::ip
