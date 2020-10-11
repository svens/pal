#include <pal/net/basic_socket>
#include <pal/net/test>


namespace {


using namespace pal_test;


TEMPLATE_TEST_CASE("net/socket_option", "", tcp_v4, tcp_v6, udp_v4, udp_v6)
{
	constexpr auto protocol = TestType::protocol();
	std::error_code error;

	SECTION("socket_option<int>")
	{
		using test_option = pal::net::socket_option<int, 100, 200>;

		test_option option(10);
		CHECK(option.value() == 10);

		option = 20;
		CHECK(option.value() == 20);

		CHECK(option.level(protocol) == 100);
		CHECK(option.name(protocol) == 200);

		CHECK(const_cast<test_option &>(option).data(protocol) != nullptr);
		CHECK(const_cast<const test_option &>(option).data(protocol) != nullptr);

		auto size = option.size(protocol);
		CHECK(size == sizeof(int));

		option.resize(protocol, size, error);
		CHECK(!error);
		CHECK_NOTHROW(option.resize(protocol, size));

		option.resize(protocol, size + 1, error);
		CHECK(error == pal::net::socket_errc::socket_option_length_error);
		CHECK_THROWS_AS(
			option.resize(protocol, size + 1),
			std::length_error
		);
	}

	SECTION("socket_option<bool>")
	{
		using test_option = pal::net::socket_option<bool, 100, 200>;

		test_option option(true);
		CHECK(option.value() == true);
		CHECK(option);
		CHECK_FALSE(!option);

		option = false;
		CHECK(option.value() == false);
		CHECK_FALSE(option);
		CHECK(!option);

		CHECK(option.level(protocol) == 100);
		CHECK(option.name(protocol) == 200);

		CHECK(const_cast<test_option &>(option).data(protocol) != nullptr);
		CHECK(const_cast<const test_option &>(option).data(protocol) != nullptr);

		auto size = option.size(protocol);
		CHECK(size == sizeof(int));

		option.resize(protocol, size, error);
		CHECK(!error);
		CHECK_NOTHROW(option.resize(protocol, size));

		option.resize(protocol, size + 1, error);
		CHECK(error == pal::net::socket_errc::socket_option_length_error);
		CHECK_THROWS_AS(
			option.resize(protocol, size + 1),
			std::length_error
		);
	}
}


} // namespace
