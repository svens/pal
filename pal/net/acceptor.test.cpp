#include <pal/net/basic_socket_acceptor>
#include <pal/net/test>


namespace {

using namespace pal_test;
using namespace std::chrono_literals;


TEMPLATE_TEST_CASE("net/acceptor", "", tcp_v4, tcp_v6)
{
	constexpr auto protocol = protocol_v<TestType>;
	const endpoint_t<TestType> any{protocol, 0};

	auto bind_endpoint = next_endpoint(protocol);
	CAPTURE(bind_endpoint);

	SECTION("ctor")
	{
		acceptor_t<TestType> acceptor;
		CHECK_FALSE(acceptor.is_open());
		CHECK(acceptor.native_handle() == acceptor_t<TestType>::invalid);
	}

	SECTION("ctor(protocol)")
	{
		acceptor_t<TestType> acceptor(protocol);
		CHECK(acceptor.is_open());
		CHECK(acceptor.enable_connection_aborted() == false);
	}

	SECTION("ctor(endpoint, true)")
	{
		acceptor_t<TestType> acceptor(bind_endpoint);
		CHECK(acceptor.is_open());
		CHECK(acceptor.local_endpoint() == bind_endpoint);
		CHECK(acceptor.enable_connection_aborted() == false);

		pal::net::socket_base::reuse_address reuse_address;
		acceptor.get_option(reuse_address);
		CHECK(static_cast<bool>(reuse_address) == true);
	}

	SECTION("ctor(endpoint, false)")
	{
		acceptor_t<TestType> acceptor(bind_endpoint, false);
		CHECK_THROWS_AS(
			(acceptor_t<TestType>{bind_endpoint, false}),
			std::system_error
		);
	}

	SECTION("ctor(acceptor&&)")
	{
		acceptor_t<TestType> a(protocol);
		REQUIRE(a.is_open());
		auto b(std::move(a));
		CHECK(b.is_open());
		CHECK_FALSE(a.is_open());
	}

	SECTION("ctor(native_handle)")
	{
		acceptor_t<TestType> a(protocol);
		REQUIRE(a.is_open());
		CHECK(a.enable_connection_aborted() == false);

		acceptor_t<TestType> b(protocol, a.release());
		CHECK(b.is_open());

		CHECK_THROWS_AS(
			(acceptor_t<TestType>{protocol, pal::net::socket_base::invalid}),
			std::system_error
		);
	}

	SECTION("operator=(acceptor&&)")
	{
		acceptor_t<TestType> a(protocol), b;
		REQUIRE(a.is_open());
		CHECK_FALSE(b.is_open());
		b = std::move(a);
		CHECK(b.is_open());
		CHECK_FALSE(a.is_open());
	}

	SECTION("assign / release")
	{
		// a closed, b opened
		acceptor_t<TestType> a, b(protocol);
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
		acceptor_t<TestType> acceptor;

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
		acceptor_t<TestType> acceptor(protocol);
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
	acceptor_t<TestType> acceptor(protocol);
	REQUIRE(acceptor.is_open());

	auto start_listen = [](auto &acceptor, auto &endpoint) -> auto
	{
		auto connect_endpoint = bind_available_port(acceptor, endpoint);
		acceptor.listen();
		return connect_endpoint;
	};

	SECTION("bind")
	{
		REQUIRE_NOTHROW(bind_available_port(acceptor, bind_endpoint));
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
		REQUIRE_NOTHROW(bind_available_port(acceptor, bind_endpoint));

		acceptor.listen(3, error);
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

	SECTION("get_option / set_option")
	{
		pal::net::socket_base::reuse_address reuse_address;
		acceptor.get_option(reuse_address);
		CHECK(!reuse_address);

		reuse_address = true;
		acceptor.set_option(reuse_address);

		reuse_address = false;
		acceptor.get_option(reuse_address);
		CHECK(static_cast<bool>(reuse_address));

		SECTION("closed")
		{
			acceptor.close();
			CHECK_THROWS_AS(
				acceptor.get_option(reuse_address),
				std::system_error
			);
			CHECK_THROWS_AS(
				acceptor.set_option(reuse_address),
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
			REQUIRE_NOTHROW(bind_available_port(acceptor, bind_endpoint));
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
		socket_t<TestType> a, b;

		a.connect(start_listen(acceptor, bind_endpoint));

		SECTION("accept(error)")
		{
			b = acceptor.accept(error);
			CHECK(!error);
		}

		SECTION("accept()")
		{
			CHECK_NOTHROW((b = acceptor.accept()));
		}

		SECTION("accept(endpoint, error)")
		{
			endpoint_t<TestType> endpoint;
			b = acceptor.accept(endpoint, error);
			CHECK(!error);
			CHECK(endpoint == b.remote_endpoint());
		}

		SECTION("accept(endpoint)")
		{
			endpoint_t<TestType> endpoint;
			CHECK_NOTHROW((b = acceptor.accept(endpoint)));
			CHECK(endpoint == b.remote_endpoint());
		}

		CHECK(b.is_open());
		CHECK(b.remote_endpoint() == a.local_endpoint());
		CHECK(b.local_endpoint() == a.remote_endpoint());
	}

	SECTION("accept")
	{
		SECTION("no connect")
		{
			acceptor.native_non_blocking(true);
			start_listen(acceptor, bind_endpoint);
			acceptor.accept(error);
			CHECK(error == std::errc::operation_would_block);
		}

		SECTION("closed")
		{
			acceptor.close();

			acceptor.accept(error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				acceptor.accept(),
				std::system_error
			);

			endpoint_t<TestType> endpoint;
			acceptor.accept(endpoint, error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				acceptor.accept(endpoint),
				std::system_error
			);
		}
	}

	SECTION("enable_connection_aborted")
	{
		CHECK_FALSE(acceptor.enable_connection_aborted());
		acceptor.enable_connection_aborted(true);
		CHECK(acceptor.enable_connection_aborted());

		socket_t<TestType> a;
		a.connect(start_listen(acceptor, bind_endpoint));
		a.set_option(pal::net::socket_base::linger(true, 0s));
		a.close();

		// XXX: expect ECONNABORTED with RST but does not happen
		auto b = acceptor.accept(error);
		CHECK(!error);
	}

	SECTION("wait")
	{
		acceptor.native_non_blocking(true);
		auto connect_endpoint = start_listen(acceptor, bind_endpoint);

		socket_t<TestType> a;

		SECTION("std::error_code")
		{
			CHECK_FALSE(acceptor.wait_for(acceptor.wait_read, 0s, error));
			REQUIRE(!error);

			a.connect(connect_endpoint);

			acceptor.wait(acceptor.wait_read, error);
			REQUIRE(!error);

			CHECK(acceptor.wait_for(acceptor.wait_read, 0s, error));
			REQUIRE(!error);
		}

		SECTION("std::system_exception")
		{
			bool readable = false;
			CHECK_NOTHROW(readable = acceptor.wait_for(acceptor.wait_read, 0s));
			CHECK_FALSE(readable);

			a.connect(connect_endpoint);

			CHECK_NOTHROW(acceptor.wait(acceptor.wait_read));

			CHECK_NOTHROW(readable = acceptor.wait_for(acceptor.wait_read, 0s));
			CHECK(readable);
		}

		SECTION("closed")
		{
			acceptor.close();

			acceptor.wait(acceptor.wait_read, error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				acceptor.wait(acceptor.wait_read),
				std::system_error
			);

			acceptor.wait_for(acceptor.wait_read, 0s, error);
			CHECK(error == std::errc::bad_file_descriptor);

			CHECK_THROWS_AS(
				acceptor.wait_for(acceptor.wait_read, 0s),
				std::system_error
			);
		}

		// extract pending connection (if any), ignoring errors
		acceptor.accept(error);
		(void)error;
	}
}


} // namespace
