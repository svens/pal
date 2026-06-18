#include <pal/crypto/session.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <ranges>
#include <thread>
#include <vector>

namespace
{

using pal::const_buffer;
using pal::make_unexpected;
using pal::mutable_buffer;
using pal::result;
using pal_test::connected_pair;

using namespace pal::crypto;
namespace cert = pal_test::cert;

struct loopback_device //{{{1
{
	// receive() reads
	std::vector<std::byte> &buffer;

	// send() writes
	std::vector<std::byte> &sink;

	std::error_code inject_error{};

	result<size_t> send (const_buffer auto const &buf) noexcept
	try
	{
		if (inject_error)
		{
			return pal::unexpected(std::exchange(inject_error, {}));
		}
		auto bytes = std::as_bytes(std::span{buf});
		sink.append_range(bytes);
		return bytes.size();
	}
	catch (...)
	{
		return make_unexpected(std::errc::not_enough_memory);
	}

	result<size_t> receive (mutable_buffer auto &&buf) noexcept
	{
		if (inject_error)
		{
			return pal::unexpected(std::exchange(inject_error, {}));
		}
		if (buffer.empty())
		{
			return pal::unexpected(make_error_code(secure_channel_errc::closed));
		}
		auto out = std::as_writable_bytes(std::span{buf});
		const size_t n = (std::min)(out.size(), buffer.size());
		std::copy_n(buffer.begin(), n, out.begin());
		buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n));
		return n;
	}
};
static_assert(io_device<loopback_device, std::span<const std::byte>, std::span<std::byte>>);

struct loopback_pair //{{{1
{
	std::vector<std::byte> c2s{};
	std::vector<std::byte> s2c{};
	loopback_device client{.buffer = s2c, .sink = c2s};
	loopback_device server{.buffer = c2s, .sink = s2c};
};

struct blocking_pair //{{{1
{
	std::mutex mutex{};
	std::condition_variable cv{};
	std::vector<std::byte> c2s{};
	std::vector<std::byte> s2c{};
	bool closed = false;

	void close () noexcept
	{
		{
			const std::scoped_lock lock{mutex};
			closed = true;
		}
		cv.notify_all();
	}

	struct half_device
	{
		blocking_pair &pair;
		std::vector<std::byte> &buffer;
		std::vector<std::byte> &sink;
		std::error_code inject_send_error{};
		std::error_code inject_receive_error{};

		result<size_t> send (const_buffer auto const &buf) noexcept
		try
		{
			if (inject_send_error)
			{
				return pal::unexpected(std::exchange(inject_send_error, {}));
			}
			auto bytes = std::as_bytes(std::span{buf});
			{
				const std::scoped_lock lock{pair.mutex};
				sink.append_range(bytes);
			}
			pair.cv.notify_all();
			return bytes.size();
		}
		catch (...)
		{
			return make_unexpected(std::errc::not_enough_memory);
		}

		result<size_t> receive (mutable_buffer auto &&buf) noexcept
		{
			if (inject_receive_error)
			{
				return pal::unexpected(std::exchange(inject_receive_error, {}));
			}
			auto out = std::as_writable_bytes(std::span{buf});
			std::unique_lock lock{pair.mutex};
			pair.cv.wait(lock, [&] { return !buffer.empty() || pair.closed; });
			if (buffer.empty())
			{
				return pal::unexpected(make_error_code(secure_channel_errc::closed));
			}
			const size_t n = (std::min)(out.size(), buffer.size());
			std::copy_n(buffer.begin(), n, out.begin());
			buffer.erase(buffer.begin(), buffer.begin() + n);
			return n;
		}
	};

	half_device client_device () noexcept
	{
		return {.pair = *this, .buffer = s2c, .sink = c2s};
	}

	half_device server_device () noexcept
	{
		return {.pair = *this, .buffer = c2s, .sink = s2c};
	}
};
static_assert(io_device<blocking_pair::half_device, std::span<const std::byte>, std::span<std::byte>>);

