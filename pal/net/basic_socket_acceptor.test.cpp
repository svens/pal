#include <pal/net/basic_socket_acceptor>
#include <pal/net/test>
#include <catch2/catch_template_test_macros.hpp>

namespace {

using namespace pal_test;

TEMPLATE_TEST_CASE("net/basic_socket_acceptor", "[!nonportable]",
	tcp_v4,
	tcp_v6,
	tcp_v6_only)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = typename protocol_t::endpoint;

	auto a = TestType::make_acceptor().value();
	REQUIRE(a);
	CHECK(a.native_socket()->handle != pal::net::native_socket_handle::invalid);
	CHECK(a.protocol() == TestType::protocol_v);

	SECTION("move ctor")
	{
		auto a1 = std::move(a);
		REQUIRE(a1);
		CHECK(a1.native_socket()->handle != pal::net::native_socket_handle::invalid);
		CHECK(a1.protocol() == TestType::protocol_v);
		CHECK(!a);
	}

	SECTION("move assign")
	{
		auto s_orig_handle = a.native_socket()->handle;
		auto a1 = TestType::make_acceptor().value();
		a1 = std::move(a);
		CHECK(!a);
		CHECK(a1.native_socket()->handle == s_orig_handle);
	}

	SECTION("release")
	{
		auto s_orig_handle = a.native_socket()->handle;
		auto native = a.release();
		CHECK(!a);
		CHECK(native->handle == s_orig_handle);
	}

	SECTION("bind")
	{
		endpoint_t endpoint{TestType::loopback_v, 0};
		REQUIRE(bind_next_available_port(a, endpoint));
		CHECK(a.local_endpoint().value() == endpoint);

		SECTION("address in use")
		{
			auto a1 = TestType::make_acceptor().value();
			auto bind = a1.bind(endpoint);
			REQUIRE_FALSE(bind);
			CHECK(bind.error() == std::errc::address_in_use);
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(a);
			auto bind = a.bind(endpoint);
			REQUIRE_FALSE(bind);
			CHECK(bind.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("listen")
	{
		SECTION("success")
		{
			endpoint_t endpoint{TestType::loopback_v, 0};
			REQUIRE(bind_next_available_port(a, endpoint));
			REQUIRE_NOTHROW(a.listen().value());
		}

		SECTION("unbound")
		{
			auto local_endpoint = a.local_endpoint().value();
			CHECK(local_endpoint.port() == 0);

			REQUIRE_NOTHROW(a.listen().value());
			local_endpoint = a.local_endpoint().value();
			CHECK(local_endpoint.port() != 0);
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(a);
			auto listen = a.listen();
			REQUIRE_FALSE(listen);
			CHECK(listen.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("accept")
	{
		endpoint_t endpoint{TestType::loopback_v, 0};
		REQUIRE(bind_next_available_port(a, endpoint));
		REQUIRE_NOTHROW(a.listen().value());

		SECTION("success")
		{
			auto s1 = TestType::make_socket().value();
			REQUIRE_NOTHROW(s1.connect(endpoint).value());
			CHECK(s1.remote_endpoint().value() == endpoint);

			auto s2 = a.accept().value();
			CHECK(s2.local_endpoint().value() == endpoint);
		}

		SECTION("no connect")
		{
			REQUIRE_NOTHROW(a.set_option(pal::net::non_blocking_io{true}).value());
			auto accept = a.accept();
			REQUIRE_FALSE(accept);
			CHECK(accept.error() == std::errc::operation_would_block);
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(a);
			auto accept = a.accept();
			REQUIRE_FALSE(accept);
			CHECK(accept.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("accept with endpoint")
	{
		endpoint_t endpoint{TestType::loopback_v, 0};
		REQUIRE(bind_next_available_port(a, endpoint));
		REQUIRE_NOTHROW(a.listen().value());

		SECTION("success")
		{
			auto s1 = TestType::make_socket().value();
			REQUIRE_NOTHROW(s1.connect(endpoint).value());
			CHECK(s1.remote_endpoint().value() == endpoint);

			endpoint_t s2_endpoint;
			auto s2 = a.accept(s2_endpoint).value();
			CHECK(s2.local_endpoint().value() == endpoint);
			CHECK(s2.remote_endpoint().value() == s2_endpoint);
			CHECK(s1.local_endpoint().value() == s2_endpoint);
		}

		SECTION("no connect")
		{
			REQUIRE_NOTHROW(a.set_option(pal::net::non_blocking_io{true}).value());
			auto accept = a.accept(endpoint);
			REQUIRE_FALSE(accept);
			CHECK(accept.error() == std::errc::operation_would_block);
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(a);
			auto accept = a.accept(endpoint);
			REQUIRE_FALSE(accept);
			CHECK(accept.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("local_endpoint")
	{
		// see SECTION("bind")
		SUCCEED();

		SECTION("unbound")
		{
			auto local_endpoint = a.local_endpoint().value();
			CHECK(has_expected_family<TestType>(local_endpoint.address()));
			CHECK(local_endpoint.address().is_unspecified());
			CHECK(local_endpoint.port() == 0);
		}

		SECTION("bad file descriptor")
		{
			close_native_handle(a);
			auto local_endpoint = a.local_endpoint();
			REQUIRE_FALSE(local_endpoint);
			CHECK(local_endpoint.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("reuse address")
	{
		pal::net::reuse_address reuse_address{true};
		REQUIRE_NOTHROW(a.get_option(reuse_address).value());
		CHECK_FALSE(reuse_address);

		reuse_address = true;
		REQUIRE_NOTHROW(a.set_option(reuse_address).value());

		REQUIRE_NOTHROW(a.get_option(reuse_address).value());
		CHECK(reuse_address);

		SECTION("bad file descriptor")
		{
			close_native_handle(a);
			auto opt = a.get_option(reuse_address);
			REQUIRE_FALSE(opt);
			CHECK(opt.error() == std::errc::bad_file_descriptor);

			opt = a.set_option(reuse_address);
			REQUIRE_FALSE(opt);
			CHECK(opt.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("reuse port")
	{
		pal::net::reuse_port reuse_port{true};

		if constexpr (pal::os == pal::os_type::windows)
		{
			auto opt = a.get_option(reuse_port);
			REQUIRE_FALSE(opt);
			CHECK(opt.error() == std::errc::no_protocol_option);

			opt = a.set_option(reuse_port);
			REQUIRE_FALSE(opt);
			CHECK(opt.error() == std::errc::no_protocol_option);
		}
		else
		{
			REQUIRE_NOTHROW(a.get_option(reuse_port).value());
			CHECK_FALSE(reuse_port);

			reuse_port = true;
			REQUIRE_NOTHROW(a.set_option(reuse_port).value());

			REQUIRE_NOTHROW(a.get_option(reuse_port).value());
			CHECK(reuse_port);

			SECTION("bad file descriptor")
			{
				close_native_handle(a);
				auto opt = a.get_option(reuse_port);
				REQUIRE_FALSE(opt);
				CHECK(opt.error() == std::errc::bad_file_descriptor);

				opt = a.set_option(reuse_port);
				REQUIRE_FALSE(opt);
				CHECK(opt.error() == std::errc::bad_file_descriptor);
			}
		}
	}
}

TEMPLATE_TEST_CASE("net/basic_socket_acceptor", "[!nonportable]",
	invalid_protocol)
{
	SECTION("make_socket_acceptor")
	{
		auto a = pal::net::make_socket_acceptor(TestType());
		REQUIRE_FALSE(a);
		CHECK(a.error() == std::errc::protocol_not_supported);
	}
}

} // namespace
