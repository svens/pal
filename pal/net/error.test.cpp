#include <pal/net/error>
#include <pal/net/test>
#include <type_traits>


namespace {


TEST_CASE("net/error")
{
	SECTION("socket_errc")
	{
		#define __pal_net_socket_errc_list(Code, Message) pal::net::socket_errc::Code,
		std::error_code ec = GENERATE(
			values({__pal_net_socket_errc(__pal_net_socket_errc_list)})
		);
		#undef __pal_net_socket_errc_list
		CAPTURE(ec);

		SECTION("message")
		{
			CHECK_FALSE(ec.message().empty());
			CHECK(ec.message() != "unknown");
			CHECK(ec.category() == pal::net::socket_category());
			CHECK(ec.category().name() == std::string{"socket"});
		}
	}


	SECTION("message_bad_alloc")
	{
		std::error_code ec = pal::net::socket_errc::__0;
		pal_test::bad_alloc_once x;
		CHECK_THROWS_AS(ec.message(), std::bad_alloc);
	}


	SECTION("unknown")
	{
		std::error_code ec = static_cast<pal::net::socket_errc>(
			(std::numeric_limits<
				std::underlying_type_t<pal::net::socket_errc>
			>::max)()
		);
		CHECK(ec.message() == "unknown");
		CHECK(ec.category() == pal::net::socket_category());
		CHECK(ec.category().name() == std::string{"socket"});
	}
}


} // namespace
