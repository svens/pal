#include <pal/crypto/__certificate.hpp>
#include <algorithm>

namespace pal::crypto
{

void alternative_name::const_iterator::load_next_entry () noexcept
{
	while (at_ < owner_->count)
	{
		if (owner_->load_at(at_++, buffer_, entry_))
		{
			return;
		}
	}

	owner_ = nullptr;
	entry_.emplace<std::monostate>();
}

std::to_chars_result alternative_name::to_chars (char *first, char *last) const noexcept
{
	auto *p = first;

	// clang-format off
	const auto append = [&]<typename... Ts>(const std::variant<Ts...> &e) noexcept -> bool
	{
		return (... && [&]<typename T>() noexcept -> bool
		{
			if constexpr (requires { T::label; })
			{
				if (const auto *v = std::get_if<T>(&e))
				{
					if (static_cast<size_t>(last - p) < T::label.size() + 1U + v->size() + 2U)
					{
						return false;
					}
					p = std::ranges::copy(T::label, p).out;
					*p++ = '=';
					p = std::ranges::copy(std::string_view{*v}, p).out;
					*p++ = ',';
					*p++ = ' ';
				}
			}
			return true;
		}.template operator()<Ts>());
	};
	// clang-format on

	for (const auto &entry: *this)
	{
		if (!append(entry))
		{
			return {.ptr = last, .ec = std::errc::value_too_large};
		}
	}

	if (p != first)
	{
		p -= 2;
	}

	return {.ptr = p, .ec = std::errc{}};
}

} // namespace pal::crypto
