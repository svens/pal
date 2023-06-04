#include <pal/__platform_sdk>
#include <pal/crypto/certificate>
#include <pal/conv>
#include <pal/memory>
#include <pal/version>
#include <cctype>
#include <string_view>

#if __pal_os_linux
	#include <pal/crypto/certificate_linux.ipp>
#elif __pal_os_macos
	#include <pal/crypto/certificate_macos.ipp>
#elif __pal_os_windows
	#include <pal/crypto/certificate_windows.ipp>
#endif

namespace pal::crypto {

namespace {

//
// because of PEM envelope and whitespaces, can't use pal::base64 methods
//

result<std::string_view> unwrap (std::string_view pem) noexcept
{
	static constexpr std::string_view prefix = "-----BEGIN CERTIFICATE-----\n";
	if (pem.starts_with(prefix))
	{
		pem.remove_prefix(prefix.size());
	}
	else
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	static constexpr std::string_view suffix = "\n-----END CERTIFICATE-----\n";
	if (auto suffix_pos = pem.find(suffix);  suffix_pos != pem.npos)
	{
		pem.remove_suffix(pem.size() - suffix_pos);
	}
	else
	{
		return make_unexpected(std::errc::invalid_argument);
	}

	return pem;
}

result<std::span<const std::byte>> decode (std::string_view pem, std::byte *buf) noexcept
{
	static constexpr unsigned char lookup[] =
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

	auto it = buf;
	int32_t val = 0, valb = -8;
	for (uint8_t ch: pem)
	{
		if (lookup[ch] != 0xff)
		{
			val = (val << 6) | lookup[ch];
			if ((valb += 6) >= 0)
			{
				*it++ = static_cast<std::byte>((val >> valb) & 0xff);
				valb -= 8;
			}
		}
		else if (std::isspace(ch))
		{
			continue;
		}
		else if (ch == '=')
		{
			break;
		}
		else
		{
			return make_unexpected(std::errc::invalid_argument);
		}
	}
	return {buf, size_t(it - buf)};
}

} // namespace

result<certificate> certificate::import_pem (std::string_view pem) noexcept
{
	return unwrap(pem).and_then([](auto pem) -> result<certificate>
	{
		if (auto der_buf = temporary_buffer<4096>{std::nothrow, pem.size() / 4 * 3})
		{
			return decode(pem, static_cast<std::byte *>(der_buf.get())).and_then(import_der);
		}
		return make_unexpected(std::errc::not_enough_memory);
	});
}

} // namespace pal::crypto
