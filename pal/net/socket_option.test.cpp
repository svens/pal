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


} // namespace
