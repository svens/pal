#include <pal/async/datagram_socket>
#include <pal/async/event_loop>
#include <pal/net/ip/udp>
#include <pal/test>
#include <catch2/catch_test_macros.hpp>

namespace {

TEST_CASE("async/event_loop")
{
	SECTION("make_loop: not enough memory")
	{
		pal_test::bad_alloc_once x;
		auto loop = pal::async::make_loop();
		REQUIRE_FALSE(loop);
		CHECK(loop.error() == std::errc::not_enough_memory);
	}

	SECTION("make_handle")
	{
		auto loop = pal::async::make_loop().value();
		auto socket = pal::net::make_datagram_socket(pal::net::ip::udp::v4()).value();

		SECTION("success")
		{
			auto async_socket = loop.make_handle(std::move(socket));
			REQUIRE(async_socket);
			REQUIRE(socket.native_socket()->handle == pal::net::native_socket_handle::invalid);
		}

		SECTION("bad file descriptor")
		{
			std::ignore = socket.release();
			REQUIRE(socket.native_socket()->handle == pal::net::native_socket_handle::invalid);
			auto async_socket = loop.make_handle(std::move(socket));
			REQUIRE_FALSE(async_socket);
			if constexpr (pal::os == pal::os_type::windows)
			{
				CHECK(async_socket.error() == std::errc::invalid_argument);
			}
			else
			{
				CHECK(async_socket.error() == std::errc::bad_file_descriptor);
			}
		}

		SECTION("not enough memory")
		{
			pal_test::bad_alloc_once x;
			auto async_socket = loop.make_handle(std::move(socket));
			REQUIRE_FALSE(async_socket);
			CHECK(async_socket.error() == std::errc::not_enough_memory);
			REQUIRE(socket.native_socket()->handle != pal::net::native_socket_handle::invalid);
		}
	}
}

} // namespace
