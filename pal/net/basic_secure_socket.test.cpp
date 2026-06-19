#include <pal/net/basic_secure_socket.hpp>
#include <pal/net/ip/tcp.hpp>
#include <pal/net/ip/udp.hpp>
#include <pal/net/socket_option.hpp>
#include <pal/net/test.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <thread>

namespace
{

namespace net = pal::net;
namespace ip = net::ip;
using tcp = ip::tcp;
using udp = ip::udp;

namespace crypto = pal::crypto;
namespace cert = pal_test::cert;

using pal_test::connected_pair;

// clang-format off
constexpr crypto::connector_handshake_options default_opts
{
	.peer_name = "server.pal.alt.ee"
};
// clang-format on

// transport traits {{{1

struct stream
{
	using acceptor = crypto::stream_acceptor;
	using connector = crypto::stream_connector;
	using socket_type = tcp::socket;
	using secure_socket_type = tcp::secure_socket;
	static constexpr crypto::transport transport_value = crypto::transport::stream;

	struct context
	{
		tcp::acceptor acceptor;
		tcp::endpoint server_endpoint;
	};

	static context prepare ()
	{
		auto acceptor = net::make_socket_acceptor(tcp::v4).value();
		REQUIRE(acceptor.bind({ip::address_v4::loopback, ip::port_type::unspecified}));
		REQUIRE(acceptor.listen());
		auto endpoint = acceptor.local_endpoint().value();
		return {.acceptor = std::move(acceptor), .server_endpoint = endpoint};
	}

	static tcp::socket server_socket (context &ctx)
	{
		auto socket = ctx.acceptor.accept();
		REQUIRE(socket);
		return std::move(*socket);
	}

	static tcp::socket client_socket (context &ctx)
	{
		auto socket = net::make_stream_socket(tcp::v4).value();
		REQUIRE(socket.connect(ctx.server_endpoint));
		return socket;
	}
};

struct datagram
{
	using acceptor = crypto::datagram_acceptor;
	using connector = crypto::datagram_connector;
	using socket_type = udp::socket;
	using secure_socket_type = udp::secure_socket;
	static constexpr crypto::transport transport_value = crypto::transport::datagram;

	struct context
	{
		udp::socket server;
		udp::socket client;
	};

	static context prepare ()
	{
		auto server = net::make_datagram_socket(udp::v4).value();
		REQUIRE(server.bind({ip::address_v4::loopback, ip::port_type::unspecified}));
		auto endpoint = server.local_endpoint().value();

		auto client = net::make_datagram_socket(udp::v4).value();
		REQUIRE(client.connect(endpoint));
		endpoint = client.local_endpoint().value();

		REQUIRE(server.connect(endpoint));
		return {.server = std::move(server), .client = std::move(client)};
	}

	static udp::socket server_socket (context &ctx)
	{
		using namespace std::chrono_literals;
		REQUIRE(ctx.server.set_option(net::receive_timeout{100ms}));
		return std::move(ctx.server);
	}

