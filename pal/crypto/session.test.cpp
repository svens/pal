#include <pal/crypto/session.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
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

struct stream //{{{1
{
	using acceptor = stream_acceptor;
	using connector = stream_connector;
	static constexpr transport transport_value = transport::stream;
};

struct datagram //{{{1
{
	using acceptor = datagram_acceptor;
	using connector = datagram_connector;
	static constexpr transport transport_value = transport::datagram;
};

template <transport Transport>
struct loopback_device //{{{1
{
	static constexpr transport transport_value = Transport;

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
			if constexpr (Transport == transport::datagram)
			{
				return pal::unexpected(make_error_code(std::errc::resource_unavailable_try_again));
			}
			else
			{
				return pal::unexpected(make_error_code(secure_channel_errc::closed));
			}
		}
		auto out = std::as_writable_bytes(std::span{buf});
		const size_t n = (std::min)(out.size(), buffer.size());
		std::copy_n(buffer.begin(), n, out.begin());
		buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n));
		return n;
	}
};
static_assert(io_device<loopback_device<transport::stream>, std::span<const std::byte>, std::span<std::byte>>);
static_assert(io_device<loopback_device<transport::datagram>, std::span<const std::byte>, std::span<std::byte>>);

template <transport Transport>
struct loopback_pair //{{{1
{
	std::vector<std::byte> c2s{};
	std::vector<std::byte> s2c{};
	loopback_device<Transport> client{.buffer = s2c, .sink = c2s};
	loopback_device<Transport> server{.buffer = c2s, .sink = s2c};
};

template <transport Transport>
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
		static constexpr transport transport_value = Transport;

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
static_assert(
	io_device<blocking_pair<transport::stream>::half_device, std::span<const std::byte>, std::span<std::byte>>
);
static_assert(
	io_device<blocking_pair<transport::datagram>::half_device, std::span<const std::byte>, std::span<std::byte>>
);

connected_pair<connected_channel>
pump_handshake (handshake_channel &client_handshake, handshake_channel &server_handshake) //{{{1
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

template <typename Acceptor>
[[nodiscard]] auto server_accept (const Acceptor &acceptor) //{{{1
{
	if constexpr (Acceptor::transport_value == transport::datagram)
	{
		return acceptor.accept(peer_token::none);
	}
	else
	{
		return acceptor.accept();
	}
}

template <typename Traits>
connected_pair<session> make_session_pair ( //{{{1
	const typename Traits::acceptor &server_factory,
	const typename Traits::connector &client_factory,
	const connector_handshake_options &opts = {.peer_name = "server.pal.alt.ee"})
{
	auto client_handshake = client_factory.connect(opts);
	REQUIRE(client_handshake);
	auto server_handshake = server_accept(server_factory);
	REQUIRE(server_handshake);

	auto [server_channel, client_channel] = pump_handshake(*client_handshake, *server_handshake);

	auto client_session = session::from(std::move(client_channel), Traits::transport_value);
	REQUIRE(client_session);
	auto server_session = session::from(std::move(server_channel), Traits::transport_value);
	REQUIRE(server_session);

	return {.server = std::move(*server_session), .client = std::move(*client_session)};
}

// concept checks — namespace scope because local classes can't have template members {{{1

struct good_device
{
	[[maybe_unused]] static constexpr transport transport_value = transport::stream;

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

struct no_noexcept_device
{
	[[maybe_unused]] static constexpr transport transport_value = transport::stream;

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

struct wrong_return_device
{
	[[maybe_unused]] static constexpr transport transport_value = transport::stream;

