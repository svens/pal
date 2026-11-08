#include <pal/async/datagram_socket.hpp>
#include <pal/async/task_pool.hpp>
#include <pal/net/__socket.hpp>
#include <pal/net/test.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <numeric>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <vector>

#if __pal_os_linux
	#include <sys/mman.h>
#endif

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
using connected_receive_result = pal::result<async_socket::receive_result>;
using connected_send_result = pal::result<async_socket::send_result>;

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

template <typename Fixture>
pal::result<async_socket> make_connected_async_socket (event_loop &loop, const endpoint &peer)
{
	auto socket = make_sync_socket<Fixture>();
	REQUIRE(socket);
	REQUIRE(socket->connect(peer));
	return loop.make_handle(std::move(*socket));
}

endpoint expect_datagram (sync_socket &peer, std::string_view payload)
{
	endpoint source{};
	std::array<char, 64> wire{};
	auto n = peer.receive_from(source, wire);
	REQUIRE(n);
	CHECK(std::string_view{wire.data(), *n} == payload);
	return source;
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

	auto socket = make_async_socket<TestType>(*loop);
	REQUIRE(socket);
	auto socket_endpoint = socket->local_endpoint();
	REQUIRE(socket_endpoint);

	auto peer = make_sync_socket<TestType>();
	REQUIRE(peer);
	auto peer_endpoint = peer->local_endpoint();
	REQUIRE(peer_endpoint);

	const auto payload = case_name();

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

	SECTION("make_handle: dead socket")
	{
		auto dead = make_sync_socket<TestType>();
		REQUIRE(dead);
		close_native_handle(*dead);

		auto h = loop->make_handle(std::move(*dead));
		REQUIRE_FALSE(h);
		CHECK(h.error() == std::errc::bad_file_descriptor);
	}

	#if __pal_os_linux
	{
		SECTION("make_handle: backend registration failure")
		{
			const auto fd = ::memfd_create("pal_test", MFD_CLOEXEC);
			REQUIRE(fd != -1);

			auto file = pal::net::make_datagram_socket(TestType::protocol_v, pal::net::__socket::from_sys(fd));
			REQUIRE(file);

			auto h = loop->make_handle(std::move(*file));
			REQUIRE_FALSE(h);
			CHECK(h.error() == std::errc::operation_not_permitted);
		}
	}
	#endif

	SECTION("make_handle: adopted native socket completes each op once")
	{
		static constexpr size_t count = 16;

		const auto protocol = TestType::protocol_v;
		const auto native = pal::net::__socket::from_sys(::socket(protocol.family(), protocol.type(), protocol.protocol()));
		REQUIRE(native != pal::net::__socket::handle_type::invalid);

		auto adopted_sync = pal::net::make_datagram_socket(protocol, native);
		REQUIRE(adopted_sync);
		REQUIRE(adopted_sync->bind(TestType::loopback_endpoint()));
		auto adopted = loop->make_handle(std::move(*adopted_sync));
		REQUIRE(adopted);
		auto adopted_endpoint = adopted->local_endpoint();
		REQUIRE(adopted_endpoint);

		for (size_t i = 0; i < count; ++i)
		{
			REQUIRE(peer->send_to(*adopted_endpoint, payload));
		}

		std::array<std::array<std::byte, 64>, count> receive_storage{}, send_storage{};
		std::array<task, count> receive_tasks, send_tasks;
		size_t received = 0, sent = 0;

		for (size_t i = 0; i < count; ++i)
		{
			receive_tasks[i].span(as_span(receive_storage[i]));
			adopted->start_receive_from(receive_tasks[i].borrow(), [&](task_ptr &&, receive_result &&r) noexcept
			{
				REQUIRE(r);
				++received;
			});

			send_tasks[i].span(as_span(send_storage[i]).first(1));
			adopted->start_send_to(send_tasks[i].borrow(), *peer_endpoint, [&](task_ptr &&, send_result &&r) noexcept
			{
				REQUIRE(r);
				++sent;
			});
		}

		run_until(*loop, [&] { return received == count && sent == count; });
		REQUIRE(loop->run_for(10ms));
		CHECK(received == count);
		CHECK(sent == count);
	}

	SECTION("receive: armed before the datagram arrives")
	{
		std::array<std::byte, 64> receive_storage{};
		task receive_task{receive_storage};

		int received = 0;
		endpoint source{};
		bool truncated = true;

		socket->start_receive_from(receive_task.borrow(), [&](task_ptr &&p, receive_result &&r) noexcept
		{
			REQUIRE(r);
			source = r->endpoint;
			truncated = r->truncated;
			++received;
			std::ignore = p;
		});

		REQUIRE(peer->send_to(*socket_endpoint, payload));
		run_until(*loop, [&] { return received == 1; });

		CHECK(source == *peer_endpoint);
		CHECK_FALSE(truncated);
		CHECK(std::ranges::equal(receive_task.span(), std::as_bytes(std::span{payload})));
	}

	SECTION("receive: datagram queued before arming")
	{
		std::array<std::byte, 64> receive_storage{};
		task receive_task{receive_storage};

		REQUIRE(peer->send_to(*socket_endpoint, payload));
		auto n = loop->run_once();
		REQUIRE(n);
		CHECK(*n == 0);

		int received = 0;
		endpoint source{};

		socket->start_receive_from(receive_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			source = r->endpoint;
			++received;
		});

		run_until(*loop, [&] { return received == 1; });

		CHECK(source == *peer_endpoint);
		CHECK(std::ranges::equal(receive_task.span(), std::as_bytes(std::span{payload})));
	}

	SECTION("receive: more ops armed than datagrams")
	{
		std::array<std::byte, 64> first_storage{}, parked_storage{};
		task first_task{first_storage}, parked_task{parked_storage};

		int received = 0, canceled = 0;

		{
			auto doomed = make_async_socket<TestType>(*loop);
			REQUIRE(doomed);
			auto doomed_endpoint = doomed->local_endpoint();
			REQUIRE(doomed_endpoint);

			doomed->start_receive_from(first_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
			{
				REQUIRE(r);
				++received;
			});
			doomed->start_receive_from(parked_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
			{
				REQUIRE_FALSE(r);
				CHECK(r.error() == std::errc::operation_canceled);
				++canceled;
			});

			REQUIRE(peer->send_to(*doomed_endpoint, payload));
			run_until(*loop, [&] { return received == 1; });

			CHECK(canceled == 0);
			CHECK(loop->stats().io_in_flight == 1);
		}

		auto n = loop->run_once();
		REQUIRE(n);
		CHECK(*n == 1);
		CHECK(canceled == 1);
		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("receive: move-assign cancels the replaced socket's armed receive")
	{
		std::array<std::byte, 64> storage{};
		task t{storage};
		int canceled = 0;

		auto target = make_async_socket<TestType>(*loop);
		REQUIRE(target);
		target->start_receive_from(t.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			CHECK(r.error() == std::errc::operation_canceled);
			++canceled;
		});

		auto source = make_async_socket<TestType>(*loop);
		REQUIRE(source);
		*target = std::move(*source);

		run_until(*loop, [&] { return canceled == 1; });
		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("receive: batched: more datagrams than one drain quantum")
	{
		static constexpr size_t count = 2 * event_loop_config{}.drain_batch;

		std::array<std::array<std::byte, 8>, count> receive_storage{};
		std::array<task, count> receive_tasks;

		size_t received = 0;
		std::array<bool, count> seen{};

		for (size_t i = 0; i < count; ++i)
		{
			receive_tasks[i].span(as_span(receive_storage[i]));
			socket->start_receive_from(receive_tasks[i].borrow(), [&](task_ptr &&p, receive_result &&r) noexcept
			{
				REQUIRE(r);
				REQUIRE(p->span().size() == 1);
				seen[std::to_integer<size_t>(p->span()[0])] = true;
				++received;
			});
		}

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

	SECTION("receive: non-default batch bounds")
	{
		static constexpr size_t drain_batch = 2;

		event_loop_config config{};
		config.poll_batch = 1;
		config.drain_batch = drain_batch;

		auto tuned = make_loop(config);
		REQUIRE(tuned);

		auto tuned_socket = make_async_socket<TestType>(*tuned);
		REQUIRE(tuned_socket);
		auto tuned_endpoint = tuned_socket->local_endpoint();
		REQUIRE(tuned_endpoint);

		static constexpr size_t count = 4 * drain_batch;

		std::array<std::array<std::byte, 8>, count> receive_storage{};
		std::array<task, count> receive_tasks;

		size_t received = 0;
		std::array<bool, count> seen{};

		for (size_t i = 0; i < count; ++i)
		{
			receive_tasks[i].span(as_span(receive_storage[i]));
			tuned_socket->start_receive_from(receive_tasks[i].borrow(), [&](task_ptr &&p, receive_result &&r) noexcept
			{
				REQUIRE(r);
				REQUIRE(p->span().size() == 1);
				seen[std::to_integer<size_t>(p->span()[0])] = true;
				++received;
			});
		}

		for (size_t i = 0; i < count; ++i)
		{
			const char value = static_cast<char>(i);
			REQUIRE(peer->send_to(*tuned_endpoint, std::string_view{&value, 1}));
		}

		run_until(*tuned, [&] { return received == count; });

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

		socket->start_receive_from(receive_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			truncated = r->truncated;
			++received;
		});

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

		socket->start_receive_from(receive_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			CHECK(r->truncated);
			++received;
		});

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

		std::memcpy(send_storage.data(), payload.data(), payload.size());
		send_task.span(as_span(send_storage).first(payload.size()));

		int sent = 0;

		socket->start_send_to(send_task.borrow(), *peer_endpoint, [&](task_ptr &&, send_result &&r) noexcept
		{
			REQUIRE(r);
			++sent;
		});

		run_until(*loop, [&] { return sent == 1; });

		CHECK(expect_datagram(*peer, payload) == *socket_endpoint);
		CHECK(send_task.span().size() == payload.size());
	}

	SECTION("send: batched: more datagrams than one drain quantum")
	{
		static constexpr size_t count = 2 * event_loop_config{}.drain_batch;

		std::array<std::array<std::byte, 8>, count> send_storage{};
		std::array<task, count> send_tasks;

		size_t sent = 0;

		for (size_t i = 0; i < count; ++i)
		{
			send_storage[i][0] = static_cast<std::byte>(i);
			send_tasks[i].span(as_span(send_storage[i]).first(1));
			socket->start_send_to(send_tasks[i].borrow(), *peer_endpoint, [&](task_ptr &&, send_result &&r) noexcept
			{
				REQUIRE(r);
				++sent;
			});
		}

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

		socket->start_send_to(send_task.borrow(), *peer_endpoint, [&](task_ptr &&, send_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			ec = r.error();
			++failed;
		});

		run_until(*loop, [&] { return failed == 1; });
		CHECK(ec == std::errc::message_size);
	}

	SECTION("send: task reuse after failed op")
	{
		std::vector<std::byte> send_storage(70'000);
		task send_task{send_storage};

		int failed = 0, sent = 0;

		socket->start_send_to(send_task.borrow(), *peer_endpoint, [&](task_ptr &&, send_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			CHECK(r.error() == std::errc::message_size);
			++failed;
		});

		run_until(*loop, [&] { return failed == 1; });

		std::memcpy(send_storage.data(), payload.data(), payload.size());
		send_task.span(as_span(send_storage).first(payload.size()));

		socket->start_send_to(send_task.borrow(), *peer_endpoint, [&](task_ptr &&, send_result &&r) noexcept
		{
			REQUIRE(r);
			++sent;
		});

		run_until(*loop, [&] { return sent == 1; });

		expect_datagram(*peer, payload);
	}

	SECTION("send: reply to receive source")
	{
		std::array<std::byte, 64> storage{};
		task echo_task{storage};

		int received = 0, replied = 0;

		socket->start_receive_from(echo_task.borrow(), [&](task_ptr &&p, receive_result &&r) noexcept
		{
			REQUIRE(r);
			CHECK(r->endpoint == *peer_endpoint);
			++received;
			socket->start_reply(std::move(p), [&](task_ptr &&, send_result &&s) noexcept
			{
				REQUIRE(s);
				++replied;
			});
		});

		REQUIRE(peer->send_to(*socket_endpoint, payload));
		run_until(*loop, [&] { return received == 1 && replied == 1; });

		CHECK(expect_datagram(*peer, payload) == *socket_endpoint);
	}

	SECTION("connected: send and receive")
	{
		auto connected = make_connected_async_socket<TestType>(*loop, *peer_endpoint);
		REQUIRE(connected);
		auto connected_endpoint = connected->local_endpoint();
		REQUIRE(connected_endpoint);

		std::array<std::byte, 64> send_storage{};
		task send_task{send_storage};
		std::memcpy(send_storage.data(), payload.data(), payload.size());
		send_task.span(as_span(send_storage).first(payload.size()));

		std::array<std::byte, 64> receive_storage{};
		task receive_task{receive_storage};

		int sent = 0, received = 0;
		bool truncated = true;

		connected->start_send(send_task.borrow(), [&](task_ptr &&, connected_send_result &&r) noexcept
		{
			REQUIRE(r);
			++sent;
		});
		connected->start_receive(receive_task.borrow(), [&](task_ptr &&, connected_receive_result &&r) noexcept
		{
			REQUIRE(r);
			truncated = r->truncated;
			++received;
		});

		run_until(*loop, [&] { return sent == 1; });
		CHECK(expect_datagram(*peer, payload) == *connected_endpoint);

		REQUIRE(peer->send_to(*connected_endpoint, payload));
		run_until(*loop, [&] { return received == 1; });

		CHECK_FALSE(truncated);
		CHECK(std::ranges::equal(receive_task.span(), std::as_bytes(std::span{payload})));
	}

	SECTION("connected: receive truncation")
	{
		auto connected = make_connected_async_socket<TestType>(*loop, *peer_endpoint);
		REQUIRE(connected);
		auto connected_endpoint = connected->local_endpoint();
		REQUIRE(connected_endpoint);

		std::array<std::byte, 64> receive_storage{};
		task receive_task{receive_storage};
		receive_task.span(as_span(receive_storage).first(4));

		int received = 0;
		bool truncated = false;

		connected->start_receive(receive_task.borrow(), [&](task_ptr &&, connected_receive_result &&r) noexcept
		{
			REQUIRE(r);
			truncated = r->truncated;
			++received;
		});

		REQUIRE(peer->send_to(*connected_endpoint, std::string_view{"12345678"}));
		run_until(*loop, [&] { return received == 1; });

		CHECK(truncated);
		CHECK(std::ranges::equal(receive_task.span(), std::as_bytes(std::span{std::string_view{"1234"}})));
	}

	SECTION("connected: receive mixed with receive_from in one drain")
	{
		std::array<std::byte, 64> from_storage{}, plain_storage{};
		task from_task{from_storage}, plain_task{plain_storage};

		int received = 0;
		endpoint source{};

		socket->start_receive_from(from_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			source = r->endpoint;
			++received;
		});
		socket->start_receive(plain_task.borrow(), [&](task_ptr &&, connected_receive_result &&r) noexcept
		{
			REQUIRE(r);
			CHECK_FALSE(r->truncated);
			++received;
		});

		REQUIRE(peer->send_to(*socket_endpoint, payload));
		REQUIRE(peer->send_to(*socket_endpoint, payload));
		run_until(*loop, [&] { return received == 2; });

		CHECK(source == *peer_endpoint);
		CHECK(std::ranges::equal(from_task.span(), std::as_bytes(std::span{payload})));
		CHECK(std::ranges::equal(plain_task.span(), std::as_bytes(std::span{payload})));
	}

	SECTION("connected: send on unconnected socket fails")
	{
		std::array<std::byte, 64> send_storage{};
		task send_task{send_storage};
		send_task.span(as_span(send_storage).first(payload.size()));

		int failed = 0;
		std::error_code ec{};

		socket->start_send(send_task.borrow(), [&](task_ptr &&, connected_send_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			ec = r.error();
			++failed;
		});

		run_until(*loop, [&] { return failed == 1; });
		CHECK((ec == std::errc::destination_address_required || ec == std::errc::not_connected));
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

		for (size_t i = 0; i < count; ++i)
		{
			receive_tasks[i].span(as_span(receive_storage[i]));
			socket->start_receive_from(receive_tasks[i].borrow(), [&](task_ptr &&p, receive_result &&r) noexcept
			{
				REQUIRE(r);
				REQUIRE(p->span().size() == 1);
				seen[std::to_integer<size_t>(p->span()[0])] = true;
				++received;
			});

			send_storage[i][0] = static_cast<std::byte>(i);
			send_tasks[i].span(as_span(send_storage[i]).first(1));
			sender->start_send_to(send_tasks[i].borrow(), *socket_endpoint, [&](task_ptr &&, send_result &&r) noexcept
			{
				REQUIRE(r);
				++sent;
			});
		}

		run_until(*loop, [&] { return sent == count && received == count; });

		for (size_t i = 0; i < count; ++i)
		{
			CHECK(seen[i]);
		}
	}

	SECTION("cancel: destruction with a connected receive in flight")
	{
		std::array<std::byte, 64> receive_storage{};
		task receive_task{receive_storage};

		int canceled = 0;
		std::error_code ec{};

		{
			auto doomed = make_connected_async_socket<TestType>(*loop, *peer_endpoint);
			REQUIRE(doomed);

			doomed->start_receive(receive_task.borrow(), [&](task_ptr &&, connected_receive_result &&r) noexcept
			{
				REQUIRE_FALSE(r);
				ec = r.error();
				++canceled;
			});
		}

		REQUIRE(loop->settle());
		CHECK(canceled == 1);
		CHECK(ec == std::errc::operation_canceled);
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

			doomed->start_receive_from(receive_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
			{
				REQUIRE_FALSE(r);
				ec = r.error();
				CHECK(loop->stats().io_in_flight == 0);
				++canceled;
			});
		}

		CHECK(canceled == 0);
		CHECK(loop->stats().io_in_flight == 1);
		auto n = loop->settle();
		REQUIRE(n);
		CHECK(*n == 1);
		CHECK(canceled == 1);
		CHECK(ec == std::errc::operation_canceled);
		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("cancel: destruction while queued for drain")
	{
		std::array<std::byte, 64> trigger_storage{}, destroyer_storage{}, send_storage{};
		task trigger_task{trigger_storage}, destroyer_task{destroyer_storage}, send_task{send_storage};
		send_task.span(as_span(send_storage).first(1));

		std::optional<async_socket> doomed{make_async_socket<TestType>(*loop).value()};

		auto n = loop->run_once();
		REQUIRE(n);
		CHECK(*n == 0);

		int received = 0, canceled = 0;

		socket->start_receive_from(trigger_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			doomed->start_send_to(send_task.borrow(), *peer_endpoint, [&](task_ptr &&, send_result &&s) noexcept
			{
				REQUIRE_FALSE(s);
				CHECK(s.error() == std::errc::operation_canceled);
				++canceled;
			});
			++received;
		});
		socket->start_receive_from(destroyer_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			doomed.reset();
			++received;
		});

		REQUIRE(peer->send_to(*socket_endpoint, std::string_view{"x"}));
		REQUIRE(peer->send_to(*socket_endpoint, std::string_view{"y"}));
		run_until(*loop, [&] { return received == 2 && canceled == 1; });

		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("cancel: destruction overtakes a completed receive")
	{
		std::array<std::byte, 64> first_storage{}, second_storage{};
		task first_task{first_storage}, second_task{second_storage};

		int received = 0, canceled = 0;
		std::error_code ec{};

		std::optional<async_socket> doomed{make_async_socket<TestType>(*loop).value()};
		auto doomed_endpoint = doomed->local_endpoint();
		REQUIRE(doomed_endpoint);

		doomed->start_receive_from(first_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			doomed.reset();
			++received;
		});
		doomed->start_receive_from(second_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			ec = r.error();
			++canceled;
		});

		REQUIRE(peer->send_to(*doomed_endpoint, payload));
		REQUIRE(peer->send_to(*doomed_endpoint, payload));
		run_until(*loop, [&] { return received == 1 && canceled == 1; });

		CHECK(ec == std::errc::operation_canceled);
		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("cancel: destruction leaves other sockets' completions intact")
	{
		std::array<std::byte, 64> destroyer_storage{}, doomed_storage{}, live_storage{};
		task destroyer_task{destroyer_storage}, doomed_task{doomed_storage}, live_task{live_storage};

		int received = 0, canceled = 0;

		std::optional<async_socket> doomed{make_async_socket<TestType>(*loop).value()};
		auto doomed_endpoint = doomed->local_endpoint();
		REQUIRE(doomed_endpoint);

		doomed->start_receive_from(destroyer_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			doomed.reset();
			++received;
		});
		doomed->start_receive_from(doomed_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			CHECK(r.error() == std::errc::operation_canceled);
			++canceled;
		});
		socket->start_receive_from(live_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			++received;
		});

		REQUIRE(peer->send_to(*doomed_endpoint, payload));
		REQUIRE(peer->send_to(*doomed_endpoint, payload));
		REQUIRE(peer->send_to(*socket_endpoint, payload));
		run_until(*loop, [&] { return received == 2 && canceled == 1; });

		CHECK(std::ranges::equal(live_task.span(), std::as_bytes(std::span{payload})));
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

		socket->start_receive_from(std::move(t), [&](task_ptr &&p, receive_result &&r) noexcept
		{
			REQUIRE(r);
			++received;
			std::ignore = p; // dropping recycles back into the pool
		});

		REQUIRE(peer->send_to(*socket_endpoint, payload));
		run_until(*loop, [&] { return received == 1; });

		// LIFO freelist: the recycled task comes back first, window reset to the slot's full buffer
		auto again = pool.try_acquire();
		REQUIRE(again);
		CHECK(again.get() == acquired);
		CHECK(again->span().size() == full_span_size);
	}
}

} // namespace
