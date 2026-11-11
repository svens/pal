#include <pal/async/socket_acceptor.hpp>
#include <pal/async/task_pool.hpp>
#include <pal/net/socket_option.hpp>
#include <pal/net/test.hpp>
#include <pal/test.hpp>
#include <pal/version.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <array>
#include <chrono>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using namespace pal::async;
using namespace pal_test;
using namespace std::chrono_literals;

using tcp = pal::net::ip::tcp;
using sync_acceptor = pal::net::basic_socket_acceptor<tcp>;
using sync_socket = pal::net::basic_stream_socket<tcp>;
using async_acceptor = pal::async::handle<sync_acceptor>;
using accept_result = pal::result<async_acceptor::accept_result>;

using pal::net::ip::port_type;

template <typename Fixture>
sync_acceptor make_listener ()
{
	auto acceptor = Fixture::make_acceptor();
	REQUIRE(acceptor);
	REQUIRE(acceptor->bind(Fixture::loopback_endpoint()));
	REQUIRE(acceptor->listen());
	return std::move(*acceptor);
}

template <typename Fixture>
sync_socket connect_to (const tcp::endpoint &endpoint)
{
	auto client = Fixture::make_socket();
	REQUIRE(client);
	REQUIRE(client->connect(endpoint));
	return std::move(*client);
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

using window = std::array<std::byte, async_acceptor::accept_window_size>;

void check_accepted (async_acceptor::accept_result &accepted, sync_socket &client, std::string_view payload)
{
	auto client_endpoint = client.local_endpoint();
	REQUIRE(client_endpoint);
	CHECK(accepted.remote_endpoint == *client_endpoint);

	auto remote = accepted.socket.remote_endpoint();
	REQUIRE(remote);
	CHECK(*remote == *client_endpoint);

	REQUIRE(accepted.socket.set_option(pal::net::non_blocking_io{false}));
	REQUIRE(client.send(payload));
	std::array<char, 64> wire{};
	auto n = accepted.socket.receive(wire);
	REQUIRE(n);
	CHECK(std::string_view{wire.data(), *n} == payload);
}

TEMPLATE_TEST_CASE("async/socket_acceptor", "", tcp_v4, tcp_v6)
{
	auto loop = make_loop();
	REQUIRE(loop);

	auto listener = make_listener<TestType>();
	auto endpoint = listener.local_endpoint();
	REQUIRE(endpoint);

	auto acceptor = loop->make_handle(std::move(listener));
	REQUIRE(acceptor);

	const auto payload = case_name();

	SECTION("make_handle: curated re-exports")
	{
		auto acceptor_endpoint = acceptor->local_endpoint();
		REQUIRE(acceptor_endpoint);
		CHECK(*acceptor_endpoint == *endpoint);
		CHECK(acceptor_endpoint->port() != port_type::unspecified);
		CHECK(has_expected_family<TestType>(acceptor_endpoint->address()));

		CHECK(static_cast<bool>(acceptor->native_socket()));

		const pal::net::receive_buffer_size buffer_size{256 * 1024};
		REQUIRE(acceptor->set_option(buffer_size));
		pal::net::receive_buffer_size confirm{};
		REQUIRE(acceptor->get_option(confirm));
		CHECK(confirm.value() >= 256 * 1024);
	}

	SECTION("make_handle: dead socket")
	{
		auto dead = make_listener<TestType>();
		close_native_handle(dead);

		auto h = loop->make_handle(std::move(dead));
		REQUIRE_FALSE(h);
		CHECK(h.error() == std::errc::bad_file_descriptor);
	}

	SECTION("accept: armed before connect")
	{
		window storage{};
		task t{storage};

		std::optional<async_acceptor::accept_result> accepted;
		acceptor->start_accept(t.borrow(), [&](task_ptr &&, accept_result &&r) noexcept
		{
			REQUIRE(r);
			CHECK(loop->stats().io_in_flight == 0);
			accepted.emplace(std::move(*r));
		});
		CHECK(loop->stats().io_in_flight == 1);
		REQUIRE(loop->run_once());
		CHECK_FALSE(accepted);

		auto client = connect_to<TestType>(*endpoint);
		run_until(*loop, [&] { return accepted.has_value(); });
		check_accepted(*accepted, client, payload);
	}

	SECTION("accept: connection queued before arming")
	{
		auto client = connect_to<TestType>(*endpoint);

		window storage{};
		task t{storage};

		std::optional<async_acceptor::accept_result> accepted;
		acceptor->start_accept(t.borrow(), [&](task_ptr &&, accept_result &&r) noexcept
		{
			REQUIRE(r);
			accepted.emplace(std::move(*r));
		});
		run_until(*loop, [&] { return accepted.has_value(); });
		check_accepted(*accepted, client, payload);
	}

	SECTION("accept: parked accept resumes on the next connection")
	{
		std::array<window, 2> storage{};
		task first{storage[0]}, second{storage[1]};

		std::optional<async_acceptor::accept_result> first_accepted, second_accepted;
		acceptor->start_accept(first.borrow(), [&](task_ptr &&, accept_result &&r) noexcept
		{
			REQUIRE(r);
			first_accepted.emplace(std::move(*r));
		});
		acceptor->start_accept(second.borrow(), [&](task_ptr &&, accept_result &&r) noexcept
		{
			REQUIRE(r);
			second_accepted.emplace(std::move(*r));
		});

		auto client_a = connect_to<TestType>(*endpoint);
		run_until(*loop, [&] { return first_accepted.has_value(); });
		REQUIRE(loop->run_once());
		CHECK_FALSE(second_accepted);
		CHECK(loop->stats().io_in_flight == 1);

		auto client_b = connect_to<TestType>(*endpoint);
		run_until(*loop, [&] { return second_accepted.has_value(); });
		CHECK(loop->stats().io_in_flight == 0);
		check_accepted(*second_accepted, client_b, payload);
	}

	SECTION("accept: pending accepts complete in start order")
	{
		constexpr size_t count = 3;
		std::array<window, count> storage{};
		std::array<task, count> tasks;
		std::array<size_t, count> order{};
		std::array<std::optional<async_acceptor::accept_result>, count> accepted;
		size_t completed = 0;

		for (size_t i = 0; i < count; ++i)
		{
			tasks[i].span(storage[i]);
			acceptor->start_accept(tasks[i].borrow(), [&, i](task_ptr &&, accept_result &&r) noexcept
			{
				REQUIRE(r);
				order[completed] = i;
				accepted[completed].emplace(std::move(*r));
				++completed;
			});
		}

		std::vector<sync_socket> clients;
		clients.reserve(count);
		for (size_t i = 0; i < count; ++i)
		{
			clients.push_back(connect_to<TestType>(*endpoint));
		}

		run_until(*loop, [&] { return completed == count; });
		CHECK(order == std::array<size_t, count>{0, 1, 2});
		CHECK(loop->stats().io_in_flight == 0);

		for (auto &a: accepted)
		{
			auto remote = a->socket.remote_endpoint();
			REQUIRE(remote);
			CHECK(*remote == a->remote_endpoint);
		}
	}

	SECTION("accept: more pending accepts than one drain quantum")
	{
		auto quantum_loop = make_loop({.drain_batch = 1});
		REQUIRE(quantum_loop);
		auto quantum_acceptor = quantum_loop->make_handle(make_listener<TestType>());
		REQUIRE(quantum_acceptor);
		auto quantum_endpoint = quantum_acceptor->local_endpoint();
		REQUIRE(quantum_endpoint);

		constexpr size_t count = 3;
		std::vector<sync_socket> clients;
		clients.reserve(count);
		for (size_t i = 0; i < count; ++i)
		{
			clients.push_back(connect_to<TestType>(*quantum_endpoint));
		}

		std::array<window, count> storage{};
		std::array<task, count> tasks;
		size_t accepted = 0;
		for (size_t i = 0; i < count; ++i)
		{
			tasks[i].span(storage[i]);
			quantum_acceptor->start_accept(tasks[i].borrow(), [&](task_ptr &&, accept_result &&r) noexcept
			{
				REQUIRE(r);
				++accepted;
			});
		}

		run_until(*quantum_loop, [&] { return accepted == count; });
		CHECK(quantum_loop->stats().io_in_flight == 0);
	}

	SECTION("accept: task re-armed from its handler")
	{
		window storage{};
		task t{storage};

		size_t accepted = 0;
		auto client_a = connect_to<TestType>(*endpoint);
		auto client_b = connect_to<TestType>(*endpoint);

		auto handler = [&](task_ptr &&p, accept_result &&r) noexcept
		{
			REQUIRE(r);
			if (++accepted == 1)
			{
				acceptor->start_accept(std::move(p), [&](task_ptr &&, accept_result &&again) noexcept
				{
					REQUIRE(again);
					++accepted;
				});
			}
		};
		acceptor->start_accept(t.borrow(), handler);

		run_until(*loop, [&] { return accepted == 2; });
		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("cancel: destruction with a pending accept")
	{
		window storage{};
		task t{storage};

		int canceled = 0;
		std::error_code ec{};

		std::optional<async_acceptor> doomed{std::move(*acceptor)};
		doomed->start_accept(t.borrow(), [&](task_ptr &&, accept_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			ec = r.error();
			CHECK(loop->stats().io_in_flight == 0);
			++canceled;
		});
		REQUIRE(loop->run_once());
		CHECK(canceled == 0);
		CHECK(loop->stats().io_in_flight == 1);

		doomed.reset();
		auto n = loop->settle();
		REQUIRE(n);
		CHECK(*n == 1);
		CHECK(canceled == 1);
		CHECK(ec == std::errc::operation_canceled);
		CHECK(loop->stats().io_in_flight == 0);
	}

	SECTION("cancel: destruction overtakes a completed accept, closing its connection")
	{
		std::array<window, 2> storage{};
		task first{storage[0]}, second{storage[1]};

		std::optional<async_acceptor> doomed{std::move(*acceptor)};

		int accepted = 0, canceled = 0;
		std::error_code ec{};

		doomed->start_accept(first.borrow(), [&](task_ptr &&, accept_result &&r) noexcept
		{
			REQUIRE(r);
			doomed.reset();
			++accepted;
		});
		doomed->start_accept(second.borrow(), [&](task_ptr &&, accept_result &&r) noexcept
		{
			REQUIRE_FALSE(r);
			ec = r.error();
			++canceled;
		});

		auto client_a = connect_to<TestType>(*endpoint);
		auto client_b = connect_to<TestType>(*endpoint);
		run_until(*loop, [&] { return accepted == 1 && canceled == 1; });

		CHECK(ec == std::errc::operation_canceled);
		CHECK(loop->stats().io_in_flight == 0);

		REQUIRE(client_b.set_option(pal::net::receive_timeout{5s}));
		std::array<char, 16> wire{};
		auto n = client_b.receive(wire);
		if (n)
		{
			CHECK(*n == 0);
		}
		else
		{
			CHECK(n.error() == std::errc::connection_reset);
		}
	}

	SECTION("pool-managed task round trip")
	{
		task_pool<2> pool;

		auto t = pool.try_acquire();
		REQUIRE(t);
		REQUIRE(t->span().size() >= async_acceptor::accept_window_size);
		task *acquired = t.get();

		int accepted = 0;
		acceptor->start_accept(std::move(t), [&](task_ptr &&, accept_result &&r) noexcept
		{
			REQUIRE(r);
			++accepted;
		});

		auto client = connect_to<TestType>(*endpoint);
		run_until(*loop, [&] { return accepted == 1; });

		auto again = pool.try_acquire();
		REQUIRE(again);
		CHECK(again.get() == acquired);
	}
}

TEST_CASE("async/socket_acceptor start_accept contracts")
{
	if constexpr (pal::build == pal::build_type::debug)
	{
		SECTION("window below accept_window_size")
		{
			auto msg = pal_test::require_terminate([]
			{
				auto loop = make_loop().value();
				auto h = loop.make_handle(make_listener<tcp_v4>()).value();
				std::array<std::byte, async_acceptor::accept_window_size - 1> storage{};
				task t{storage};
				h.start_accept(t.borrow(), [](task_ptr &&, accept_result &&) noexcept {});
			});
			CHECK(msg.contains("accept_window_size"));
		}
	}
}

} // namespace
