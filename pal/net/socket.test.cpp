#include <pal/net/basic_socket>
#include <pal/net/test>


namespace {

using namespace pal_test;
using namespace std::chrono_literals;


TEMPLATE_TEST_CASE("net/socket", "", tcp_v4, tcp_v6, udp_v4, udp_v6)
{
	constexpr auto protocol = protocol_v<TestType>;
	const endpoint_t<TestType> any{protocol, 0};
	auto [bind_endpoint, connect_endpoint] = test_endpoints(protocol);
	CAPTURE(bind_endpoint);

	SECTION("ctor")
	{
		socket_t<TestType> socket;
		CHECK_FALSE(socket.is_open());
		CHECK(socket.native_handle() == socket_t<TestType>::invalid);
	}

	SECTION("ctor(protocol)")
	{
		socket_t<TestType> socket(protocol);
		CHECK(socket.is_open());
	}

	SECTION("ctor(endpoint)")
	{
		socket_t<TestType> socket(bind_endpoint);
		CHECK(socket.is_open());
		CHECK(socket.local_endpoint() == bind_endpoint);

		CHECK_THROWS_AS(
			socket_t<TestType>{bind_endpoint},
			std::system_error
		);
	}

	SECTION("ctor(socket&&)")
	{
		socket_t<TestType> a(protocol);
		REQUIRE(a.is_open());
		auto b(std::move(a));
		CHECK(b.is_open());
		CHECK_FALSE(a.is_open());
	}

	SECTION("ctor(native_handle)")
	{
		socket_t<TestType> a(protocol);
		REQUIRE(a.is_open());

		socket_t<TestType> b(protocol, a.release());
		CHECK(b.is_open());

		CHECK_THROWS_AS(
			(socket_t<TestType>{protocol, pal::net::socket_base::invalid}),
			std::system_error
		);
	}

	SECTION("operator=(socket&&)")
	{
		socket_t<TestType> a(protocol), b;
		REQUIRE(a.is_open());
		CHECK_FALSE(b.is_open());
		b = std::move(a);
		CHECK(b.is_open());
		CHECK_FALSE(a.is_open());
	}

	SECTION("assign / release")
	{
		// a closed, b opened
		socket_t<TestType> a, b(protocol);
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
		socket_t<TestType> socket;

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
		socket_t<TestType> socket(protocol);
		REQUIRE(socket.is_open());

		socket.close(error);
		CHECK(!error);
		CHECK_FALSE(socket.is_open());

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
	socket_t<TestType> socket(protocol);
	REQUIRE(socket.is_open());

	// doing multiple bind(2)
	REQUIRE_NOTHROW(socket.set_option(pal::net::socket_base::reuse_address(true)));

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
		if constexpr (is_tcp_v<TestType>)
		{
			acceptor_t<TestType> acceptor(bind_endpoint);

			socket.connect(connect_endpoint, error);
			CHECK(!error);
			REQUIRE_NOTHROW((void)acceptor.accept());

			CHECK_NOTHROW(socket.close());
			CHECK_NOTHROW(socket.connect(connect_endpoint));
			REQUIRE_NOTHROW((void)acceptor.accept());
		}
		else if constexpr (is_udp_v<TestType>)
		{
			socket.connect(connect_endpoint, error);
			CHECK(!error);

			CHECK_NOTHROW(socket.close());
			CHECK_NOTHROW(socket.connect(connect_endpoint));
		}

		SECTION("no listener")
		{
			socket.close();
			socket.connect(connect_endpoint, error);
			if constexpr (is_tcp_v<TestType>)
			{
				CHECK(error == std::errc::connection_refused);
				CHECK_THROWS_AS(
					socket.connect(connect_endpoint),
					std::system_error
				);
			}
			else if (is_udp_v<TestType>)
			{
				CHECK(!error);
				CHECK_NOTHROW(socket.connect(connect_endpoint));
				CHECK(socket.remote_endpoint() == connect_endpoint);
			}
		}
	}

	SECTION("wait")
	{
		socket_t<TestType> a;
		if constexpr (is_tcp_v<TestType>)
		{
			acceptor_t<TestType> acceptor(bind_endpoint);
			socket.connect(connect_endpoint);
			a = acceptor.accept();
		}

		constexpr auto what = socket.wait_write;

		socket.wait(what, error);
		CHECK(!error);

		CHECK_NOTHROW(socket.wait(what));

		CHECK(socket.wait_for(what, 0s, error) == what);
		CHECK(!error);

		CHECK_NOTHROW(socket.wait_for(what, 0s) == what);

		SECTION("closed")
		{
			socket.close();

			socket.wait(socket.wait_write, error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				socket.wait(socket.wait_write),
				std::system_error
			);

			socket.wait_for(socket.wait_write, 0s, error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				socket.wait_for(socket.wait_write, 0s),
				std::system_error
			);
		}
	}

	SECTION("shutdown")
	{
		constexpr auto what = pal::net::socket_base::shutdown_both;

		SECTION("connected")
		{
			socket_t<TestType> a;
			if constexpr (is_tcp_v<TestType>)
			{
				acceptor_t<TestType> acceptor(bind_endpoint);
				socket.connect(connect_endpoint);
				a = acceptor.accept();
			}
			else if constexpr (is_udp_v<TestType>)
			{
				socket.connect(connect_endpoint);
			}

			SECTION("std::error_code")
			{
				socket.shutdown(what, error);
				CHECK(!error);
			}

			SECTION("std::system_error")
			{
				CHECK_NOTHROW(socket.shutdown(what));
			}
		}

		SECTION("not connected")
		{
			if constexpr (!pal::is_windows_build || is_tcp_v<TestType>)
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
			if constexpr (is_tcp_v<TestType>)
			{
				acceptor_t<TestType> acceptor(bind_endpoint);

				socket.connect(connect_endpoint, error);
				REQUIRE(!error);

				(void)acceptor.accept(error);
				REQUIRE(!error);
			}
			else if constexpr (is_udp_v<TestType>)
			{
				socket.connect(connect_endpoint, error);
				REQUIRE(!error);
			}

			auto ep = socket.remote_endpoint(error);
			REQUIRE(!error);
			CHECK(ep == connect_endpoint);

			CHECK_NOTHROW(socket.remote_endpoint());
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

		CHECK_NOTHROW(socket.available() == 0);

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
			CHECK_THROWS_AS(
				socket.native_non_blocking(),
				std::system_error
			);

			socket.native_non_blocking(true, error);
			CHECK(!error);
			CHECK_NOTHROW(socket.native_non_blocking(true));
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
			CHECK(error == std::errc::bad_file_descriptor);

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
