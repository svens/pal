#pragma once

/**
 * \file pal/crypto/oid.hpp
 * Object identifiers for X.509 certificates
 */

#include <array>
#include <string_view>
#include <utility>

namespace pal::crypto::oid
{

/// \defgroup crypto_oid OID values
/// \see https://en.wikipedia.org/wiki/Object_identifier
/// \{

constexpr std::string_view

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.8.1
	collective_state_or_province_name = "2.5.4.8.1",

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.9.1
	collective_street_address = "2.5.4.9.1",

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.3
	common_name = "2.5.4.3",

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.6
	country_name = "2.5.4.6",

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.13
	description = "2.5.4.13",

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.42
	given_name = "2.5.4.42",

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.7
	locality_name = "2.5.4.7",

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.10
	organization_name = "2.5.4.10",

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.11
	organizational_unit_name = "2.5.4.11",

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.5
	serial_number = "2.5.4.5",

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.8
	state_or_province_name = "2.5.4.8",

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.9
	street_address = "2.5.4.9",

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.4
	surname = "2.5.4.4",

	/// http://www.oid-info.com/cgi-bin/display?action=display&oid=2.5.4.12
	title = "2.5.4.12";

/// \}

/// Returns RFC 1779 §2.3 alias for \a oid, or \a oid itself if none exists.
constexpr std::string_view alias_or (std::string_view oid) noexcept
{
	using namespace std::string_view_literals;

	// clang-format off
	constexpr auto map = std::to_array<std::pair<std::string_view, std::string_view>>(
	{
		{ common_name, "CN"sv },
		{ country_name, "C"sv },
		{ locality_name, "L"sv },
		{ organization_name, "O"sv },
		{ organizational_unit_name, "OU"sv },
		{ state_or_province_name, "ST"sv },
		{ street_address, "STREET"sv },
	});
	// clang-format on

	for (const auto &[key, alias]: map)
	{
		if (key == oid)
		{
			return alias;
		}
	}
	return oid;
}

} // namespace pal::crypto::oid
