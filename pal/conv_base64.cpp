#include <pal/conv>


__pal_begin


conv_result to_base64 (
	const mutable_buffer &dest,
	const const_buffer &source) noexcept
{
	const auto encoded_size = (((source.size() * 4) / 3) + 3) & ~3;
	if (encoded_size > dest.size())
	{
		return {0, std::errc::value_too_large};
	}

	static constexpr uint8_t map[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789"
		"+/";

	auto to = static_cast<uint8_t *>(dest.data());
	auto from = static_cast<const uint8_t *>(source.data());
	const auto mod = source.size() % 3;
	const auto end = from + source.size() - mod;

	for (/**/;  from != end;  from += 3)
	{
		*to++ = map[from[0] >> 2];
		*to++ = map[((from[0] << 4) | (from[1] >> 4)) & 0b00111111];
		*to++ = map[((from[1] << 2) | (from[2] >> 6)) & 0b00111111];
		*to++ = map[from[2] & 0b00111111];
	}

	if (mod == 1)
	{
		*to++ = map[from[0] >> 2];
		*to++ = map[((from[0] << 4)) & 0b00111111];
		*to++ = '=';
		*to++ = '=';
	}
	else if (mod == 2)
	{
		*to++ = map[from[0] >> 2];
		*to++ = map[((from[0] << 4) | (from[1] >> 4)) & 0b00111111];
		*to++ = map[(from[1] << 2) & 0b00111111];
		*to++ = '=';
	}

	return {encoded_size, {}};
}


conv_result from_base64 (
	const mutable_buffer &dest,
	const const_buffer &source) noexcept
{
	if (!source.size())
	{
		return {0, std::errc{}};
	}
	else if (source.size() % 4 != 0)
	{
		return {0, std::errc::message_size};
	}

	const auto end = static_cast<const uint8_t *>(source.data()) + source.size();
	auto pad_size = (end[-1] == '=') + (end[-2] == '=');
	if (dest.size() < (source.size() - pad_size) * 3 / 4)
	{
		return {0, std::errc::value_too_large};
	}

	static constexpr uint8_t map[] =
		// _0  _1  _2  _3  _4  _5  _6  _7  _8  _9  _a  _b  _c  _d  _e  _f
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 0_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 1_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x3e\xff\xff\xff\x3f" // 2_
		"\x34\x35\x36\x37\x38\x39\x3a\x3b\x3c\x3d\xff\xff\xff\xff\xff\xff" // 3_
		"\xff\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e" // 4_
		"\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\xff\xff\xff\xff\xff" // 5_
		"\xff\x1a\x1b\x1c\x1d\x1e\x1f\x20\x21\x22\x23\x24\x25\x26\x27\x28" // 6_
		"\x29\x2a\x2b\x2c\x2d\x2e\x2f\x30\x31\x32\x33\xff\xff\xff\xff\xff" // 7_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 8_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // 9_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // a_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // b_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // c_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // d_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // e_
		"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff" // f_
	;

	auto from = static_cast<const uint8_t *>(source.data());
	auto to = static_cast<uint8_t *>(dest.data());

	for (auto e = from + ((source.size() - pad_size) / 4) * 4;  from != e;  from += 4)
	{
		auto b0 = map[from[0]], b1 = map[from[1]], b2 = map[from[2]], b3 = map[from[3]];
		if (b0 != 0xff && b1 != 0xff && b2 != 0xff && b3 != 0xff)
		{
			auto v = (b0 << 18) | (b1 << 12) | (b2 << 6) | b3;
			*to++ = (v >> 16) & 0xff;
			*to++ = (v >> 8) & 0xff;
			*to++ = v & 0xff;
		}
		else
		{
			break;
		}
	}

	for (auto it = from, e = end;  it != e;  ++it)
	{
		if (*it != '=' && map[*it] == 0xff)
		{
			return
			{
				static_cast<size_t>(it - static_cast<const uint8_t *>(source.data())),
				std::errc::invalid_argument
			};
		}
	}

	if (pad_size == 1)
	{
		auto v = (map[from[0]] << 18) | (map[from[1]] << 12) | (map[from[2]] << 6);
		*to++ = (v >> 16) & 0xff;
		*to++ = (v >> 8) & 0xff;
		from += 3;
	}
	else if (pad_size == 2)
	{
		*to++ = ((map[from[0]] << 18) | (map[from[1]] << 12)) >> 16;
		from += 2;
	}

	return
	{
		static_cast<size_t>(to - static_cast<uint8_t *>(dest.data())),
		std::errc{}
	};
}


__pal_end
