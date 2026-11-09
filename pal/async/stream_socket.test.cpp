#include <pal/async/stream_socket.hpp>
#include <pal/async/task_pool.hpp>
#include <pal/net/socket_option.hpp>
#include <pal/net/test.hpp>
#include <pal/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if __pal_os_linux
	#include <pal/net/__socket.hpp>
	#include <sys/mman.h>
#endif

namespace
{

using namespace pal::async;
using namespace pal_test;
using namespace std::chrono_literals;

using tcp = pal::net::ip::tcp;
using sync_socket = pal::net::basic_stream_socket<tcp>;
using async_socket = pal::async::handle<sync_socket>;
using receive_result = pal::result<async_socket::receive_result>;
using send_result = pal::result<async_socket::send_result>;

using pal::net::ip::port_type;

template <typename Fixture>
std::pair<sync_socket, sync_socket> make_connected_pair ()
{
	auto acceptor = Fixture::make_acceptor();
	REQUIRE(acceptor);
	REQUIRE(acceptor->bind(Fixture::loopback_endpoint()));
	REQUIRE(acceptor->listen());
	auto acceptor_endpoint = acceptor->local_endpoint();
	REQUIRE(acceptor_endpoint);

	auto client = Fixture::make_socket();
	REQUIRE(client);
	REQUIRE(client->connect(*acceptor_endpoint));

	auto server = acceptor->accept();
	REQUIRE(server);

	return {std::move(*client), std::move(*server)};
}

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

void drain_available (sync_socket &peer, std::vector<std::byte> &sink)
{
	std::array<std::byte, size_t{64} * 1024> chunk{};
	for (;;)
	{
		auto n = peer.receive(chunk);
		if (!n)
		{
			REQUIRE((n.error() == std::errc::timed_out || n.error() == std::errc::operation_would_block));
			return;
		}
		REQUIRE(*n > 0);
		sink.insert(sink.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(*n));
	}
}

std::string_view as_string_view (std::span<const std::byte> bytes)
{
	return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

TEMPLATE_TEST_CASE("async/stream_socket", "", tcp_v4, tcp_v6)
{
	auto loop = make_loop();
	REQUIRE(loop);

	auto [local, connected_peer] = make_connected_pair<TestType>();
	std::optional<sync_socket> peer_holder{std::move(connected_peer)};
	auto &peer = *peer_holder;
	auto peer_endpoint = peer.local_endpoint();
	REQUIRE(peer_endpoint);

	auto socket = loop->make_handle(std::move(local));
	REQUIRE(socket);

	const auto payload = case_name();

	SECTION("make_handle: curated re-exports")
	{
		auto socket_endpoint = socket->local_endpoint();
		REQUIRE(socket_endpoint);
		CHECK(socket_endpoint->port() != port_type::unspecified);
		CHECK(has_expected_family<TestType>(socket_endpoint->address()));

		auto remote = socket->remote_endpoint();
		REQUIRE(remote);
		CHECK(*remote == *peer_endpoint);

		CHECK(static_cast<bool>(socket->native_socket()));

		const pal::net::receive_buffer_size buffer_size{256 * 1024};
		REQUIRE(socket->set_option(buffer_size));
		pal::net::receive_buffer_size confirm{};
		REQUIRE(socket->get_option(confirm));
		CHECK(confirm.value() >= 256 * 1024);
	}

	SECTION("make_handle: dead socket")
	{
		auto [dead, dead_peer] = make_connected_pair<TestType>();
		close_native_handle(dead);

		auto h = loop->make_handle(std::move(dead));
		REQUIRE_FALSE(h);
		CHECK(h.error() == std::errc::bad_file_descriptor);
	}

	#if __pal_os_linux
	{
		SECTION("make_handle: backend registration failure")
		{
			const auto fd = ::memfd_create("pal_test", MFD_CLOEXEC);
			REQUIRE(fd != -1);

			auto file = pal::net::make_stream_socket(
				TestType::protocol_v,
				pal::net::__socket::from_sys(fd)
			);
			REQUIRE(file);

			auto h = loop->make_handle(std::move(*file));
			REQUIRE_FALSE(h);
			CHECK(h.error() == std::errc::operation_not_permitted);
		}
	}
	#endif

	SECTION("receive: armed before data arrives")
	{
		std::array<std::byte, 64> storage{};
		task t{storage};

		int received = 0;
		socket->start_receive(t.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			++received;
		});

		auto n = loop->run_once();
		REQUIRE(n);
		CHECK(*n == 0);

		REQUIRE(peer.send(payload));
		run_until(*loop, [&] { return received == 1; });
		CHECK(as_string_view(t.span()) == payload);
		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("receive: data queued before arming")
	{
		REQUIRE(peer.send(payload));

		auto n = loop->run_once();
		REQUIRE(n);
		CHECK(*n == 0);

		std::array<std::byte, 64> storage{};
		task t{storage};

		int received = 0;
		socket->start_receive(t.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			++received;
		});

		run_until(*loop, [&] { return received == 1; });
		CHECK(as_string_view(t.span()) == payload);
	}

	SECTION("receive: parked receive resumes on the next data")
	{
		std::array<std::byte, 64> first_storage{}, second_storage{};
		task first{first_storage}, second{second_storage};

		int received = 0;
		for (auto *t: {&first, &second})
		{
			socket->start_receive(t->borrow(), [&](task_ptr &&, receive_result &&r) noexcept
			{
				REQUIRE(r);
				++received;
			});
		}

		REQUIRE(peer.send(payload));
		run_until(*loop, [&] { return received == 1; });
		REQUIRE(loop->run_once());
		CHECK(received == 1);

		REQUIRE(peer.send(std::string_view{"x"}));
		run_until(*loop, [&] { return received == 2; });
		CHECK(as_string_view(first.span()) == payload);
		CHECK(as_string_view(second.span()) == "x");
	}

	SECTION("receive: window smaller than available data")
	{
		REQUIRE(peer.send(payload));

		std::array<std::byte, 64> first_storage{}, second_storage{};
		task first{as_span(first_storage).first(4)}, second{second_storage};

		int received = 0;
		for (auto *t: {&first, &second})
		{
			socket->start_receive(t->borrow(), [&](task_ptr &&, receive_result &&r) noexcept
			{
				REQUIRE(r);
				++received;
			});
		}

		run_until(*loop, [&] { return received == 2; });
		CHECK(as_string_view(first.span()) == payload.substr(0, 4));
		CHECK(as_string_view(second.span()) == payload.substr(4));
	}

	SECTION("receive: more pending receives than one drain quantum")
	{
		event_loop_config config{};
		config.drain_batch = 1;

		auto tuned = make_loop(config);
		REQUIRE(tuned);

		auto [tuned_local, tuned_peer] = make_connected_pair<TestType>();
		auto tuned_socket = tuned->make_handle(std::move(tuned_local));
		REQUIRE(tuned_socket);

		static constexpr size_t count = 4;
		REQUIRE(tuned_peer.send(std::string_view{"abcd"}));

		std::array<std::array<std::byte, 1>, count> storage{};
		std::array<task, count> tasks;
		size_t received = 0;

		for (size_t i = 0; i < count; ++i)
		{
			tasks[i].span(as_span(storage[i]));
			tuned_socket->start_receive(tasks[i].borrow(), [&](task_ptr &&, receive_result &&r) noexcept
			{
				REQUIRE(r);
				++received;
			});
		}

		run_until(*tuned, [&] { return received == count; });

		std::string wire;
		for (const auto &t: tasks)
		{
			wire += as_string_view(t.span());
		}
		CHECK(wire == "abcd");
	}

	SECTION("receive: end of stream")
	{
		REQUIRE(peer.send(payload));
		REQUIRE(peer.shutdown(pal::net::socket_base::shutdown_send));

		std::array<std::byte, 64> data_storage{}, eof_storage{}, again_storage{};
		task data{data_storage}, eof{eof_storage}, again{again_storage};

		int received = 0;
		for (auto *t: {&data, &eof, &again})
		{
			socket->start_receive(t->borrow(), [&](task_ptr &&, receive_result &&r) noexcept
			{
				REQUIRE(r);
				++received;
			});
		}

		run_until(*loop, [&] { return received == 3; });
		CHECK(as_string_view(data.span()) == payload);
		CHECK(eof.span().empty());
		CHECK(again.span().empty());
	}

	SECTION("receive: peer reset")
	{
		std::array<std::byte, 64> storage{};
		task t{storage};

		std::error_code ec{};
		socket->start_receive(t.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			ec = r.error();
		});

		REQUIRE(peer.set_option(pal::net::linger{true, 0s}));
		peer_holder.reset();

		run_until(*loop, [&] { return ec != std::error_code{}; });
		CHECK(ec == std::errc::connection_reset);
	}

