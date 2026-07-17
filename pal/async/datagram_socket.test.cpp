#include <pal/async/datagram_socket.hpp>
#include <pal/async/task_pool.hpp>
#include <pal/net/test.hpp>
#include <pal/test.hpp>
#include <pal/version.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <array>
#include <chrono>
#include <cstring>
#include <span>
#include <string_view>
#include <tuple>
#include <vector>

namespace
{

using namespace pal::async;
using namespace pal_test;
using namespace std::chrono_literals;

using udp = pal::net::ip::udp;
using endpoint = udp::endpoint;
using sync_socket = pal::net::basic_datagram_socket<udp>;
using async_socket = pal::async::handle<sync_socket>;
using receive_result = pal::result<async_socket::receive_from_result>;
using send_result = pal::result<async_socket::send_to_result>;

using pal::net::ip::port_type;

template <typename Fixture>
pal::result<sync_socket> make_sync_socket ()
{
	return pal::net::make_datagram_socket(Fixture::protocol_v, Fixture::loopback_endpoint());
}

template <typename Fixture>
pal::result<async_socket> make_async_socket (event_loop &loop)
{
	auto socket = make_sync_socket<Fixture>();
	REQUIRE(socket);
	return loop.make_handle(std::move(*socket));
}

// run_for() until \a done, bounded by an overall deadline
void run_until (event_loop &loop, const auto &done)
{
	const auto deadline = event_loop::clock::now() + 5s;
	while (!done())
	{
		const auto now = event_loop::clock::now();
		REQUIRE(now < deadline);
		auto r = loop.run_for(deadline - now);
		REQUIRE(r);
	}
}

std::span<std::byte> as_span (auto &storage)
{
	return {storage.data(), storage.size()};
}

TEMPLATE_TEST_CASE("async/datagram_socket", "", udp_v4, udp_v6)
{
	auto loop = make_loop();
	REQUIRE(loop);

	if constexpr (pal::os == pal::os_type::windows)
	{
		auto h = make_async_socket<TestType>(*loop);
		REQUIRE_FALSE(h);
		CHECK(h.error() == std::errc::operation_not_supported);
		return;
	}

	auto socket = make_async_socket<TestType>(*loop);
	REQUIRE(socket);
	auto socket_endpoint = socket->local_endpoint();
	REQUIRE(socket_endpoint);

	auto peer = make_sync_socket<TestType>();
	REQUIRE(peer);
	auto peer_endpoint = peer->local_endpoint();
	REQUIRE(peer_endpoint);

	SECTION("make_handle: curated re-exports")
	{
		CHECK(socket_endpoint->port() != port_type::unspecified);
		CHECK(has_expected_family<TestType>(socket_endpoint->address()));
		CHECK(static_cast<bool>(socket->native_socket()));

		const pal::net::receive_buffer_size buffer_size{256 * 1024};
		REQUIRE(socket->set_option(buffer_size));
		pal::net::receive_buffer_size confirm{};
		REQUIRE(socket->get_option(confirm));
		CHECK(confirm.value() >= 256 * 1024);
	}

	SECTION("receive: armed before the datagram arrives")
	{
		std::array<std::byte, 64> receive_storage{};
		task receive_task{receive_storage};

		const auto payload = case_name();

		int received = 0;
		endpoint source{};
		bool truncated = true;

		// clang-format off
		socket->start_receive_from(receive_task.borrow(), [&] (task_ptr &&p, receive_result &&r) noexcept
		{
			REQUIRE(r);
			source = r->endpoint;
			truncated = r->truncated;
			++received;
			std::ignore = p;
		});
		// clang-format on

		REQUIRE(peer->send_to(*socket_endpoint, payload));
		run_until(*loop, [&] { return received == 1; });

		CHECK(source == *peer_endpoint);
		CHECK_FALSE(truncated);
		REQUIRE(receive_task.span().size() == payload.size());
		CHECK(std::memcmp(receive_task.span().data(), payload.data(), payload.size()) == 0);
	}

	SECTION("receive: datagram queued before arming")
	{
		std::array<std::byte, 64> receive_storage{};
		task receive_task{receive_storage};

		const auto payload = case_name();

		REQUIRE(peer->send_to(*socket_endpoint, payload));
		auto n = loop->run_once();
		REQUIRE(n);
		CHECK(*n == 0);

		int received = 0;
		endpoint source{};

		// clang-format off
		socket->start_receive_from(receive_task.borrow(), [&] (task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			source = r->endpoint;
			++received;
		});
		// clang-format on

		run_until(*loop, [&] { return received == 1; });

		CHECK(source == *peer_endpoint);
		REQUIRE(receive_task.span().size() == payload.size());
		CHECK(std::memcmp(receive_task.span().data(), payload.data(), payload.size()) == 0);
	}

	SECTION("receive: batched: more datagrams than one drain quantum")
	{
		// exceeds the backend drain batch (16), so the direction spans multiple quanta/batches
		static constexpr size_t count = 32;

		std::array<std::array<std::byte, 8>, count> receive_storage{};
		std::array<task, count> receive_tasks;

		size_t received = 0;
		std::array<bool, count> seen{};

		// clang-format off
		for (size_t i = 0; i < count; ++i)
		{
			receive_tasks[i].span(as_span(receive_storage[i]));
			socket->start_receive_from(receive_tasks[i].borrow(), [&] (task_ptr &&p, receive_result &&r) noexcept
			{
				REQUIRE(r);
				REQUIRE(p->span().size() == 1);
				seen[std::to_integer<size_t>(p->span()[0])] = true;
				++received;
			});
		}
		// clang-format on

		for (size_t i = 0; i < count; ++i)
		{
			const char value = static_cast<char>(i);
			REQUIRE(peer->send_to(*socket_endpoint, std::string_view{&value, 1}));
		}

		run_until(*loop, [&] { return received == count; });

		for (size_t i = 0; i < count; ++i)
		{
			CHECK(seen[i]);
		}
	}

	SECTION("receive: truncation")
	{
		std::array<std::byte, 64> receive_storage{};
		task receive_task{receive_storage};
		receive_task.span(as_span(receive_storage).first(4));

		std::array<char, 8> wire{};
		for (size_t i = 0; i < wire.size(); ++i)
		{
			wire[i] = static_cast<char>(i);
		}

		int received = 0;
		bool truncated = false;

		// clang-format off
		socket->start_receive_from(receive_task.borrow(), [&] (task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			truncated = r->truncated;
			++received;
		});
		// clang-format on

		REQUIRE(peer->send_to(*socket_endpoint, wire));
		run_until(*loop, [&] { return received == 1; });

		CHECK(truncated);
		REQUIRE(receive_task.span().size() == 4);
		for (size_t i = 0; i < 4; ++i)
		{
			CHECK(receive_task.span()[i] == static_cast<std::byte>(i));
		}
	}

	SECTION("receive: writes only within the payload window")
	{
		// canaries around an app-attached window: the op must not touch storage outside task.span
		std::array<std::byte, 64> receive_storage{};
		receive_storage.fill(std::byte{0xaa});

		task receive_task{receive_storage};
		receive_task.span(as_span(receive_storage).subspan(24, 16));

		std::array<char, 32> wire{};
		wire.fill('\x55');

		int received = 0;

		// clang-format off
		socket->start_receive_from(receive_task.borrow(), [&] (task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			CHECK(r->truncated);
			++received;
		});
		// clang-format on

		REQUIRE(peer->send_to(*socket_endpoint, wire));
		run_until(*loop, [&] { return received == 1; });

		REQUIRE(receive_task.span().size() == 16);
		for (size_t i = 0; i < receive_storage.size(); ++i)
		{
			const auto expected = (i >= 24 && i < 40) ? std::byte{0x55} : std::byte{0xaa};
			CHECK(receive_storage[i] == expected);
		}
	}

	SECTION("send: single datagram")
	{
		std::array<std::byte, 64> send_storage{};
		task send_task{send_storage};

		const auto payload = case_name();
		std::memcpy(send_storage.data(), payload.data(), payload.size());
		send_task.span(as_span(send_storage).first(payload.size()));

		int sent = 0;

		// clang-format off
		socket->start_send_to(send_task.borrow(), *peer_endpoint, [&] (task_ptr &&, send_result &&r) noexcept
		{
			REQUIRE(r);
			++sent;
		});
		// clang-format on

		run_until(*loop, [&] { return sent == 1; });

		endpoint source{};
		std::array<char, 64> wire{};
		auto n = peer->receive_from(source, wire);
		REQUIRE(n);
		REQUIRE(*n == payload.size());
		CHECK(std::memcmp(wire.data(), payload.data(), payload.size()) == 0);
		CHECK(source == *socket_endpoint);

		CHECK(send_task.span().size() == payload.size());
	}

	SECTION("send: batched: more datagrams than one drain quantum")
	{
		static constexpr size_t count = 32;

		std::array<std::array<std::byte, 8>, count> send_storage{};
		std::array<task, count> send_tasks;

		size_t sent = 0;

		// clang-format off
		for (size_t i = 0; i < count; ++i)
		{
			send_storage[i][0] = static_cast<std::byte>(i);
			send_tasks[i].span(as_span(send_storage[i]).first(1));
			socket->start_send_to(send_tasks[i].borrow(), *peer_endpoint, [&] (task_ptr &&, send_result &&r) noexcept
			{
				REQUIRE(r);
				++sent;
			});
		}
		// clang-format on

		run_until(*loop, [&] { return sent == count; });

		std::array<bool, count> seen{};
		for (size_t i = 0; i < count; ++i)
		{
			endpoint source{};
			std::array<char, 8> wire{};
			auto n = peer->receive_from(source, wire);
			REQUIRE(n);
			REQUIRE(*n == 1);
			seen[static_cast<unsigned char>(wire[0])] = true;
			CHECK(source == *socket_endpoint);
		}

		for (size_t i = 0; i < count; ++i)
		{
			CHECK(seen[i]);
		}
	}

	SECTION("send: error observed in handler")
	{
		// payload above the UDP maximum: the send fails with message_size, delivered via the ready queue
		std::vector<std::byte> send_storage(70'000);
		task send_task{send_storage};

		int failed = 0;
		std::error_code ec{};

		// clang-format off
		socket->start_send_to(send_task.borrow(), *peer_endpoint, [&] (task_ptr &&, send_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			ec = r.error();
			++failed;
		});
		// clang-format on

		run_until(*loop, [&] { return failed == 1; });
		CHECK(ec == std::errc::message_size);
	}

	SECTION("both-async: send and receive drains share one loop")
	{
		static constexpr size_t count = 32;

		auto sender = make_async_socket<TestType>(*loop);
		REQUIRE(sender);

		std::array<std::array<std::byte, 8>, count> receive_storage{}, send_storage{};
		std::array<task, count> receive_tasks, send_tasks;

		size_t sent = 0, received = 0;
		std::array<bool, count> seen{};

		// clang-format off
		for (size_t i = 0; i < count; ++i)
		{
			receive_tasks[i].span(as_span(receive_storage[i]));
			socket->start_receive_from(receive_tasks[i].borrow(), [&] (task_ptr &&p, receive_result &&r) noexcept
			{
				REQUIRE(r);
				REQUIRE(p->span().size() == 1);
				seen[std::to_integer<size_t>(p->span()[0])] = true;
				++received;
			});

			send_storage[i][0] = static_cast<std::byte>(i);
			send_tasks[i].span(as_span(send_storage[i]).first(1));
			sender->start_send_to(send_tasks[i].borrow(), *socket_endpoint, [&] (task_ptr &&, send_result &&r) noexcept
			{
				REQUIRE(r);
				++sent;
			});
		}
		// clang-format on

		run_until(*loop, [&] { return sent == count && received == count; });

		for (size_t i = 0; i < count; ++i)
		{
			CHECK(seen[i]);
		}
	}

	SECTION("cancel: destruction with ops in flight")
	{
		std::array<std::byte, 64> receive_storage{};
		task receive_task{receive_storage};

		int canceled = 0;
		std::error_code ec{};

		{
			auto doomed = make_async_socket<TestType>(*loop);
			REQUIRE(doomed);

			// clang-format off
			doomed->start_receive_from(receive_task.borrow(), [&] (task_ptr &&, receive_result &&r) noexcept
			{
				REQUIRE_FALSE(r);
				ec = r.error();
				CHECK(loop->stats().io_in_flight == 0);
				++canceled;
			});
			// clang-format on
		}

		// synthesized completion is delivered from run(), io_uring close-parity
		CHECK(canceled == 0);
		CHECK(loop->stats().io_in_flight == 1);
		auto n = loop->run_once();
		REQUIRE(n);
		CHECK(*n == 1);
		CHECK(canceled == 1);
		CHECK(ec == std::errc::operation_canceled);
		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("pool-managed task round trip")
	{
		task_pool<2> pool;

		auto t = pool.try_acquire();
		REQUIRE(t);
		task *acquired = t.get();
		const auto full_span_size = t->span().size();
		t->span(t->span().first(8));

		int received = 0;

		// clang-format off
		socket->start_receive_from(std::move(t), [&] (task_ptr &&p, receive_result &&r) noexcept
		{
			REQUIRE(r);
			++received;
			std::ignore = p; // dropping recycles back into the pool
		});
		// clang-format on

		REQUIRE(peer->send_to(*socket_endpoint, case_name()));
		run_until(*loop, [&] { return received == 1; });

		// LIFO freelist: the recycled task comes back first, window reset to the slot's full buffer
		auto again = pool.try_acquire();
		REQUIRE(again);
		CHECK(again.get() == acquired);
		CHECK(again->span().size() == full_span_size);
	}
}

TEST_CASE("async/datagram_socket destructor contract")
{
	if constexpr (pal::build == pal::build_type::debug && pal::os != pal::os_type::windows)
	{
		// Loop destroyed before the cancel completions were dispatched -> REQUIRE crash.
		static std::array<std::byte, 64> storage{};
		static task t{storage};

		// clang-format off
		auto msg = pal_test::require_terminate([]
		{
			auto loop = make_loop().value();
			auto socket = make_sync_socket<udp_v4>().value();
			auto h = loop.make_handle(std::move(socket)).value();
			h.start_receive_from(t.borrow(), [] (task_ptr &&, receive_result &&) noexcept {});
		});
		// clang-format on

		CHECK(msg.contains("I/O ops"));
	}
}

} // namespace