connected_pair<connected_channel> pump_handshake ( //{{{1
	handshake_channel &client_handshake,
	handshake_channel &server_handshake)
{
	constexpr size_t buf_cap = 16UL * 1024;

	struct wire
	{
		std::array<std::byte, buf_cap> data{};
		size_t size = 0;
	};
	wire c2s;
	wire s2c;

	std::optional<connected_channel> client_channel;
	std::optional<connected_channel> server_channel;

	auto step = [&] (handshake_channel &handshake, std::optional<connected_channel> &channel, wire &in, wire &out)
	{
		if (channel || !handshake)
		{
			return;
		}

		auto r = handshake.step(std::span{in.data}.first(in.size), std::span{out.data}.subspan(out.size));
		REQUIRE(r);

		std::shift_left(in.data.begin(), in.data.begin() + in.size, static_cast<std::ptrdiff_t>(r->consumed));
		in.size -= r->consumed;
		out.size += r->produced;

		if (r->connected)
		{
			channel.emplace(std::move(*r->connected));
		}
	};

	for (int i = 0; i < 32 && !(client_channel && server_channel); ++i)
	{
		step(client_handshake, client_channel, s2c, c2s);
		step(server_handshake, server_channel, c2s, s2c);
	}

	REQUIRE(client_channel);
	REQUIRE(server_channel);
	return {.server = std::move(*server_channel), .client = std::move(*client_channel)};
}

connected_pair<session> make_session_pair ( //{{{1
	const stream_acceptor &server_factory,
	const stream_connector &client_factory,
	const connector_handshake_options &opts = {.peer_name = "server.pal.alt.ee"})
{
	auto client_handshake = client_factory.connect(opts);
	REQUIRE(client_handshake);
	auto server_handshake = server_factory.accept();
	REQUIRE(server_handshake);

	auto [server_channel, client_channel] = pump_handshake(*client_handshake, *server_handshake);

	auto client_session = session::from(std::move(client_channel));
	REQUIRE(client_session);
	auto server_session = session::from(std::move(server_channel));
	REQUIRE(server_session);

	return {.server = std::move(*server_session), .client = std::move(*client_session)};
}

struct good_device //{{{1
{
	result<size_t> send (std::span<const std::byte>) noexcept
	{
		return 0;
	}
	result<size_t> receive (std::span<std::byte>) noexcept
	{
		return 0;
	}
};
static_assert(io_device<good_device, std::span<const std::byte>, std::span<std::byte>>);

struct no_noexcept_device //{{{1
{
	result<size_t> send (std::span<const std::byte>)
	{
		return 0;
	}
	result<size_t> receive (std::span<std::byte>)
	{
		return 0;
	}
};
static_assert(!io_device<no_noexcept_device, std::span<const std::byte>, std::span<std::byte>>);

struct wrong_return_device //{{{1
{
	void send (std::span<const std::byte>) noexcept
	{
	}
	void receive (std::span<std::byte>) noexcept
	{
	}
};
static_assert(!io_device<wrong_return_device, std::span<const std::byte>, std::span<std::byte>>);

// }}}1

