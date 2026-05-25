#include <pal/crypto/__certificate.hpp>
#include <pal/crypto/oid.hpp>
#include <algorithm>

namespace pal::crypto
{

void distinguished_name::const_iterator::load_next_entry () noexcept
{
	if (!owner_->load_at(at_, oid_, value_, entry_))
	{
		owner_ = nullptr;
	}
	else
	{
		++at_;
	}
}

std::to_chars_result distinguished_name::to_chars (char *first, char *last) const noexcept
{
	entry e;
	oid_buffer oid_buf{};
	value_buffer value_buf{};

	auto *p = first;
	auto index = impl_->count;
	while (impl_->load_at(--index, oid_buf, value_buf, e))
	{
		const auto alias = oid::alias_or(e.oid);
		if (static_cast<size_t>(last - p) < alias.size() + 1U + e.value.size() + 2U)
		{
			return {.ptr = last, .ec = std::errc::value_too_large};
		}

		p = std::ranges::copy(alias, p).out;
		*p++ = '=';
		p = std::ranges::copy(e.value, p).out;
		*p++ = ',';
		*p++ = ' ';
	}

	if (p != first)
	{
		// strip final separator
		p -= 2;
	}

	return {.ptr = p, .ec = std::errc{}};
}

} // namespace pal::crypto
