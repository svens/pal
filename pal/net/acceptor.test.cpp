#include <pal/net/basic_socket_acceptor>
#include <pal/net/test>


namespace {

using namespace pal_test;


TEMPLATE_TEST_CASE("net/acceptor", "", tcp_v4, tcp_v6)
{
	constexpr auto protocol = TestType::protocol();
	using protocol_type = decltype(protocol);
	using acceptor_type = typename protocol_type::acceptor;
	using socket_type = typename protocol_type::socket;
	using endpoint_type = typename socket_type::endpoint_type;

	const endpoint_type
		any{protocol, 0},
		bind_endpoint{protocol, 3478};

	SECTION("ctor")
	{
		acceptor_type acceptor;
		CHECK_FALSE(acceptor.is_open());
		CHECK(acceptor.native_handle() == acceptor_type::invalid);
	}

	SECTION("ctor(protocol_type)")
	{
		acceptor_type acceptor(protocol);
		CHECK(acceptor.is_open());
		CHECK(acceptor.enable_connection_aborted() == false);
	}

	SECTION("ctor(endpoint_type, true)")
	{
		acceptor_type acceptor(bind_endpoint);
		CHECK(acceptor.is_open());
		CHECK(acceptor.local_endpoint() == bind_endpoint);
		CHECK(acceptor.enable_connection_aborted() == false);

		pal::net::socket_base::reuse_address reuse_address;
		acceptor.get_option(reuse_address);
		CHECK(static_cast<bool>(reuse_address) == true);
	}

	SECTION("ctor(endpoint_type, false)")
	{
		acceptor_type acceptor(bind_endpoint, false);
		CHECK_THROWS_AS(
			(acceptor_type{bind_endpoint, false}),
			std::system_error
		);
	}

	SECTION("ctor(acceptor&&)")
	{
		acceptor_type a(protocol);
		REQUIRE(a.is_open());
		auto b(std::move(a));
		CHECK(b.is_open());
		CHECK_FALSE(a.is_open());
	}

	SECTION("ctor(native_handle_type)")
	{
		acceptor_type a(protocol);
		REQUIRE(a.is_open());
		CHECK(a.enable_connection_aborted() == false);

		acceptor_type b(protocol, a.release());
		CHECK(b.is_open());

		CHECK_THROWS_AS(
			(acceptor_type{protocol, pal::net::socket_base::invalid}),
			std::system_error
		);
	}

	SECTION("operator=(acceptor&&)")
	{
		acceptor_type a(protocol), b;
		REQUIRE(a.is_open());
		CHECK_FALSE(b.is_open());
		b = std::move(a);
		CHECK(b.is_open());
		CHECK_FALSE(a.is_open());
	}

	SECTION("assign / release")
	{
		// a closed, b opened
		acceptor_type a, b(protocol);
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
		acceptor_type acceptor;

		std::error_code error;
		acceptor.open(protocol, error);
		CHECK(!error);
		CHECK(acceptor.is_open());
		CHECK(acceptor.enable_connection_aborted() == false);

		acceptor.open(protocol, error);
		CHECK(error == pal::net::socket_errc::already_open);
		CHECK(acceptor.is_open());

		CHECK_THROWS_AS(
			acceptor.open(protocol),
			std::system_error
		);
	}

	SECTION("close")
	{
		std::error_code error;
		acceptor_type acceptor(protocol);
		REQUIRE(acceptor.is_open());

		acceptor.close(error);
		CHECK(!error);
		CHECK_FALSE(acceptor.is_open());

		acceptor.open(protocol);
		CHECK_NOTHROW(acceptor.close());

		acceptor.close(error);
		CHECK(error == std::errc::bad_file_descriptor);

		CHECK_THROWS_AS(
			acceptor.close(),
			std::system_error
		);
	}

	// all following tests require opened acceptor
	std::error_code error;
	acceptor_type acceptor(protocol);
	REQUIRE(acceptor.is_open());

	SECTION("bind")
	{
		acceptor.bind(bind_endpoint, error);
		REQUIRE(!error);
		CHECK(acceptor.local_endpoint() == bind_endpoint);

		acceptor.bind(bind_endpoint, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			acceptor.bind(bind_endpoint),
			std::system_error
		);

		acceptor.close();
		acceptor.open(protocol);
		CHECK_NOTHROW(acceptor.bind(bind_endpoint));
	}

	SECTION("listen")
	{
		// Windows requires bound socket for listen()
		// Linux & MacOS bind implicitly to random port

		if constexpr (pal::is_windows_build)
		{
			acceptor.bind(bind_endpoint);
		}
		else
		{
			CHECK(acceptor.local_endpoint() == any);
		}

		acceptor.listen(3, error);
		CAPTURE(error);
		REQUIRE(!error);

		CHECK(acceptor.local_endpoint() != any);

		CHECK_NOTHROW(acceptor.listen());

		SECTION("closed")
		{
			acceptor.close();
			acceptor.listen(1, error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				acceptor.listen(),
				std::system_error
			);
		}
	}

	SECTION("native_non_blocking")
	{
		if constexpr (pal::is_windows_build)
		{
			acceptor.native_non_blocking(error);
			CHECK(error == std::errc::operation_not_supported);
			CHECK_THROWS_AS(
				acceptor.native_non_blocking(),
				std::system_error
			);

			acceptor.native_non_blocking(true, error);
			CHECK(!error);
			CHECK_NOTHROW(acceptor.native_non_blocking(true));
		}
		else
		{
			// default is off
			CHECK_FALSE(acceptor.native_non_blocking(error));
			CHECK(!error);
			CHECK_NOTHROW(acceptor.native_non_blocking());

			// turn on
			acceptor.native_non_blocking(true, error);
			CHECK(!error);
			CHECK_NOTHROW(acceptor.native_non_blocking(true));

			// check it is on
			CHECK(acceptor.native_non_blocking(error));
			CHECK(!error);
			CHECK_NOTHROW(acceptor.native_non_blocking());

			// turn off
			acceptor.native_non_blocking(false, error);
			CHECK(!error);
			CHECK_NOTHROW(acceptor.native_non_blocking(false));

			// check it is off
			CHECK_FALSE(acceptor.native_non_blocking(error));
			CHECK(!error);
			CHECK_NOTHROW(acceptor.native_non_blocking());
		}

		SECTION("closed")
		{
			acceptor.close();
			acceptor.native_non_blocking(error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				acceptor.native_non_blocking(),
				std::system_error
			);

			acceptor.native_non_blocking(true, error);
			CHECK(error == std::errc::bad_file_descriptor);
			CHECK_THROWS_AS(
				acceptor.native_non_blocking(true),
				std::system_error
			);
		}
	}

	SECTION("local_endpoint")
	{
		SECTION("unbound")
		{
			auto ep = acceptor.local_endpoint(error);
			CHECK(!error);
			CHECK(ep == any);
			CHECK_NOTHROW(acceptor.local_endpoint());
		}

		SECTION("bound")
		{
			acceptor.bind(bind_endpoint, error);
			REQUIRE(!error);
			CHECK(acceptor.local_endpoint(error) == bind_endpoint);
			CHECK(!error);
			CHECK_NOTHROW(acceptor.local_endpoint());
		}

		SECTION("closed")
		{
			acceptor.close();
			acceptor.local_endpoint(error);
			CHECK(error == std::errc::bad_file_descriptor);
			CHECK_THROWS_AS(
				acceptor.local_endpoint(),
				std::system_error
			);
		}
	}

	SECTION("accept")
	{
		// TODO
	}

	SECTION("enable_connection_aborted")
	{
		// TODO
	}

	SECTION("wait")
	{
		// TODO
	}
}


} // namespace