TEST_CASE("crypto/session") //{{{1
{
	auto chain = cert::load_pkcs12(cert::pkcs12_data);
	auto key = chain.front().private_key();
	REQUIRE(key);
	const std::array roots{cert::load_pem(cert::ca)};

	auto server_factory = stream_acceptor::make({.certificate_chain = chain, .private_key = *key});
	REQUIRE(server_factory);

	auto client_factory = stream_connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(client_factory);

	auto [server, client] = make_session_pair(*server_factory, *client_factory);
	loopback_pair io;

	SECTION("operator_bool")
	{
		session empty;
		CHECK_FALSE(empty);
		CHECK(client);

		session moved = std::move(client);
		CHECK_FALSE(client);
		CHECK(moved);
	}

	SECTION("move_semantics")
	{
		session moved = std::move(client);
		CHECK_FALSE(client);
		REQUIRE(moved);

		const auto msg = pal_test::case_name();
		REQUIRE(moved.send(io.client, msg));

		std::array<char, 256> buf{};
		auto receive = server.receive(io.server, buf);
		REQUIRE(receive);
		CHECK(std::string_view{buf.data(), *receive} == msg);
	}

	SECTION("move_assign")
	{
		session other;
		other = std::move(client);
		CHECK_FALSE(client);
		CHECK(other);
	}

	SECTION("run_handshake")
	{
		blocking_pair wire;
		auto client_device = wire.client_device();
		auto server_device = wire.server_device();

		result<session> server_session = make_unexpected(std::errc::not_connected);

		// clang-format off
		std::thread server_thread([&]
		{
			auto server_handshake = server_factory->accept();
			if (server_handshake)
			{
				server_session = session::run_handshake(server_device, *server_handshake);
			}
		});
		// clang-format on

		auto client_handshake = client_factory->connect({.peer_name = "server.pal.alt.ee"});
		REQUIRE(client_handshake);
		auto client_session = session::run_handshake(client_device, *client_handshake);

		if (!client_session)
		{
			wire.close();
		}
		server_thread.join();

		REQUIRE(client_session);
		REQUIRE(server_session);

		const auto msg = pal_test::case_name();
		REQUIRE(client_session->send(client_device, msg));
		std::array<char, 256> buf{};
		auto receive = server_session->receive(server_device, buf);
		REQUIRE(receive);
		CHECK(std::string_view{buf.data(), *receive} == msg);
	}

	SECTION("round_trip")
	{
		const auto msg = pal_test::case_name();
		auto send = client.send(io.client, msg);
		REQUIRE(send);
		CHECK(*send == msg.size());

		std::array<char, 256> buf{};
		auto receive = server.receive(io.server, buf);
		REQUIRE(receive);
		CHECK(*receive == msg.size());
		CHECK(std::string_view{buf.data(), *receive} == msg);
	}

	SECTION("close_notify")
	{
		SECTION("client_closes")
		{
			REQUIRE(client.close_notify(io.client));

			std::array<char, 256> buf{};
			auto receive = server.receive(io.server, buf);
			REQUIRE_FALSE(receive);
			CHECK(receive.error() == secure_channel_errc::closed);
		}

		SECTION("both_sides")
		{
			REQUIRE(client.close_notify(io.client));
			auto receive = server.receive(io.server, std::array<char, 256>{});
			REQUIRE_FALSE(receive);
			CHECK(receive.error() == secure_channel_errc::closed);

			REQUIRE(server.close_notify(io.server));
			receive = client.receive(io.client, std::array<char, 256>{});
			REQUIRE_FALSE(receive);
			CHECK(receive.error() == secure_channel_errc::closed);
		}
	}

	SECTION("receive_small_buffer")
	{
		const std::vector<std::byte> msg(4096, std::byte{0x42});
		REQUIRE(server.send(io.server, msg));

		std::vector<std::byte> received;
		std::array<std::byte, 128> buf{};
		while (received.size() < msg.size())
		{
			auto receive = client.receive(io.client, buf);
			REQUIRE(receive);
			received.append_range(std::span{buf}.first(*receive));
		}
		CHECK(received == msg);
	}

	SECTION("send_large_payload")
	{
		const std::vector<std::byte> msg(40UL * 1024, std::byte{0x42});
		auto send = client.send(io.client, msg);
		REQUIRE(send);
		CHECK(*send == msg.size());

		std::vector<std::byte> received;
		std::array<std::byte, 4096> buf{};
		while (received.size() < msg.size())
		{
			auto receive = server.receive(io.server, buf);
			REQUIRE(receive);
			received.append_range(std::span{buf}.first(*receive));
		}
		CHECK(received == msg);
	}

	SECTION("peer_certificate")
	{
		auto server_cert = client.peer_certificate();
		REQUIRE(server_cert);
		CHECK_FALSE(server_cert->is_null());
		CHECK(server_cert->common_name() == "pal.alt.ee");

		auto client_cert = server.peer_certificate();
		REQUIRE(client_cert);
		CHECK(client_cert->is_null());
	}

	SECTION("selected_protocol")
	{
		const std::array<std::string_view, 2> server_protocols{"h2", "http/1.1"};
		auto server_factory = stream_acceptor::make({
			.certificate_chain = chain,
			.private_key = *key,
			.supported_protocols = server_protocols,
		});
		REQUIRE(server_factory);

		const std::array<std::string_view, 1> client_protocols{"http/1.1"};
		auto client_factory = stream_connector::make({
			.trusted_roots = roots,
			.use_system_trust = false,
			.supported_protocols = client_protocols,
		});
		REQUIRE(client_factory);

		auto [server, client] = make_session_pair(*server_factory, *client_factory);
		CHECK(client.selected_protocol() == "http/1.1");
		CHECK(server.selected_protocol() == "http/1.1");
	}

	SECTION("run_handshake/not_enough_memory")
	{
		auto client_handshake = client_factory->connect({.peer_name = "server.pal.alt.ee"});
		REQUIRE(client_handshake);

		const pal_test::bad_alloc_once x;
		auto client_session = session::run_handshake(io.client, *client_handshake);
		REQUIRE_FALSE(client_session);
		CHECK(client_session.error() == std::errc::not_enough_memory);
	}

	SECTION("run_handshake/send_error")
	{
		blocking_pair wire;
		auto client_device = wire.client_device();
		client_device.inject_send_error = make_error_code(std::errc::connection_reset);

		auto client_handshake = client_factory->connect({.peer_name = "server.pal.alt.ee"});
		REQUIRE(client_handshake);
		auto client_session = session::run_handshake(client_device, *client_handshake);
		REQUIRE_FALSE(client_session);
		CHECK(client_session.error() == std::errc::connection_reset);
	}

	SECTION("run_handshake/receive_error")
	{
		blocking_pair wire;
		auto client_device = wire.client_device();
		client_device.inject_receive_error = make_error_code(std::errc::connection_reset);

		auto client_handshake = client_factory->connect({.peer_name = "server.pal.alt.ee"});
		REQUIRE(client_handshake);
		auto client_session = session::run_handshake(client_device, *client_handshake);
		REQUIRE_FALSE(client_session);
		CHECK(client_session.error() == std::errc::connection_reset);
	}

	SECTION("send/after_close_notify")
	{
		REQUIRE(client.close_notify(io.client));
		auto send = client.send(io.client, pal_test::case_name());
		REQUIRE_FALSE(send);
		CHECK(send.error() == secure_channel_errc::closed);
	}

	SECTION("send/transport_error")
	{
		io.client.inject_error = make_error_code(std::errc::connection_reset);
		auto send = client.send(io.client, pal_test::case_name());
		REQUIRE_FALSE(send);
		CHECK(send.error() == std::errc::connection_reset);
	}

	SECTION("receive/transport_error")
	{
		io.server.inject_error = make_error_code(std::errc::connection_reset);
		std::array<char, 256> buf{};
		auto receive = server.receive(io.server, buf);
		REQUIRE_FALSE(receive);
		CHECK(receive.error() == std::errc::connection_reset);
	}

	SECTION("receive/decrypt_error")
	{
		REQUIRE(client.send(io.client, pal_test::case_name()));
		io.c2s.back() ^= std::byte{0xff};
		std::array<char, 256> buf{};
		auto receive = server.receive(io.server, buf);
		REQUIRE_FALSE(receive);
		CHECK(receive.error() == secure_channel_errc::decrypt_failed);
	}

	SECTION("close_notify/transport_error")
	{
		io.client.inject_error = make_error_code(std::errc::connection_reset);
		auto close_notify = client.close_notify(io.client);
		REQUIRE_FALSE(close_notify);
		CHECK(close_notify.error() == std::errc::connection_reset);
	}
}

// }}}1

} // namespace
