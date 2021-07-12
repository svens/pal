#include <pal/net/socket_option>
#include <pal/net/test>


namespace {


using namespace pal_test;
using namespace std::chrono_literals;


TEMPLATE_TEST_CASE("net/socket_option", "",
	udp_v4,
	tcp_v4,
	udp_v6,
	tcp_v6,
	udp_v6_only,
	tcp_v6_only)
{
	SECTION("int")
	{
		using option_type = pal::net::socket_option<int, 100, 200>;

		option_type o{10};
		CHECK(o.value() == 10);

		o = 20;
		CHECK(o.value() == 20);

		CHECK(o.level(TestType::protocol_v) == 100);
		CHECK(o.name(TestType::protocol_v) == 200);

		CHECK(const_cast<option_type &>(o).data(TestType::protocol_v) != nullptr);
		CHECK(const_cast<const option_type &>(o).data(TestType::protocol_v) != nullptr);

		auto size = o.size(TestType::protocol_v);
		CHECK(size == sizeof(int));

		auto r = o.resize(TestType::protocol_v, size);
		CHECK(r);

		r = o.resize(TestType::protocol_v, size + 1);
		REQUIRE_FALSE(r);
		CHECK(r.error() == std::errc::invalid_argument);
	}

	SECTION("bool")
	{
		using option_type = pal::net::socket_option<bool, 100, 200>;

		option_type o{true};
		CHECK(o.value() == true);
		CHECK(o);
		CHECK_FALSE(!o);

		o = false;
		CHECK(o.value() == false);
		CHECK_FALSE(o);
		CHECK(!o);

		CHECK(o.level(TestType::protocol_v) == 100);
		CHECK(o.name(TestType::protocol_v) == 200);

		CHECK(const_cast<option_type &>(o).data(TestType::protocol_v) != nullptr);
		CHECK(const_cast<const option_type &>(o).data(TestType::protocol_v) != nullptr);

		auto size = o.size(TestType::protocol_v);
		CHECK(size == sizeof(int));

		auto r = o.resize(TestType::protocol_v, size);
		CHECK(r);

		r = o.resize(TestType::protocol_v, size + 1);
		REQUIRE_FALSE(r);
		CHECK(r.error() == std::errc::invalid_argument);
	}

	SECTION("linger")
	{
		pal::net::linger o{true, 1s};
		CHECK(o.enabled());
		CHECK(o.timeout() == 1s);

		o.enabled(false);
		CHECK_FALSE(o.enabled());
		CHECK(o.timeout() == 1s);

		o.timeout(5s);
		CHECK_FALSE(o.enabled());
		CHECK(o.timeout() == 5s);

		CHECK(o.level(TestType::protocol_v) == SOL_SOCKET);
		CHECK(o.name(TestType::protocol_v) == SO_LINGER);

		CHECK(const_cast<pal::net::linger &>(o).data(TestType::protocol_v) != nullptr);
		CHECK(const_cast<const pal::net::linger &>(o).data(TestType::protocol_v) != nullptr);

		auto size = o.size(TestType::protocol_v);
		CHECK(size == sizeof(::linger));

		auto r = o.resize(TestType::protocol_v, size);
		CHECK(r);

		r = o.resize(TestType::protocol_v, size + 1);
		REQUIRE_FALSE(r);
		CHECK(r.error() == std::errc::invalid_argument);
	}

	SECTION("receive_timeout")
	{
		pal::net::receive_timeout o{1s};
		CHECK(o.timeout() == 1s);

		o.timeout(5s);
		CHECK(o.timeout() == 5s);

		CHECK(o.level(TestType::protocol_v) == SOL_SOCKET);
		CHECK(o.name(TestType::protocol_v) == SO_RCVTIMEO);

		CHECK(const_cast<pal::net::receive_timeout &>(o).data(TestType::protocol_v) != nullptr);
		CHECK(const_cast<const pal::net::receive_timeout &>(o).data(TestType::protocol_v) != nullptr);

		auto size = o.size(TestType::protocol_v);
		CHECK(size == sizeof(pal::net::__bits::timeval));

		auto r = o.resize(TestType::protocol_v, size);
		CHECK(r);

		r = o.resize(TestType::protocol_v, size + 1);
		REQUIRE_FALSE(r);
		CHECK(r.error() == std::errc::invalid_argument);
	}

	SECTION("send_timeout")
	{
		pal::net::send_timeout o{1s};
		CHECK(o.timeout() == 1s);

		o.timeout(5s);
		CHECK(o.timeout() == 5s);

		CHECK(o.level(TestType::protocol_v) == SOL_SOCKET);
		CHECK(o.name(TestType::protocol_v) == SO_SNDTIMEO);

		CHECK(const_cast<pal::net::send_timeout &>(o).data(TestType::protocol_v) != nullptr);
		CHECK(const_cast<const pal::net::send_timeout &>(o).data(TestType::protocol_v) != nullptr);

		auto size = o.size(TestType::protocol_v);
		CHECK(size == sizeof(pal::net::__bits::timeval));

		auto r = o.resize(TestType::protocol_v, size);
		CHECK(r);

		r = o.resize(TestType::protocol_v, size + 1);
		REQUIRE_FALSE(r);
		CHECK(r.error() == std::errc::invalid_argument);
	}
}


using namespace pal::net;


template <typename Protocol, typename Option>
std::error_code expected_os_error () noexcept
{
	if constexpr (pal::is_linux_build)
	{
		if constexpr (std::is_same_v<Option, debug>)
		{
			return std::make_error_code(std::errc::permission_denied);
		}
		else if constexpr (std::is_same_v<Option, send_low_watermark>)
		{
			return std::make_error_code(std::errc::no_protocol_option);
		}
	}
	else if constexpr (pal::is_macos_build)
	{
		// no special cases for MacOS
	}
	else if constexpr (pal::is_windows_build)
	{
		#if !__pal_os_windows
			constexpr int WSAENOPROTOOPT = ENOPROTOOPT;
			constexpr int WSAEINVAL = EINVAL;
		#endif

		if (pal_test::is_udp_v<Protocol>)
		{
			if constexpr (
				std::is_same_v<Option, keepalive> ||
				std::is_same_v<Option, pal::net::linger> ||
				std::is_same_v<Option, out_of_band_inline> ||
				std::is_same_v<Option, reuse_port>)
			{
				return {WSAENOPROTOOPT, std::system_category()};
			}
			else if constexpr (
				std::is_same_v<Option, receive_low_watermark> ||
				std::is_same_v<Option, send_low_watermark>)
			{
				return {WSAEINVAL, std::system_category()};
			}
		}
		else if (pal_test::is_tcp_v<Protocol>)
		{
			if constexpr (
				std::is_same_v<Option, broadcast> ||
				std::is_same_v<Option, receive_low_watermark> ||
				std::is_same_v<Option, reuse_port> ||
				std::is_same_v<Option, send_low_watermark>)
			{
				return {WSAENOPROTOOPT, std::system_category()};
			}
		}
	}
	return {};
}


TEMPLATE_PRODUCT_TEST_CASE("net/socket_option", "",
	(
		udp_v4::with,
		tcp_v4::with,
		udp_v6::with,
		tcp_v6::with,
		udp_v6_only::with,
		tcp_v6_only::with
	),
	(
		broadcast,
		debug,
		do_not_route,
		keepalive,
		out_of_band_inline,
		reuse_address,
		reuse_port
	)
)
{
	using protocol_t = typename TestType::first_type;
	auto socket = protocol_t::make_socket();
	REQUIRE(socket);

	using option_t = typename TestType::second_type;
	option_t option{true};
	auto set_option = socket->set_option(option);

	const auto expected_error = expected_os_error<protocol_t, option_t>();
	if (expected_error)
	{
		REQUIRE_FALSE(set_option);
		CHECK(set_option.error() == expected_error);
	}
	else
	{
		REQUIRE(set_option);
		option = false;
		REQUIRE(socket->get_option(option));
		CHECK(option.value());
	}

	SECTION("bad file descriptor")
	{
		pal_test::handle_guard{socket->native_handle()};

		set_option = socket->set_option(option);
		REQUIRE_FALSE(set_option);

		auto get_option = socket->get_option(option);
		REQUIRE_FALSE(get_option);

		if constexpr (pal::is_windows_build)
		{
			if (expected_error)
			{
				// Windows has different order checking for
				// invalid handle vs unsupported option
				// here we settle set_option() just to return error
			}
			else
			{
				CHECK(set_option.error() == std::errc::bad_file_descriptor);
				CHECK(get_option.error() == std::errc::bad_file_descriptor);
			}
		}
		else
		{
			CHECK(set_option.error() == std::errc::bad_file_descriptor);
			CHECK(get_option.error() == std::errc::bad_file_descriptor);
		}
	}
}


TEMPLATE_PRODUCT_TEST_CASE("net/socket_option", "",
	(
		udp_v4::with,
		tcp_v4::with,
		udp_v6::with,
		tcp_v6::with,
		udp_v6_only::with,
		tcp_v6_only::with
	),
	(
		receive_buffer_size,
		receive_low_watermark,
		send_buffer_size,
		send_low_watermark
	)
)
{
	using protocol_t = typename TestType::first_type;
	auto socket = protocol_t::make_socket();
	REQUIRE(socket);

	using option_t = typename TestType::second_type;
	int value = 4096;
	option_t option{value};
	auto set_option = socket->set_option(option);

	const auto expected_error = expected_os_error<protocol_t, option_t>();
	if (expected_error)
	{
		REQUIRE_FALSE(set_option);
		CHECK(set_option.error() == expected_error);
	}
	else
	{
		REQUIRE(set_option);
		option = value / 2;
		REQUIRE(socket->get_option(option));

		if constexpr (pal::is_linux_build)
		{
			CHECK(option.value() >= value);
		}
		else
		{
			CHECK(option.value() == value);
		}
	}

	SECTION("bad file descriptor")
	{
		pal_test::handle_guard{socket->native_handle()};

		set_option = socket->set_option(option);
		REQUIRE_FALSE(set_option);
		CHECK(set_option.error() == std::errc::bad_file_descriptor);

		auto get_option = socket->get_option(option);
		REQUIRE_FALSE(get_option);
		CHECK(get_option.error() == std::errc::bad_file_descriptor);
	}
}


using linger = pal::net::linger;


TEMPLATE_PRODUCT_TEST_CASE("net/socket_option", "",
	(
		udp_v4::with,
		tcp_v4::with,
		udp_v6::with,
		tcp_v6::with,
		udp_v6_only::with,
		tcp_v6_only::with
	),
	(
		linger
	)
)
{
	using protocol_t = typename TestType::first_type;
	auto socket = protocol_t::make_socket();
	REQUIRE(socket);

	using option_t = typename TestType::second_type;
	option_t option{true, 3s};
	auto set_option = socket->set_option(option);

	const auto expected_error = expected_os_error<protocol_t, option_t>();
	if (expected_error)
	{
		REQUIRE_FALSE(set_option);
		CHECK(set_option.error() == expected_error);
	}
	else
	{
		REQUIRE(set_option);
		option.enabled(false);
		option.timeout(1s);
		REQUIRE(socket->get_option(option));
		CHECK(option.enabled() == true);
		CHECK(option.timeout() == 3s);
	}

	SECTION("bad file descriptor")
	{
		pal_test::handle_guard{socket->native_handle()};

		set_option = socket->set_option(option);
		REQUIRE_FALSE(set_option);
		CHECK(set_option.error() == std::errc::bad_file_descriptor);

		auto get_option = socket->get_option(option);
		REQUIRE_FALSE(get_option);
		CHECK(get_option.error() == std::errc::bad_file_descriptor);
	}
}


TEMPLATE_PRODUCT_TEST_CASE("net/socket_option", "",
	(
		udp_v4::with,
		tcp_v4::with,
		udp_v6::with,
		tcp_v6::with,
		udp_v6_only::with,
		tcp_v6_only::with
	),
	(
		receive_timeout,
		send_timeout
	)
)
{
	using protocol_t = typename TestType::first_type;
	auto socket = protocol_t::make_socket();
	REQUIRE(socket);

	using option_t = typename TestType::second_type;
	option_t option{1s};
	auto set_option = socket->set_option(option);
	REQUIRE(set_option);

	option.timeout(2s);
	REQUIRE(socket->get_option(option));
	CHECK(option.timeout() == 1s);

	SECTION("bad file descriptor")
	{
		pal_test::handle_guard{socket->native_handle()};

		set_option = socket->set_option(option);
		REQUIRE_FALSE(set_option);
		CHECK(set_option.error() == std::errc::bad_file_descriptor);

		auto get_option = socket->get_option(option);
		REQUIRE_FALSE(get_option);
		CHECK(get_option.error() == std::errc::bad_file_descriptor);
	}
}


} // namespace
