#pragma once

/**
 * \file pal/crypto/alternative_name_value.hpp
 * X.509 certificate alternative name value list
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace pal::crypto
{

class certificate;

/// Packed alternative name value list for hostname/IP lookup.
/// Stores only DNS (FQDN) and IP entries from the Subject Alternative Name extension;
/// other entry types are ignored.
class alternative_name_value
{
public:

	/// Return true if \a name matches any stored entry.
	///
	/// DNS entries support single-level wildcard matching: *.example.com matches
	/// sub.example.com but not a.b.example.com. Inputs starting with '.' or '*'
	/// are rejected.
	[[nodiscard]] bool contains (std::string_view name) const noexcept
	{
		if (name.starts_with('.') || name.starts_with('*'))
		{
			return false;
		}

		const auto *p = data_.data();
		auto k = static_cast<kind>(*p);
		while (k != kind::end)
		{
			const std::string_view entry{p + 2, static_cast<uint8_t>(p[1])};

			if (k == kind::fqdn && matches_fqdn(name, entry))
			{
				return true;
			}
			if (k == kind::ip && name == entry)
			{
				return true;
			}

			p += 2 + entry.size();
			k = static_cast<kind>(p[0]);
		}

		return false;
	}

private:

	enum class kind : uint8_t
	{
		end,
		ip,
		fqdn
	};

	static constexpr size_t max_data = 2048;
	std::array<char, max_data> data_{};

	[[nodiscard]] char *append (char *p, kind k, std::string_view value) noexcept
	{
		if (std::in_range<uint8_t>(value.size()) && p + 2 + value.size() < data_.data() + max_data)
		{
			p[0] = static_cast<char>(k);
			p[1] = static_cast<char>(value.size());
			return std::ranges::copy(value, p + 2).out;
		}
		return nullptr;
	}

	static bool matches_fqdn (std::string_view name, std::string_view entry) noexcept
	{
		if (entry.starts_with("*."))
		{
			if (const auto dot = name.find('.'); dot != name.npos)
			{
				name.remove_prefix(dot);
				entry.remove_prefix(1);
			}
		}
		return name == entry;
	}

	friend class certificate;
};

} // namespace pal::crypto
