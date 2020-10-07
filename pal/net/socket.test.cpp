#include <pal/net/basic_socket>
#include <pal/net/ip/tcp>
#include <pal/net/ip/udp>
#include <pal/net/test>


namespace {


struct tcp_v4
{
	using address_type = pal::net::ip::address_v4;

	static constexpr auto protocol () noexcept
	{
		return pal::net::ip::tcp::v4();
	}
};


struct tcp_v6
{
	using address_type = pal::net::ip::address_v6;

	static constexpr auto protocol () noexcept
	{
		return pal::net::ip::tcp::v6();
	}
};


struct udp_v4
{
	using address_type = pal::net::ip::address_v4;

	static constexpr auto protocol () noexcept
	{
		return pal::net::ip::udp::v4();
	}
};


struct udp_v6
{
	using address_type = pal::net::ip::address_v6;

	static constexpr auto protocol () noexcept
	{
		return pal::net::ip::udp::v6();
	}
};


TEMPLATE_TEST_CASE("net/socket", "", tcp_v4, tcp_v6, udp_v4, udp_v6)
{
	using protocol_type = decltype(TestType::protocol());
	using socket_type = typename protocol_type::socket;

	using endpoint_type = typename socket_type::endpoint_type;
	const endpoint_type
		any{TestType::protocol(), 0},
		bind_endpoint{TestType::protocol(), 3478},
		connect_endpoint{TestType::address_type::loopback(), 3478};

	SECTION("ctor")
	{
		socket_type socket;
		CHECK_FALSE(socket.is_open());
		CHECK(socket.native_handle() == socket_type::invalid);
	}

	SECTION("ctor(socket&&)")
	{
		socket_type a(TestType::protocol());
		REQUIRE(a.is_open());
		auto b(std::move(a));
		CHECK(b.is_open());
		CHECK_FALSE(a.is_open());
	}

	SECTION("ctor(native_handle_type)")
	{
		socket_type a(TestType::protocol());
		REQUIRE(a.is_open());

		socket_type b(a.native_handle());
		CHECK(b.is_open());

		// b is owner now
		a.release();
	}

	SECTION("operator=(socket&&)")
	{
		socket_type a(TestType::protocol()), b;
		REQUIRE(a.is_open());
		CHECK_FALSE(b.is_open());
		b = std::move(a);
		CHECK(b.is_open());
		CHECK_FALSE(a.is_open());
	}

	SECTION("assign / release")
	{
		// a closed, b opened
		socket_type a, b(TestType::protocol());;
		CHECK(!a.is_open());
		REQUIRE(b.is_open());

		// a <- b
		std::error_code error;
		a.assign(b.release(), error);
		REQUIRE(!error);

		// a <- invalid
		a.assign(pal::net::socket_base::invalid, error);
		CHECK(error == std::errc::bad_file_descriptor);

		// a <- invalid
		CHECK_THROWS_AS(
			a.assign(pal::net::socket_base::invalid),
			std::system_error
		);

		// a <- reopened b
		b.open(TestType::protocol());
		a.assign(b.native_handle(), error);
		CHECK(error == pal::net::socket_errc::already_open);

		// a <- reopened b
		CHECK_THROWS_AS(
			a.assign(b.native_handle()),
			std::system_error
		);

		// close a, a <- b
		CHECK_NOTHROW(a.close());
		CHECK_NOTHROW(a.assign(b.release()));
	}

	SECTION("open")
	{
		socket_type socket;

		std::error_code error;
		socket.open(TestType::protocol(), error);
		CHECK(!error);
		CHECK(socket.is_open());

		socket.open(TestType::protocol(), error);
		CHECK(error == pal::net::socket_errc::already_open);
		CHECK(socket.is_open());

		CHECK_THROWS_AS(
			socket.open(TestType::protocol()),
			std::system_error
		);
	}

	SECTION("close")
	{
		std::error_code error;
		socket_type socket(TestType::protocol());
		REQUIRE(socket.is_open());

		socket.close(error);
		CHECK(!error);

		socket.open(TestType::protocol());
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
	socket_type socket(TestType::protocol());
	REQUIRE(!error);
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
		socket.open(TestType::protocol());
		CHECK_NOTHROW(socket.bind(bind_endpoint));
	}

	SECTION("connect")
	{
		SECTION("no listener")
		{
			socket.connect(connect_endpoint, error);
			if constexpr (std::is_same_v<protocol_type, pal::net::ip::tcp>)
			{
				CHECK(error == std::errc::connection_refused);
				CHECK_THROWS_AS(
					socket.connect(connect_endpoint),
					std::system_error
				);
			}
			else if (std::is_same_v<protocol_type, pal::net::ip::udp>)
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
			if constexpr (std::is_same_v<protocol_type, pal::net::ip::tcp>)
			{
				CHECK(error == std::errc::connection_refused);
				CHECK_THROWS_AS(
					socket.connect(connect_endpoint),
					std::system_error
				);
			}
			else if (std::is_same_v<protocol_type, pal::net::ip::udp>)
			{
				CHECK(!error);
				CHECK_NOTHROW(socket.connect(connect_endpoint));
				CHECK(socket.remote_endpoint() == connect_endpoint);
			}
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
}


} // namespace
