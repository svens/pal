#include <pal/net/basic_socket>
#include <pal/net/test>


namespace {

using namespace pal_test;


TEMPLATE_TEST_CASE("net/socket", "", tcp_v4, tcp_v6, udp_v4, udp_v6)
{
	constexpr auto protocol = TestType::protocol();
	using protocol_type = decltype(protocol);
	using socket_type = typename protocol_type::socket;
	using endpoint_type = typename socket_type::endpoint_type;

	const endpoint_type
		any{protocol, 0},
		bind_endpoint{protocol, 3478},
		connect_endpoint{TestType::address_type::loopback(), 3478};

	SECTION("ctor")
	{
		socket_type socket;
		CHECK_FALSE(socket.is_open());
		CHECK(socket.native_handle() == socket_type::invalid);
	}

	SECTION("ctor(socket&&)")
	{
		socket_type a(protocol);
		REQUIRE(a.is_open());
		auto b(std::move(a));
		CHECK(b.is_open());
		CHECK_FALSE(a.is_open());
	}

	SECTION("ctor(native_handle_type)")
	{
		socket_type a(protocol);
		REQUIRE(a.is_open());

		socket_type b(protocol, a.native_handle());
		CHECK(b.is_open());

		// b is owner now
		a.release();
	}

	SECTION("operator=(socket&&)")
	{
		socket_type a(protocol), b;
		REQUIRE(a.is_open());
		CHECK_FALSE(b.is_open());
		b = std::move(a);
		CHECK(b.is_open());
		CHECK_FALSE(a.is_open());
	}

	SECTION("assign / release")
	{
		// a closed, b opened
		socket_type a, b(protocol);
		CHECK(!a.is_open());
		REQUIRE(b.is_open());

		// a <- b
		std::error_code error;
		a.assign(protocol, b.release(), error);
		REQUIRE(!error);

		// a <- invalid
		a.assign(protocol, pal::net::socket_base::invalid, error);
		CHECK(error == std::errc::bad_file_descriptor);

		// a <- invalid
		CHECK_THROWS_AS(
			a.assign(protocol, pal::net::socket_base::invalid),
			std::system_error
		);

		// a <- reopened b
		b.open(protocol);
		a.assign(protocol, b.native_handle(), error);
		CHECK(error == pal::net::socket_errc::already_open);

		// a <- reopened b
		CHECK_THROWS_AS(
			a.assign(protocol, b.native_handle()),
			std::system_error
		);

		// close a, a <- b
		CHECK_NOTHROW(a.close());
		CHECK_NOTHROW(a.assign(protocol, b.release()));
	}

	SECTION("open")
	{
		socket_type socket;

		std::error_code error;
		socket.open(protocol, error);
		CHECK(!error);
		CHECK(socket.is_open());

		socket.open(protocol, error);
		CHECK(error == pal::net::socket_errc::already_open);
		CHECK(socket.is_open());

		CHECK_THROWS_AS(
			socket.open(protocol),
			std::system_error
		);
	}

	SECTION("close")
	{
		std::error_code error;
		socket_type socket(protocol);
		REQUIRE(socket.is_open());

		socket.close(error);
		CHECK(!error);

		socket.open(protocol);
		CHECK_NOTHROW(socket.close());

		socket.close(error);
		CHECK(error == std::errc::bad_file_descriptor);

		CHECK_THROWS_AS(
			socket.close(),
			std::system_error
		);
	}

	// following tests require opened socket
	std::error_code error;
	socket_type socket(protocol);
	REQUIRE(socket.is_open());

	SECTION("bind")
	{
		socket.bind(bind_endpoint, error);
		REQUIRE(!error);
		CHECK(socket.local_endpoint() == bind_endpoint);

		socket.bind(bind_endpoint, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			socket.bind(bind_endpoint),
			std::system_error
		);

		socket.close();
		socket.open(protocol);
		CHECK_NOTHROW(socket.bind(bind_endpoint));
	}

	SECTION("connect")
	{
		SECTION("no listener")
		{
			socket.connect(connect_endpoint, error);
			if constexpr (pal_test::is_tcp_v<protocol_type>)
			{
				CHECK(error == std::errc::connection_refused);
				CHECK_THROWS_AS(
					socket.connect(connect_endpoint),
					std::system_error
				);
			}
			else if (pal_test::is_udp_v<protocol_type>)
			{
				CHECK(!error);
				CHECK_NOTHROW(socket.connect(connect_endpoint));
				CHECK(socket.remote_endpoint() == connect_endpoint);
			}
		}

		SECTION("closed")
		{
			socket.close();
			socket.connect(connect_endpoint, error);
			if constexpr (pal_test::is_tcp_v<protocol_type>)
			{
				CHECK(error == std::errc::connection_refused);
				CHECK_THROWS_AS(
					socket.connect(connect_endpoint),
					std::system_error
				);
			}
			else if (pal_test::is_udp_v<protocol_type>)
			{
				CHECK(!error);
				CHECK_NOTHROW(socket.connect(connect_endpoint));
				CHECK(socket.remote_endpoint() == connect_endpoint);
			}
		}
	}

	SECTION("wait")
	{
		using namespace std::chrono_literals;
		constexpr auto none = pal::net::socket_base::wait_type{};

		SECTION("wait_for")
		{
			if constexpr (pal_test::is_tcp_v<protocol_type>)
			{
				CHECK(socket.wait_for(socket.wait_read, 0ms, error) == none);
				CHECK_NOTHROW(socket.wait_for(socket.wait_read, 0ms));
			}
			else if (pal_test::is_udp_v<protocol_type>)
			{
				CHECK(socket.wait_for(socket.wait_write, 0ms, error) == socket.wait_write);
				CHECK_NOTHROW(socket.wait_for(socket.wait_write, 0ms));
			}
			CHECK(!error);
		}

		SECTION("closed")
		{
			socket.close();

			socket.wait(socket.wait_read, error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				socket.wait(socket.wait_write),
				std::system_error
			);
		}
	}

	SECTION("shutdown")
	{
		auto what = GENERATE(
			pal::net::socket_base::shutdown_receive,
			pal::net::socket_base::shutdown_send,
			pal::net::socket_base::shutdown_both
		);

		SECTION("not connected")
		{
			if constexpr (pal_test::is_tcp_v<protocol_type>)
			{
				socket.shutdown(what, error);
				CHECK(error == std::errc::not_connected);
				CHECK_THROWS_AS(
					socket.shutdown(what),
					std::system_error
				);
			}
		}

		SECTION("closed")
		{
			socket.close();
			socket.shutdown(what, error);
			CHECK(error == std::errc::bad_file_descriptor);
			CHECK_THROWS_AS(
				socket.shutdown(what),
				std::system_error
			);
		}
	}

	SECTION("local_endpoint")
	{
		SECTION("unbound")
		{
			auto ep = socket.local_endpoint(error);
			CHECK(!error);
			CHECK(ep == any);
			CHECK_NOTHROW(socket.local_endpoint());
		}

		SECTION("bound")
		{
			socket.bind(bind_endpoint, error);
			REQUIRE(!error);
			CHECK(socket.local_endpoint(error) == bind_endpoint);
			CHECK(!error);
			CHECK_NOTHROW(socket.local_endpoint());
		}

		SECTION("closed")
		{
			socket.close();
			socket.local_endpoint(error);
			CHECK(error == std::errc::bad_file_descriptor);
			CHECK_THROWS_AS(
				socket.local_endpoint(),
				std::system_error
			);
		}
	}

	SECTION("remote_endpoint")
	{
		SECTION("connected")
		{
			// tested at basic_stream_socket level, no here
		}

		SECTION("disconnected")
		{
			socket.remote_endpoint(error);
			CHECK(error == std::errc::not_connected);
			CHECK_THROWS_AS(
				socket.remote_endpoint(),
				std::system_error
			);
		}

		SECTION("closed")
		{
			socket.close();
			socket.remote_endpoint(error);
			CHECK(error == std::errc::bad_file_descriptor);
			CHECK_THROWS_AS(
				socket.remote_endpoint(),
				std::system_error
			);
		}
	}

	SECTION("available")
	{
		CHECK(socket.available(error) == 0);
		CHECK(!error);

		SECTION("closed")
		{
			socket.close();
			socket.available(error);
			CHECK(error == std::errc::bad_file_descriptor);
			CHECK_THROWS_AS(socket.available(), std::system_error);
		}
	}

	SECTION("native_non_blocking")
	{
		if constexpr (pal::is_windows_build)
		{
			socket.native_non_blocking(error);
			CHECK(error == std::errc::operation_not_supported);
			CHECK_THROWS_AS(socket.native_non_blocking(),
				std::system_error
			);

			socket.native_non_blocking(true, error);
			CHECK(!error);
			CHECK_NOTHROW(socket.native_non_blocking(true));

			socket.native_non_blocking(false, error);
			CHECK(!error);
			CHECK_NOTHROW(socket.native_non_blocking(false));
		}
		else
		{
			// default is off
			CHECK_FALSE(socket.native_non_blocking(error));
			CHECK(!error);
			CHECK_NOTHROW(socket.native_non_blocking());

			// turn on
			socket.native_non_blocking(true, error);
			CHECK(!error);
			CHECK_NOTHROW(socket.native_non_blocking(true));

			// check it is on
			CHECK(socket.native_non_blocking(error));
			CHECK(!error);
			CHECK_NOTHROW(socket.native_non_blocking());

			// turn off
			socket.native_non_blocking(false, error);
			CHECK(!error);
			CHECK_NOTHROW(socket.native_non_blocking(false));

			// check it is off
			CHECK_FALSE(socket.native_non_blocking(error));
			CHECK(!error);
			CHECK_NOTHROW(socket.native_non_blocking());
		}

		SECTION("closed")
		{
			socket.close();
			socket.native_non_blocking(error);
			if constexpr (pal::is_windows_build)
			{
				CHECK(error == std::errc::operation_not_supported);
			}
			else
			{
				CHECK(error == std::errc::bad_file_descriptor);
			}

			CHECK_THROWS_AS(
				socket.native_non_blocking(),
				std::system_error
			);

			socket.native_non_blocking(true, error);
			CHECK(error == std::errc::bad_file_descriptor);
			CHECK_THROWS_AS(
				socket.native_non_blocking(true),
				std::system_error
			);
		}
	}
}


} // namespace