	SECTION("send: single")
	{
		std::array<std::byte, 64> storage{};
		std::ranges::copy(std::as_bytes(std::span{payload}), storage.begin());
		task t{as_span(storage).first(payload.size())};

		int sent = 0;
		socket->start_send(t.borrow(), [&](task_ptr &&, send_result &&r) noexcept
		{
			REQUIRE(r);
			++sent;
		});

		run_until(*loop, [&] { return sent == 1; });
		CHECK(t.span().size() == payload.size());

		std::array<char, 64> wire{};
		auto n = peer.receive(wire);
		REQUIRE(n);
		CHECK(std::string_view{wire.data(), *n} == payload);
	}

	SECTION("send: whole window across partial writes")
	{
		static constexpr size_t size = size_t{1024} * 1024;

		REQUIRE(socket->set_option(pal::net::send_buffer_size{4096}));
		REQUIRE(peer.set_option(pal::net::non_blocking_io{true}));

		std::vector<std::byte> storage(size);
		std::ranges::generate(storage, [i = 0u]() mutable { return static_cast<std::byte>(i++ * 31u); });
		task t{storage};

		int sent = 0;
		socket->start_send(t.borrow(), [&](task_ptr &&, send_result &&r) noexcept
		{
			REQUIRE(r);
			++sent;
		});

		std::vector<std::byte> wire;
		wire.reserve(size);
		const auto deadline = event_loop::clock::now() + 10s;
		while (sent == 0 || wire.size() < size)
		{
			REQUIRE(event_loop::clock::now() < deadline);
			REQUIRE(loop->run_for(1ms));
			drain_available(peer, wire);
		}

		CHECK(sent == 1);
		CHECK(t.span().size() == size);
		CHECK(std::ranges::equal(wire, storage));
		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("send: queued sends across partial writes under a one-op quantum")
	{
		static constexpr size_t size = size_t{256} * 1024;

		event_loop_config config{};
		config.drain_batch = 1;

		auto tuned = make_loop(config);
		REQUIRE(tuned);

		auto [tuned_local, tuned_peer] = make_connected_pair<TestType>();
		REQUIRE(tuned_local.set_option(pal::net::send_buffer_size{4096}));
		REQUIRE(tuned_peer.set_option(pal::net::non_blocking_io{true}));
		auto tuned_socket = tuned->make_handle(std::move(tuned_local));
		REQUIRE(tuned_socket);

		std::vector<std::byte> storage(2 * size);
		std::ranges::generate(storage, [i = 0u]() mutable { return static_cast<std::byte>(i++ * 31u); });
		task first{std::span{storage}.first(size)}, second{std::span{storage}.subspan(size)};

		int sent = 0;
		for (auto *t: {&first, &second})
		{
			tuned_socket->start_send(t->borrow(), [&](task_ptr &&, send_result &&r) noexcept
			{
				REQUIRE(r);
				++sent;
			});
		}

		std::vector<std::byte> wire;
		wire.reserve(storage.size());
		const auto deadline = event_loop::clock::now() + 10s;
		while (sent < 2 || wire.size() < storage.size())
		{
			REQUIRE(event_loop::clock::now() < deadline);
			REQUIRE(tuned->run_for(1ms));
			drain_available(tuned_peer, wire);
		}

		CHECK(std::ranges::equal(wire, storage));
		CHECK(tuned->stats().io_in_flight == 0);
	}

	SECTION("send: error after partial progress")
	{
		static constexpr size_t size = size_t{1024} * 1024;

		REQUIRE(socket->set_option(pal::net::send_buffer_size{4096}));

		std::vector<std::byte> storage(size);
		task t{storage};

		int failed = 0;
		socket->start_send(t.borrow(), [&](task_ptr &&, send_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			++failed;
		});

		REQUIRE(loop->run_once());
		CHECK(failed == 0);

		REQUIRE(peer.set_option(pal::net::linger{true, 0s}));
		peer_holder.reset();

		run_until(*loop, [&] { return failed == 1; });
		CHECK(t.span().size() == size);
		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("send: queued sends keep start order")
	{
		static constexpr size_t count = 16;

		std::array<std::array<std::byte, 1>, count> storage{};
		std::array<task, count> tasks;
		size_t sent = 0;

		for (size_t i = 0; i < count; ++i)
		{
			storage[i][0] = static_cast<std::byte>('a' + i);
			tasks[i].span(as_span(storage[i]));
			socket->start_send(tasks[i].borrow(), [&](task_ptr &&, send_result &&r) noexcept
			{
				REQUIRE(r);
				++sent;
			});
		}

		run_until(*loop, [&] { return sent == count; });

		std::string wire;
		std::array<char, 64> chunk{};
		while (wire.size() < count)
		{
			auto n = peer.receive(chunk);
			REQUIRE(n);
			REQUIRE(*n > 0);
			wire.append(chunk.data(), *n);
		}
		CHECK(wire == "abcdefghijklmnop");
	}

	SECTION("send: error after peer reset")
	{
		REQUIRE(peer.set_option(pal::net::linger{true, 0s}));
		peer_holder.reset();

		std::array<std::byte, 64> receive_storage{}, send_storage{};
		task receive_task{receive_storage}, send_task{send_storage};

		bool reset = false;
		socket->start_receive(receive_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			reset = true;
		});
		run_until(*loop, [&] { return reset; });

		int failed = 0;
		socket->start_send(send_task.borrow(), [&](task_ptr &&, send_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			++failed;
		});
		run_until(*loop, [&] { return failed == 1; });
	}

	SECTION("both directions share one loop")
	{
		std::array<std::byte, 64> receive_storage{}, send_storage{};
		std::ranges::copy(std::as_bytes(std::span{payload}), send_storage.begin());
		task receive_task{receive_storage}, send_task{as_span(send_storage).first(payload.size())};

		int received = 0, sent = 0;
		socket->start_receive(receive_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			++received;
		});
		socket->start_send(send_task.borrow(), [&](task_ptr &&, send_result &&r) noexcept
		{
			REQUIRE(r);
			++sent;
		});

		std::array<char, 64> wire{};
		run_until(*loop, [&] { return sent == 1; });
		auto n = peer.receive(wire);
		REQUIRE(n);
		REQUIRE(peer.send(std::string_view{wire.data(), *n}));

		run_until(*loop, [&] { return received == 1; });
		CHECK(as_string_view(receive_task.span()) == payload);
	}