	static udp::socket client_socket (context &ctx)
	{
		return std::move(ctx.client);
	}
};

// make_factories, make_connected_pair {{{1

template <typename Traits>
connected_pair<typename Traits::acceptor, typename Traits::connector> make_factories ()
{
	auto chain = cert::load_pkcs12(cert::pkcs12_data);
	auto key = chain.front().private_key();
	REQUIRE(key);
	const std::array roots{cert::load_pem(cert::ca)};

	auto server_factory = Traits::acceptor::make({.certificate_chain = chain, .private_key = *key});
	REQUIRE(server_factory);
	auto client_factory = Traits::connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(client_factory);

	return {.server = std::move(*server_factory), .client = std::move(*client_factory)};
}

template <typename Traits>
connected_pair<typename Traits::secure_socket_type> make_connected_pair (
	const typename Traits::acceptor &server_factory,
	const typename Traits::connector &client_factory,
	const crypto::connector_handshake_options &connect_opts = default_opts
)
{
	auto ctx = Traits::prepare();

	std::optional<typename Traits::secure_socket_type> server_socket;

	// clang-format off
	std::thread server_thread([&]
	{
		if (auto tls = net::make_secure_socket(Traits::server_socket(ctx), server_factory))
		{
			server_socket = std::move(*tls);
		}
	});
	// clang-format on

	auto client_socket = net::make_secure_socket(Traits::client_socket(ctx), client_factory, connect_opts);
	server_thread.join();

	REQUIRE(client_socket);
	REQUIRE(server_socket);
	return {.server = std::move(*server_socket), .client = std::move(*client_socket)};
}

// }}}1

TEMPLATE_TEST_CASE("net/basic_secure_socket", "", stream, datagram) //{{{1
{
	auto [server_factory, client_factory] = make_factories<TestType>();
	auto [server, client] = make_connected_pair<TestType>(server_factory, client_factory);

	SECTION("operator_bool")
	{
		CHECK(client);
		CHECK(server);

		typename TestType::secure_socket_type moved = std::move(client);
		CHECK_FALSE(client);
		CHECK(moved);

		std::ignore = moved.release();
		CHECK_FALSE(moved);
	}

	SECTION("move_semantics")
	{
		typename TestType::secure_socket_type moved = std::move(client);
		CHECK_FALSE(client);
		REQUIRE(moved);

		REQUIRE(moved.send(pal_test::case_name()));

		std::array<char, 256> buf{};
		auto receive = server.receive(buf);
		REQUIRE(receive);
		CHECK(std::string_view{buf.data(), *receive} == pal_test::case_name());
	}

	SECTION("move_assign")
	{
		server = std::move(client);
		CHECK_FALSE(client);
		CHECK(server);
	}

	SECTION("round_trip")
	{
		const auto msg = pal_test::case_name();
		auto send = client.send(msg);
		REQUIRE(send);
		CHECK(*send == msg.size());

		std::array<char, 256> buf{};
		auto receive = server.receive(buf);
		REQUIRE(receive);
		CHECK(*receive == msg.size());
		CHECK(std::string_view{buf.data(), *receive} == msg);
	}

	SECTION("close_notify")
	{
		SECTION("client_closes")
		{
			REQUIRE(client.close_notify());

			std::array<char, 256> buf{};
			auto receive = server.receive(buf);
			REQUIRE_FALSE(receive);
			CHECK(receive.error() == crypto::secure_channel_errc::closed);
		}

		SECTION("both_sides")
		{
			REQUIRE(client.close_notify());
			auto receive = server.receive(std::array<char, 256>{});
			REQUIRE_FALSE(receive);
			CHECK(receive.error() == crypto::secure_channel_errc::closed);

			REQUIRE(server.close_notify());
			receive = client.receive(std::array<char, 256>{});
			REQUIRE_FALSE(receive);
			CHECK(receive.error() == crypto::secure_channel_errc::closed);
		}
	}

	SECTION("receive_small_buffer")
	{
		const size_t msg_size = std::min(4096UL, client.max_message_size());
		const std::vector<std::byte> msg(msg_size, std::byte{0x42});
		REQUIRE(server.send(msg));

		std::vector<std::byte> received;
		std::array<std::byte, 128> buf{};
		while (received.size() < msg.size())
		{
			auto receive = client.receive(buf);
			REQUIRE(receive);
			received.append_range(std::span{buf}.first(*receive));
		}
		CHECK(received == msg);
	}

	SECTION("send_large_payload")
	{
		// TLS fragments large messages; DTLS is limited to one record per send (within MTU).
		const size_t msg_size = std::min(40UL * 1024, client.max_message_size());
		const std::vector<std::byte> msg(msg_size, std::byte{0x42});
		auto send = client.send(msg);
		REQUIRE(send);
		CHECK(*send == msg.size());

		std::vector<std::byte> received;
		std::array<std::byte, 4096> buf{};
		while (received.size() < msg.size())
		{
			auto receive = server.receive(buf);
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
		auto chain = cert::load_pkcs12(cert::pkcs12_data);
		auto key = chain.front().private_key();
		REQUIRE(key);
		const std::array roots{cert::load_pem(cert::ca)};

		const std::array<std::string_view, 2> server_protocols{"h2", "http/1.1"};
		auto server_factory = TestType::acceptor::make({
			.certificate_chain = chain,
			.private_key = *key,
			.supported_protocols = server_protocols,
		});
		REQUIRE(server_factory);

		const std::array<std::string_view, 1> client_protocols{"http/1.1"};
		auto client_factory = TestType::connector::make({
			.trusted_roots = roots,
			.use_system_trust = false,
			.supported_protocols = client_protocols,
		});
		REQUIRE(client_factory);

		auto [server, client] = make_connected_pair<TestType>(*server_factory, *client_factory);
		CHECK(client.selected_protocol() == "http/1.1");
		CHECK(server.selected_protocol() == "http/1.1");
	}

	SECTION("max_message_size")
	{
		if constexpr (TestType::transport_value == crypto::transport::stream)
		{
			CHECK(client.max_message_size() == SIZE_MAX);
		}
		else
		{
			CHECK(client.max_message_size() > 0);
			CHECK(client.max_message_size() < 65536);
		}
	}

	SECTION("local_endpoint")
	{
		auto endpoint = client.local_endpoint();
		REQUIRE(endpoint);
		CHECK(endpoint->address() == ip::address_v4::loopback);
		CHECK(endpoint->port() != ip::port_type::unspecified);
	}

	SECTION("remote_endpoint")
	{
		auto endpoint = client.remote_endpoint();
		REQUIRE(endpoint);
		CHECK(endpoint->address() == ip::address_v4::loopback);
		CHECK(endpoint->port() != ip::port_type::unspecified);
	}

	SECTION("set_get_option")
	{
		using namespace std::chrono_literals;
		REQUIRE(client.set_option(net::receive_timeout{200ms}));

		net::receive_timeout opt{};
		REQUIRE(client.get_option(opt));
		CHECK(opt.timeout() >= 200ms);
	}

	SECTION("release")
	{
		REQUIRE(client);
		auto handle = client.release();
		CHECK_FALSE(client);
		CHECK(static_cast<bool>(handle));

		net::native_socket::close(handle.handle());
	}

	SECTION("make_secure_socket/connector/not_enough_memory")
	{
		auto ctx = TestType::prepare();
		const pal_test::bad_alloc_once x;
		auto result = net::make_secure_socket(TestType::client_socket(ctx), client_factory, default_opts);
		REQUIRE_FALSE(result);
		CHECK(result.error() == std::errc::not_enough_memory);
	}

	SECTION("make_secure_socket/acceptor/not_enough_memory")
	{
		auto ctx = TestType::prepare();

		// For stream, a client must connect to unblock TCP accept before bad_alloc fires.
		std::thread client_thread([&] { std::ignore = TestType::client_socket(ctx); });
		auto server_raw = TestType::server_socket(ctx);
		client_thread.join();

		const pal_test::bad_alloc_once x;
		auto result = net::make_secure_socket(std::move(server_raw), server_factory);
		REQUIRE_FALSE(result);
		CHECK(result.error() == std::errc::not_enough_memory);
	}

	SECTION("hostname_mismatch")
	{
		auto ctx = TestType::prepare();

		// clang-format off
		std::thread server_thread([&]
		{
			std::ignore = net::make_secure_socket(TestType::server_socket(ctx), server_factory);
		});
		// clang-format on

		auto client = net::make_secure_socket(
			TestType::client_socket(ctx), client_factory, {.peer_name = "wrong.name"}
		);
		server_thread.join();

		REQUIRE_FALSE(client);
		CHECK(client.error() == crypto::secure_channel_errc::peer_hostname_mismatch);
	}

	SECTION("verification_failure")
	{
		auto untrusted = TestType::connector::make({.trusted_roots = {}, .use_system_trust = false});
		REQUIRE(untrusted);

		auto ctx = TestType::prepare();

		// clang-format off
		std::thread server_thread([&]
		{
			std::ignore = net::make_secure_socket(TestType::server_socket(ctx), server_factory);
		});
		// clang-format on

		auto client = net::make_secure_socket(TestType::client_socket(ctx), *untrusted, default_opts);
		server_thread.join();

		REQUIRE_FALSE(client);
		CHECK(client.error() == crypto::secure_channel_errc::peer_verification_failed);
	}

	SECTION("failed_handshake_leaves_acceptor_alive")
	{
		// First: bad peer name, handshake fails on client
		{
			auto ctx = TestType::prepare();

			// clang-format off
			std::thread server_thread([&]
			{
				std::ignore = net::make_secure_socket(TestType::server_socket(ctx), server_factory);
			});
			// clang-format on

			auto bad = net::make_secure_socket(
				TestType::client_socket(ctx), client_factory, {.peer_name = "wrong.name"}
			);
			server_thread.join();
			CHECK_FALSE(bad);
		}

		// Second: connection succeeds
		{
			std::optional<typename TestType::secure_socket_type> server_socket;
			auto ctx = TestType::prepare();

			// clang-format off
			std::thread server_thread([&]
			{
				if (auto tls = net::make_secure_socket(TestType::server_socket(ctx), server_factory))
				{
					server_socket = std::move(*tls);
				}
			});
			// clang-format on

			auto good = net::make_secure_socket(TestType::client_socket(ctx), client_factory, default_opts);
			server_thread.join();

			REQUIRE(good);
			REQUIRE(server_socket);
		}
	}
}

// }}}1

} // namespace
