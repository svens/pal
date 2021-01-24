#include <pal/crypto/error>
#include <pal/test>
#include <type_traits>


namespace {


TEST_CASE("crypto/error")
{
	SECTION("errc")
	{
		#define __pal_crypto_errc_value(Code, Message) pal::crypto::errc::Code,
		std::error_code ec = GENERATE(values({__pal_crypto_errc(__pal_crypto_errc_value)}));
		#undef __pal_crypto_errc_value
		CAPTURE(ec);

		SECTION("message")
		{
			CHECK_FALSE(ec.message().empty());
			CHECK(ec.message() != "unknown");
			CHECK(ec.category() == pal::crypto::category());
			CHECK(ec.category().name() == std::string{"crypto"});
		}
	}

	SECTION("unknown")
	{
		std::error_code ec = static_cast<pal::crypto::errc>(
			std::numeric_limits<std::underlying_type_t<pal::crypto::errc>>::max()
		);
		CHECK(ec.message() == "unknown");
		CHECK(ec.category() == pal::crypto::category());
		CHECK(ec.category().name() == std::string{"crypto"});
	}

	SECTION("system_category")
	{
		std::error_code ec{1, pal::crypto::system_category()};
		CHECK(!ec.message().empty());
		CHECK(ec.category() == pal::crypto::system_category());
		if constexpr (pal::is_windows_build)
		{
			CHECK(ec.category().name() == std::string{"system"});
		}
		else
		{
			CHECK(ec.category().name() == std::string{"crypto"});
		}
	}
}


} // namespace
