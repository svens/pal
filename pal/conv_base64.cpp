#include <pal/conv>


__pal_begin


conv_result to_base64 (const char *first, const char *last, char *out) noexcept
{
	static constexpr uint8_t map[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789"
		"+/";

	conv_result result{last, nullptr};

	const auto mod = (last - first) % 3;
	last -= mod;

	for (/**/;  first != last;  first += 3)
	{
		*out++ = map[first[0] >> 2];
		*out++ = map[((first[0] << 4) | (first[1] >> 4)) & 0b00111111];
		*out++ = map[((first[1] << 2) | (first[2] >> 6)) & 0b00111111];
		*out++ = map[first[2] & 0b00111111];
	}

	if (mod == 1)
	{
		*out++ = map[first[0] >> 2];
		*out++ = map[((first[0] << 4)) & 0b00111111];
		*out++ = '=';
		*out++ = '=';
	}
	else if (mod == 2)
	{
		*out++ = map[first[0] >> 2];
		*out++ = map[((first[0] << 4) | (first[1] >> 4)) & 0b00111111];
		*out++ = map[(first[1] << 2) & 0b00111111];
		*out++ = '=';
	}

	result.last_out = out;
	return result;
}


conv_result from_base64 (const char *first, const char *last, char *out) noexcept
{
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

	int pad = last >= first + 2 ? (last[-1] == '=') + (last[-2] == '=') : 0;
	auto in = reinterpret_cast<const uint8_t *>(first);
	const auto end = in + ((last - first - pad) / 4) * 4;

	while (in != end)
	{
		auto b0 = map[in[0]], b1 = map[in[1]], b2 = map[in[2]], b3 = map[in[3]];
		if (b0 != 0xff && b1 != 0xff && b2 != 0xff && b3 != 0xff)
		{
			auto v = (b0 << 18) | (b1 << 12) | (b2 << 6) | b3;
			*out++ = (v >> 16) & 0xff;
			*out++ = (v >> 8) & 0xff;
			*out++ = v & 0xff;
			in += 4;
		}
		else
		{
			break;
		}
	}

	for (auto i = in, e = reinterpret_cast<const uint8_t *>(last);  i != e;  ++i)
	{
		if (*i != '=' && map[*i] == 0xff)
		{
			return {reinterpret_cast<const char *>(i), out};
		}
	}

	if (pad == 1)
	{
		auto v = (map[in[0]] << 18) | (map[in[1]] << 12) | (map[in[2]] << 6);
		*out++ = (v >> 16) & 0xff;
		*out++ = (v >> 8) & 0xff;
		in += 3;
	}
	else if (pad == 2)
	{
		*out++ = ((map[in[0]] << 18) | (map[in[1]] << 12)) >> 16;
		in += 2;
	}

	return {reinterpret_cast<const char *>(in) + pad, out};
}


__pal_end
