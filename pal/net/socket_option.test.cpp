#include <pal/net/basic_socket>
#include <pal/net/test>


namespace {


using namespace pal_test;
using option = pal::net::socket_base;


template <typename Protocol, typename Option>
struct protocol_and_option: Protocol
{
	using option_type = Option;
};

template <typename Option>
using tcp_v4_with = protocol_and_option<tcp_v4, Option>;

template <typename Option>
using tcp_v6_with = protocol_and_option<tcp_v6, Option>;

template <typename Option>
using udp_v4_with = protocol_and_option<udp_v4, Option>;

template <typename Option>
using udp_v6_with = protocol_and_option<udp_v6, Option>;


TEMPLATE_TEST_CASE("net/socket_option", "", tcp_v4, tcp_v6, udp_v4, udp_v6)
{
	constexpr auto protocol = protocol_v<TestType>;
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


template <typename TestType, typename Option>
std::error_code expected_os_error (const Option &) noexcept
{
	if constexpr (pal::is_linux_build)
	{
		if constexpr (std::is_same_v<Option, option::debug>)
		{
			return std::make_error_code(std::errc::permission_denied);
		}
		else if constexpr (std::is_same_v<Option, option::send_low_watermark>)
		{
			return std::make_error_code(std::errc::no_protocol_option);
		}
	}
	else if constexpr (pal::is_macos_build)
	{
		// no specializations
		// MacOS itself swallows unknown option errors
	}
	else if constexpr (pal::is_windows_build)
	{
		#if !__pal_os_windows
			// not used, just to silence compilers
			constexpr int WSAENOPROTOOPT = ENOPROTOOPT;
			constexpr int WSAEINVAL = EINVAL;
		#endif

		if (is_udp_v<TestType>)
		{
			if constexpr (
				std::is_same_v<Option, option::keepalive> ||
				std::is_same_v<Option, option::linger> ||
				std::is_same_v<Option, option::out_of_band_inline> ||
				std::is_same_v<Option, option::reuse_port>)
			{
				return {WSAENOPROTOOPT, std::system_category()};
			}
			else if constexpr (
				std::is_same_v<Option, option::receive_low_watermark> ||
				std::is_same_v<Option, option::send_low_watermark>)
			{
				return {WSAEINVAL, std::system_category()};
			}
		}
		else if (is_tcp_v<TestType>)
		{
			if constexpr (
				std::is_same_v<Option, option::broadcast> ||
				std::is_same_v<Option, option::receive_low_watermark> ||
				std::is_same_v<Option, option::reuse_port> ||
				std::is_same_v<Option, option::send_low_watermark>)
			{
				return {WSAENOPROTOOPT, std::system_category()};
			}
		}
	}
	return {};
}


TEMPLATE_PRODUCT_TEST_CASE("net/socket_option", "",
	(
		tcp_v4_with,
		tcp_v6_with,
		udp_v4_with,
		udp_v6_with
	),
	(
		option::broadcast,
		option::debug,
		option::do_not_route,
		option::keepalive,
		option::out_of_band_inline,
		option::reuse_address,
		option::reuse_port
	)
)
{
	constexpr auto protocol = protocol_v<TestType>;
	using option_type = typename TestType::option_type;

	socket_t<TestType> socket(protocol);
	REQUIRE(socket.is_open());

	option_type option{true};
	std::error_code error, expected_error = expected_os_error<TestType>(option);

	socket.set_option(option, error);
	CHECK(error == expected_error);
	if (!error)
	{
		CHECK_NOTHROW(socket.set_option(option));

		option = false;
		socket.get_option(option, error);
		CHECK(!error);
		CHECK(option.value() == true);
		CHECK_NOTHROW(socket.get_option(option));
	}
	else
	{
		CHECK_THROWS_AS(
			socket.set_option(option),
			std::system_error
		);
	}

	SECTION("closed")
	{
		socket.close();

		socket.get_option(option, error);
		CHECK(error == std::errc::bad_file_descriptor);
		CHECK_THROWS_AS(
			socket.get_option(option),
			std::system_error
		);

		socket.set_option(option, error);
		CHECK(error == std::errc::bad_file_descriptor);
		CHECK_THROWS_AS(
			socket.set_option(option),
			std::system_error
		);
	}
}


TEMPLATE_PRODUCT_TEST_CASE("net/socket_option", "",
	(
		tcp_v4_with,
		tcp_v6_with,
		udp_v4_with,
		udp_v6_with
	),
	(
		option::receive_buffer_size,
		option::receive_low_watermark,
		option::send_buffer_size,
		option::send_low_watermark
	)
)
{
	constexpr auto protocol = protocol_v<TestType>;
	using option_type = typename TestType::option_type;

	socket_t<TestType> socket(protocol);
	REQUIRE(socket.is_open());

	int value = 4096;
	option_type option{value};
	std::error_code error, expected_error = expected_os_error<TestType>(option);

	socket.set_option(option, error);
	CHECK(error == expected_error);
	if (!error)
	{
		CHECK_NOTHROW(socket.set_option(option));

		option = value/2;
		socket.get_option(option, error);
		CHECK(!error);

		if constexpr (
			pal::is_linux_build &&
			!std::is_same_v<option_type, option::receive_low_watermark>)
		{
			value *= 2;
		}
		CHECK(option.value() == value);

		CHECK_NOTHROW(socket.get_option(option));
	}
	else
	{
		CHECK_THROWS_AS(
			socket.set_option(option),
			std::system_error
		);
	}

	SECTION("closed")
	{
		socket.close();

		socket.get_option(option, error);
		CHECK(error == std::errc::bad_file_descriptor);
		CHECK_THROWS_AS(
			socket.get_option(option),
			std::system_error
		);

		socket.set_option(option, error);
		CHECK(error == std::errc::bad_file_descriptor);
		CHECK_THROWS_AS(
			socket.set_option(option),
			std::system_error
		);
	}
}


TEMPLATE_PRODUCT_TEST_CASE("net/socket_option", "",
	(
		tcp_v4_with,
		tcp_v6_with,
		udp_v4_with,
		udp_v6_with
	),
	(
		option::linger
	)
)
{
	using namespace std::chrono_literals;

	constexpr auto protocol = protocol_v<TestType>;
	using option_type = typename TestType::option_type;

	std::error_code error;

	SECTION("methods")
	{
		option_type linger;
		CHECK(linger.enabled() == false);
		CHECK(linger.timeout() == 0s);

		CHECK(linger.level(protocol) == SOL_SOCKET);
		CHECK(linger.name(protocol) == SO_LINGER);
		CHECK(static_cast<option_type &>(linger).data(protocol) != nullptr);
		CHECK(static_cast<const option_type &>(linger).data(protocol) != nullptr);
		CHECK(linger.size(protocol) == sizeof(::linger));

		auto size = linger.size(protocol);

		linger.resize(protocol, size, error);
		CHECK(!error);

		CHECK_NOTHROW(linger.resize(protocol, size));

		linger.resize(protocol, size * 2, error);
		CHECK(error == pal::net::socket_errc::socket_option_length_error);

		CHECK_THROWS_AS(
			linger.resize(protocol, size * 2),
			std::length_error
		);

		linger.enabled(true);
		linger.timeout(5s);
		CHECK(linger.enabled() == true);
		CHECK(linger.timeout() == 5s);
	}

	socket_t<TestType> socket(protocol);
	REQUIRE(socket.is_open());

	option_type linger{true, 5s};
	socket.set_option(linger, error);
	CHECK(error == expected_os_error<TestType>(linger));
	if (!error)
	{
		CHECK_NOTHROW(socket.set_option(linger));

		linger.timeout(linger.timeout() * 2);
		socket.get_option(linger, error);
		CHECK(!error);
		CHECK(linger.timeout() == 5s);
		CHECK(linger.enabled() == true);

		CHECK_NOTHROW(socket.get_option(linger));
	}

	SECTION("closed")
	{
		socket.close();

		socket.get_option(linger, error);
		CHECK(error == std::errc::bad_file_descriptor);
		CHECK_THROWS_AS(
			socket.get_option(linger),
			std::system_error
		);

		socket.set_option(linger, error);
		CHECK(error == std::errc::bad_file_descriptor);
		CHECK_THROWS_AS(
			socket.set_option(linger),
			std::system_error
		);
	}
}


} // namespace
