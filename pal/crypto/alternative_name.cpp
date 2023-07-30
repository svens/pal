#include <pal/crypto/alternative_name>

namespace pal::crypto {

bool alternative_name::has_fqdn_match (std::string_view fqdn) const noexcept
{
	if (fqdn.starts_with('.') || fqdn.starts_with('*'))
	{
		return false;
	}

	for (const auto &entry: *this)
	{
		if (auto value = std::get_if<dns_name>(&entry))
		{
			std::string_view dns = *value, expected = fqdn;
			if (dns.starts_with("*."))
			{
				if (auto subdomain = expected.find('.');  subdomain != expected.npos)
				{
					expected.remove_prefix(subdomain);
					dns.remove_prefix(1);
				}
				else
				{
					continue;
				}
			}
			if (dns == expected)
			{
				return true;
			}
		}
	}

	return false;
}

} // namespace pal::crypto