	void send (std::span<const std::byte>) noexcept
	{
	}
	void receive (std::span<std::byte>) noexcept
	{
	}
};
static_assert(!io_device<wrong_return_device, std::span<const std::byte>, std::span<std::byte>>);

struct missing_transport_device
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
static_assert(!io_device<missing_transport_device, std::span<const std::byte>, std::span<std::byte>>);

// }}}1

TEMPLATE_TEST_CASE("crypto/session", "", stream, datagram) //{{{1
{
	auto chain = cert::load_pkcs12(cert::pkcs12_data);
	auto key = chain.front().private_key();
	REQUIRE(key);
	const std::array roots{cert::load_pem(cert::ca)};

	auto server_factory = TestType::acceptor::make({.certificate_chain = chain, .private_key = *key});
	REQUIRE(server_factory);

	auto client_factory = TestType::connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(client_factory);

	auto [server, client] = make_session_pair<TestType>(*server_factory, *client_factory);
	loopback_pair<TestType::transport_value> io;

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
		blocking_pair<TestType::transport_value> wire;
		auto client_device = wire.client_device();
		auto server_device = wire.server_device();

		result<session> server_session = make_unexpected(std::errc::not_connected);

		// clang-format off
		std::thread server_thread([&]
		{
			if (auto server_handshake = server_accept(*server_factory))
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
		const auto check_close_error = [] (const std::error_code &ec)
		{
			// After a close, the peer's next receive errors out. Stream and OpenSSL-datagram emit a real
			// close_notify so the peer reports `closed`; SChannel cannot generate a DTLS close_notify, so
			// its datagram close is a no-op and the peer just sees no datagram (try_again on the loopback).
			// Assert the exact per-backend outcome rather than accepting either, so a backend that silently
			// stops emitting close_notify is caught.
			if constexpr (
				TestType::transport_value == transport::datagram && pal::os == pal::os_type::windows)
			{
				CHECK(ec == std::errc::resource_unavailable_try_again);
			}
			else
			{
				CHECK(ec == secure_channel_errc::closed);
			}
		};

		SECTION("client_closes")
		{
			REQUIRE(client.close_notify(io.client));

			std::array<char, 256> buf{};
			auto receive = server.receive(io.server, buf);
			REQUIRE_FALSE(receive);
			check_close_error(receive.error());
		}

		SECTION("both_sides")
		{
			REQUIRE(client.close_notify(io.client));
			auto receive = server.receive(io.server, std::array<char, 256>{});
			REQUIRE_FALSE(receive);
			check_close_error(receive.error());

			REQUIRE(server.close_notify(io.server));
			receive = client.receive(io.client, std::array<char, 256>{});
			REQUIRE_FALSE(receive);
			check_close_error(receive.error());
		}
	}

	SECTION("receive_small_buffer")
	{
		// DTLS MTU constrains single-record size; 256 bytes is within any DTLS MTU.
		constexpr size_t msg_size = (TestType::transport_value == transport::stream) ? 4096 : 256;
		const std::vector<std::byte> msg(msg_size, std::byte{0x42});
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
		// TLS fragments across records; DTLS is limited to one record per send (within MTU).
		constexpr size_t msg_size = (TestType::transport_value == transport::stream) ? 40UL * 1024 : 256;
		const std::vector<std::byte> msg(msg_size, std::byte{0x42});
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
		auto sf = TestType::acceptor::make({
			.certificate_chain = chain,
			.private_key = *key,
			.supported_protocols = server_protocols,
		});
		REQUIRE(sf);

		const std::array<std::string_view, 1> client_protocols{"http/1.1"};
		auto cf = TestType::connector::make({
			.trusted_roots = roots,
			.use_system_trust = false,
			.supported_protocols = client_protocols,
		});
		REQUIRE(cf);

		auto [s, c] = make_session_pair<TestType>(*sf, *cf);
		CHECK(c.selected_protocol() == "http/1.1");

		if constexpr (TestType::transport_value == transport::datagram && pal::os == pal::os_type::windows)
		{
			// The SChannel DTLS server cannot introspect its own ALPN selection (negotiation still works on
			// the wire). Other backends/roles report it; accept either to hold across backends.
			CHECK_NOFAIL(s.selected_protocol() == "http/1.1");
		}
		else
		{
			CHECK(s.selected_protocol() == "http/1.1");
		}
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
		blocking_pair<TestType::transport_value> wire;
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
		blocking_pair<TestType::transport_value> wire;
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
		if constexpr (TestType::transport_value == transport::stream)
		{
			CHECK(receive.error() == secure_channel_errc::decrypt_failed);
		}
		else
		{
			// DTLS mandates silent discard of bad records (RFC 9147); the session loops
			// and requests more data, but no datagram is available.
			CHECK(receive.error() == std::errc::resource_unavailable_try_again);
		}
	}

	SECTION("close_notify/transport_error")
	{
		io.client.inject_error = make_error_code(std::errc::connection_reset);
		auto close_notify = client.close_notify(io.client);

		// Datagram diverges by backend: OpenSSL writes a close_notify and surfaces the transport
		// error; SChannel's no-op close writes nothing, so it succeeds. Accept either, but if it
		// failed it must be the injected error.
		if constexpr (TestType::transport_value == transport::datagram && pal::os == pal::os_type::windows)
		{
			if (!close_notify)
			{
				CHECK(close_notify.error() == std::errc::connection_reset);
			}
		}
		else
		{
			REQUIRE_FALSE(close_notify);
			CHECK(close_notify.error() == std::errc::connection_reset);
		}
	}
}

// }}}1

} // namespace