	SECTION("cancel: destruction with ops in flight")
	{
		std::array<std::byte, 64> receive_storage{};
		task receive_task{receive_storage};

		int canceled = 0;
		std::error_code ec{};

		std::optional<async_socket> doomed{std::move(*socket)};
		doomed->start_receive(receive_task.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			ec = r.error();
			CHECK(loop->stats().io_in_flight == 0);
			++canceled;
		});
		doomed.reset();

		CHECK(canceled == 0);
		CHECK(loop->stats().io_in_flight == 1);
		auto n = loop->settle();
		REQUIRE(n);
		CHECK(*n == 1);
		CHECK(canceled == 1);
		CHECK(ec == std::errc::operation_canceled);
		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("cancel: move-assign cancels the replaced socket's armed receive")
	{
		std::array<std::byte, 64> storage{};
		task t{storage};

		int canceled = 0;
		std::error_code ec{};
		socket->start_receive(t.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			ec = r.error();
			++canceled;
		});

		auto [replacement_local, replacement_peer] = make_connected_pair<TestType>();
		auto replacement = loop->make_handle(std::move(replacement_local));
		REQUIRE(replacement);
		*socket = std::move(*replacement);

		REQUIRE(loop->settle());
		CHECK(canceled == 1);
		CHECK(ec == std::errc::operation_canceled);
		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("cancel: destruction with a partially sent op")
	{
		REQUIRE(socket->set_option(pal::net::send_buffer_size{4096}));

		std::vector<std::byte> storage(size_t{1024} * 1024);
		task t{storage};

		std::optional<async_socket> doomed{std::move(*socket)};

		int canceled = 0;
		std::error_code ec{};
		doomed->start_send(t.borrow(), [&](task_ptr &&, send_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			ec = r.error();
			++canceled;
		});

		REQUIRE(loop->run_once());
		CHECK(canceled == 0);
		CHECK(loop->stats().io_in_flight == 1);

		doomed.reset();
		REQUIRE(loop->settle());
		CHECK(canceled == 1);
		CHECK(ec == std::errc::operation_canceled);
		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("cancel: destruction overtakes a completed receive")
	{
		std::array<std::byte, 64> first_storage{}, second_storage{};
		task first{first_storage}, second{second_storage};

		std::optional<async_socket> doomed{std::move(*socket)};

		int received = 0, canceled = 0;
		std::error_code ec{};

		doomed->start_receive(first.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE(r);
			doomed.reset();
			++received;
		});
		doomed->start_receive(second.borrow(), [&](task_ptr &&, receive_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			ec = r.error();
			++canceled;
		});

		REQUIRE(peer.send(payload));
		run_until(*loop, [&] { return received == 1 && canceled == 1; });

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

		int received = 0;
		socket->start_receive(std::move(t), [&](task_ptr &&p, receive_result &&r) noexcept
		{
			REQUIRE(r);
			CHECK(as_string_view(p->span()) == payload);
			++received;
		});

		REQUIRE(peer.send(payload));
		run_until(*loop, [&] { return received == 1; });

		auto again = pool.try_acquire();
		REQUIRE(again);
		CHECK(again.get() == acquired);
		CHECK(again->span().size() == full_span_size);
	}
}

} // namespace
