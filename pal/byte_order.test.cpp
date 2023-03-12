#include <pal/byte_order>
#include <pal/test>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <limits>

#if __pal_os_linux || __pal_os_macos
	#include <arpa/inet.h>
#elif __pal_os_windows
	#include <winsock2.h>
#endif

#if __pal_os_linux
	#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		#define htonll(v) __builtin_bswap64(v)
		#define ntohll(v) __builtin_bswap64(v)
	#else
		#define htonll(v) (v)
		#define ntohll(v) (v)
	#endif
#endif


namespace {


// "independent" implementations to confirm pal::XtoY results
inline auto hton (uint16_t v) noexcept { return htons(v); }
inline auto hton (uint32_t v) noexcept { return htonl(v); }
inline auto hton (uint64_t v) noexcept { return htonll(v); }
inline auto ntoh (uint16_t v) noexcept { return ntohs(v); }
inline auto ntoh (uint32_t v) noexcept { return ntohl(v); }
inline auto ntoh (uint64_t v) noexcept { return ntohll(v); }


TEMPLATE_TEST_CASE("byte_order", "",
	uint16_t,
	uint32_t,
	uint64_t)
{
	using limits = std::numeric_limits<TestType>;
	auto value = GENERATE(
		(limits::min)(),
		(limits::lowest)(),
		(limits::max)(),
		static_cast<TestType>((limits::lowest)() + (limits::max)() / 2)
	);
	CAPTURE(value);

	CHECK(hton(value) == pal::hton(value));
	CHECK(ntoh(value) == pal::ntoh(value));
}


} // namespace
