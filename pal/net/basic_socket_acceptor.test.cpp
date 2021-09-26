#include <pal/net/basic_socket_acceptor>
#include <pal/net/test>


namespace {


using namespace pal_test;


TEMPLATE_TEST_CASE("net/basic_socket_acceptor", "",
	tcp_v4,
	tcp_v6,
	tcp_v6_only)
{
	using protocol_t = std::remove_cvref_t<decltype(TestType::protocol_v)>;
	using endpoint_t = typename protocol_t::endpoint;

	typename protocol_t::acceptor a;
	CHECK_FALSE(a.is_open());

	auto r = TestType::make_acceptor();
	REQUIRE(r);
	a = std::move(*r);
	REQUIRE(a.is_open());
	CHECK(a.native_handle() != -1);
	CHECK(a.protocol() == TestType::protocol_v);

	SECTION("move ctor")
	{
		auto a1 = std::move(a);
		CHECK(a1.is_open());
		CHECK(a1.protocol() == TestType::protocol_v);
		CHECK_FALSE(a.is_open());
	}

	SECTION("move assign")
	{
		std::decay_t<decltype(a)> a1;
		a1 = std::move(a);
		CHECK(a1.is_open());
		CHECK(a1.protocol() == TestType::protocol_v);
		CHECK_FALSE(a.is_open());
	}

	SECTION("release")
	{
		pal_test::handle_guard guard{a.release()};
		CHECK_FALSE(a.is_open());
	}

	SECTION("open")
	{
		pal_test::handle_guard guard{a.release()};
		REQUIRE_FALSE(a.is_open());
		REQUIRE(a.open(TestType::protocol_v));
		CHECK(a.is_open());
	}

	SECTION("open already opened")
	{
		if constexpr (!pal::assert_noexcept)
		{
			REQUIRE_THROWS_AS(a.open(TestType::protocol_v), std::logic_error);
		}
		else
		{
			// although will succeed in release build, it is still runtime
			// penalty for compile-time logic error
			auto handle = a.native_handle();
			REQUIRE(a.open(TestType::protocol_v));
			CHECK(handle != a.native_handle());
		}
	}

	SECTION("assign opened")
	{
		auto new_handle = TestType::make_acceptor().value().release();
		auto old_handle = a.native_handle();
		REQUIRE(a.assign(TestType::protocol_v, new_handle));
		CHECK(a.native_handle() == new_handle);
		CHECK(a.native_handle() != old_handle);
		CHECK(a.protocol() == TestType::protocol_v);
	}

	SECTION("assign closed")
	{
		auto new_handle = TestType::make_acceptor().value().release();
		pal_test::handle_guard guard{a.release()};
		REQUIRE(a.assign(TestType::protocol_v, new_handle));
		CHECK(a.native_handle() == new_handle);
		CHECK(a.native_handle() != guard.handle);
		CHECK(a.protocol() == TestType::protocol_v);
	}

	SECTION("assign invalid")
	{
		REQUIRE(a.assign(TestType::protocol_v, pal::net::__bits::invalid_native_socket));
		REQUIRE_FALSE(a.is_open());
	}

	SECTION("assign invalid to closed")
	{
		REQUIRE(a.close());
		REQUIRE(a.assign(TestType::protocol_v, pal::net::__bits::invalid_native_socket));
		REQUIRE_FALSE(a.is_open());
	}

	SECTION("assign bad_alloc")
	{
		pal_test::handle_guard guard{a.release()};
		pal_test::bad_alloc_once x;
		auto assign = a.assign(TestType::protocol_v, guard.handle);
		REQUIRE_FALSE(assign);
		CHECK(assign.error() == std::errc::not_enough_memory);
	}

	SECTION("close")
	{
		a.close();
		CHECK_FALSE(a.is_open());
	}

	SECTION("close invalidated")
	{
		pal_test::handle_guard{a.native_handle()};
		auto close = a.close();
		REQUIRE_FALSE(close);
		CHECK(close.error() == std::errc::bad_file_descriptor);
	}

	SECTION("close already closed")
	{
		a.close();
		CHECK_FALSE(a.is_open());

		if constexpr (!pal::assert_noexcept)
		{
			REQUIRE_THROWS_AS(a.close(), std::logic_error);
		}
	}

	SECTION("make bad_alloc")
	{
		pal_test::handle_guard guard{a.release()};
		pal_test::bad_alloc_once x;
		r = TestType::make_acceptor();
		REQUIRE_FALSE(r);
		CHECK(r.error() == std::errc::not_enough_memory);
	}

	SECTION("local_endpoint")
	{
		// see SECTION("bind")
		SUCCEED();

		SECTION("unbound")
		{
			auto local_endpoint = a.local_endpoint();
			REQUIRE(local_endpoint);
			CHECK(local_endpoint->address().is_v4() == pal_test::is_v4_v<TestType>);
			CHECK(local_endpoint->address().is_unspecified());
			CHECK(local_endpoint->port() == 0);
		}

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{a.native_handle()};
			auto local_endpoint = a.local_endpoint();
			REQUIRE_FALSE(local_endpoint);
			CHECK(local_endpoint.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("bind")
	{
		endpoint_t endpoint{TestType::loopback_v, 0};
		REQUIRE(pal_test::bind_next_available_port(a, endpoint));
		CHECK(a.local_endpoint().value() == endpoint);

		SECTION("address in use")
		{
			r = TestType::make_acceptor();
			REQUIRE(r);
			auto bind = r->bind(endpoint);
			REQUIRE_FALSE(bind);
			CHECK(bind.error() == std::errc::address_in_use);
		}

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{a.native_handle()};
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
			REQUIRE(pal_test::bind_next_available_port(a, endpoint));
			CHECK(a.listen());
		}

		SECTION("unbound")
		{
			auto local_endpoint = a.local_endpoint();
			REQUIRE(local_endpoint);
			CHECK(local_endpoint->port() == 0);

			REQUIRE(a.listen());
			local_endpoint = a.local_endpoint();
			REQUIRE(local_endpoint);
			CHECK(local_endpoint->port() != 0);
		}

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{a.native_handle()};
			auto listen = a.listen();
			REQUIRE_FALSE(listen);
			CHECK(listen.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("accept")
	{
		endpoint_t endpoint{TestType::loopback_v, 0};
		REQUIRE(pal_test::bind_next_available_port(a, endpoint));
		REQUIRE(a.listen());

		SECTION("success")
		{
			auto s1 = TestType::make_socket().value();
			REQUIRE(s1.connect(endpoint));
			CHECK(s1.remote_endpoint().value() == endpoint);

			auto s2 = a.accept().value();
			CHECK(s2.local_endpoint().value() == endpoint);
		}

		SECTION("no connect")
		{
			a.native_non_blocking(true);
			auto accept = a.accept();
			REQUIRE_FALSE(accept);
			CHECK(accept.error() == std::errc::operation_would_block);
		}

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{a.native_handle()};
			auto accept = a.accept();
			REQUIRE_FALSE(accept);
			CHECK(accept.error() == std::errc::bad_file_descriptor);
		}

		SECTION("not enough memory")
		{
			auto s = TestType::make_socket().value();
			REQUIRE(s.connect(endpoint));

			pal_test::bad_alloc_once x;
			auto accept = a.accept();
			REQUIRE_FALSE(accept);
			CHECK(accept.error() == std::errc::not_enough_memory);
		}
	}

	SECTION("accept with endpoint")
	{
		endpoint_t endpoint{TestType::loopback_v, 0};
		REQUIRE(pal_test::bind_next_available_port(a, endpoint));
		REQUIRE(a.listen());

		SECTION("success")
		{
			auto s1 = TestType::make_socket().value();
			REQUIRE(s1.connect(endpoint));
			CHECK(s1.remote_endpoint().value() == endpoint);

			endpoint_t s2_endpoint;
			auto s2 = a.accept(s2_endpoint).value();
			CHECK(s2.local_endpoint().value() == endpoint);
			CHECK(s2.remote_endpoint().value() == s2_endpoint);
			CHECK(s1.local_endpoint().value() == s2_endpoint);
		}

		SECTION("no connect")
		{
			a.native_non_blocking(true);
			auto accept = a.accept(endpoint);
			REQUIRE_FALSE(accept);
			CHECK(accept.error() == std::errc::operation_would_block);
		}

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{a.native_handle()};
			auto accept = a.accept(endpoint);
			REQUIRE_FALSE(accept);
			CHECK(accept.error() == std::errc::bad_file_descriptor);
		}

		SECTION("not enough memory")
		{
			auto s = TestType::make_socket().value();
			REQUIRE(s.connect(endpoint));

			pal_test::bad_alloc_once x;
			auto accept = a.accept(endpoint);
			REQUIRE_FALSE(accept);
			CHECK(accept.error() == std::errc::not_enough_memory);
		}
	}

	SECTION("native_non_blocking")
	{
		if constexpr (pal::is_windows_build)
		{
			auto set_mode = a.native_non_blocking();
			REQUIRE_FALSE(set_mode);
			CHECK(set_mode.error() == std::errc::operation_not_supported);

			REQUIRE(a.native_non_blocking(true));
		}
		else
		{
			// default off
			CHECK_FALSE(a.native_non_blocking().value());

			// turn on, check
			REQUIRE(a.native_non_blocking(true));
			CHECK(a.native_non_blocking().value());

			// turn off, check
			REQUIRE(a.native_non_blocking(false));
			CHECK_FALSE(a.native_non_blocking().value());
		}

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{a.native_handle()};
			auto get_mode = a.native_non_blocking();
			REQUIRE_FALSE(get_mode);

			if constexpr (pal::is_windows_build)
			{
				CHECK(get_mode.error() == std::errc::operation_not_supported);
			}
			else
			{
				CHECK(get_mode.error() == std::errc::bad_file_descriptor);
			}

			auto set_mode = a.native_non_blocking(false);
			REQUIRE_FALSE(set_mode);
			CHECK(set_mode.error() == std::errc::bad_file_descriptor);
		}
	}

	SECTION("reuse address")
	{
		pal::net::reuse_address reuse_address{true};
		REQUIRE(a.get_option(reuse_address));
		CHECK_FALSE(reuse_address);

		reuse_address = true;
		REQUIRE(a.set_option(reuse_address));

		REQUIRE(a.get_option(reuse_address));
		CHECK(reuse_address);

		SECTION("bad file descriptor")
		{
			pal_test::handle_guard{a.native_handle()};
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

		if constexpr (pal::is_windows_build)
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
			REQUIRE(a.get_option(reuse_port));
			CHECK_FALSE(reuse_port);

			reuse_port = true;
			REQUIRE(a.set_option(reuse_port));

			REQUIRE(a.get_option(reuse_port));
			CHECK(reuse_port);

			SECTION("bad file descriptor")
			{
				pal_test::handle_guard{a.native_handle()};
				auto opt = a.get_option(reuse_port);
				REQUIRE_FALSE(opt);
				CHECK(opt.error() == std::errc::bad_file_descriptor);

				opt = a.set_option(reuse_port);
				REQUIRE_FALSE(opt);
				CHECK(opt.error() == std::errc::bad_file_descriptor);
			}
		}
	}

	SECTION("make_socket_acceptor with endpoint")
	{
		endpoint_t endpoint{TestType::loopback_v, 0};

		SECTION("success")
		{
			// bind with existing acceptor and assume next port is available
			REQUIRE(pal_test::bind_next_available_port(a, endpoint));
			endpoint.port(pal_test::next_port(TestType::protocol_v));

			auto acceptor = pal::net::make_socket_acceptor(TestType::protocol_v, endpoint);
			REQUIRE(acceptor);
			CHECK(acceptor->local_endpoint().value() == endpoint);
		}

		SECTION("address in use")
		{
			REQUIRE(pal_test::bind_next_available_port(a, endpoint));

			auto acceptor = pal::net::make_socket_acceptor(TestType::protocol_v, endpoint);
			REQUIRE_FALSE(acceptor);
			CHECK(acceptor.error() == std::errc::address_in_use);
		}

		SECTION("not enough memory")
		{
			pal_test::bad_alloc_once x;
			auto acceptor = pal::net::make_socket_acceptor(TestType::protocol_v, endpoint);
			REQUIRE_FALSE(acceptor);
			CHECK(acceptor.error() == std::errc::not_enough_memory);
		}
	}

	SECTION("make_socket_acceptor with handle")
	{
		pal_test::handle_guard guard{a.release()};

		SECTION("success")
		{
			auto acceptor = pal::net::make_socket_acceptor(TestType::protocol_v, guard.handle);
			REQUIRE(acceptor);
			CHECK(acceptor->native_handle() == guard.handle);
			guard.release();
		}

		SECTION("not enough memory")
		{
			pal_test::bad_alloc_once x;
			auto acceptor = pal::net::make_socket_acceptor(TestType::protocol_v, guard.handle);
			REQUIRE_FALSE(acceptor);
			CHECK(acceptor.error() == std::errc::not_enough_memory);
		}
	}
}


TEMPLATE_TEST_CASE("net/basic_socket_acceptor", "",
	invalid_protocol)
{
	SECTION("make_socket_acceptor")
	{
		auto acceptor = pal::net::make_socket_acceptor(TestType());
		REQUIRE_FALSE(acceptor);
		CHECK(acceptor.error() == std::errc::protocol_not_supported);
	}
}


} // namespace
