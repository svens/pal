#include <pal/crypto/secure_channel.hpp>
#include <pal/crypto/certificate.hpp>
#include <pal/crypto/certificate_store.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace test_cert = pal_test::cert;
using namespace pal::crypto;

// Scratch buffer size for handshake I/O: comfortably larger than any single (D)TLS record or flight.
constexpr auto io_buffer_size = 16 * 1024;

// handshake driver {{{1

struct handshake_result
{
	std::optional<connected_channel> client;
	std::optional<connected_channel> server;
	std::error_code error;
};

// Drive both sides until both produce a connected channel or one errors. On the connected hand-off the
// originating handshake_channel must go null (checked inline); the slot guard in step_side then stops
// stepping that side, so each hands off at most once.
handshake_result pump (handshake_channel &client_hs, handshake_channel &server_hs)
{
	using diff = std::ptrdiff_t;

	// One direction's in-flight bytes: `data` is the backing store, `size` its valid prefix.
	struct wire
	{
		std::array<std::byte, io_buffer_size> data{};
		size_t size = 0;
	};
	wire c2s;
	wire s2c;

	handshake_result result;

	// Step one side: read its inbound bytes, append handshake output to its outbound buffer. Returns
	// false (with result.error set) on a step error.
	auto step_side = [&] (handshake_channel &handshake, std::optional<connected_channel> &slot, wire &in, wire &out)
	{
		// clang-format off

		if (slot || !handshake)
		{
			return true;
		}

		auto r = handshake.step(
			std::span{in.data}.first(in.size),
			std::span{out.data}.subspan(out.size)
		);
		if (!r)
		{
			result.error = r.error();
			return false;
		}

		std::shift_left(
			in.data.begin(),
			in.data.begin() + static_cast<diff>(in.size),
			static_cast<diff>(r->consumed)
		);
		in.size -= r->consumed;
		out.size += r->produced;

		if (r->connected)
		{
			slot.emplace(std::move(*r->connected));
			CHECK(handshake.is_null());
		}

		return true;

		// clang-format on
	};

	for (int i = 0; i < 32 && !(result.client && result.server); ++i)
	{
		if (!step_side(client_hs, result.client, s2c, c2s))
		{
			return result;
		}
		if (!step_side(server_hs, result.server, c2s, s2c))
		{
			return result;
		}
	}

	REQUIRE(result.client);
	REQUIRE(result.server);
	return result;
}

template <typename Acceptor>
[[nodiscard]] auto server_accept (const Acceptor &acceptor)
{
	if constexpr (Acceptor::transport == transport_type::datagram)
	{
		return acceptor.accept(peer_token::none);
	}
	else
	{
		return acceptor.accept();
	}
}

// Build acceptor + connector from the given options, run the handshake to completion (or first error).
template <typename Traits>
handshake_result establish (
	const typename Traits::acceptor::options &accept_options,
	const typename Traits::connector::options &connect_options,
	std::string_view peer_name = "server.pal.alt.ee"
)
{
	auto acceptor = Traits::acceptor::make(accept_options);
	REQUIRE(acceptor);
	auto connector = Traits::connector::make(connect_options);
	REQUIRE(connector);

	auto connector_handshake = connector->connect({.peer_name = peer_name});
	REQUIRE(connector_handshake);
	auto acceptor_handshake = server_accept(*acceptor);
	REQUIRE(acceptor_handshake);

	return pump(*connector_handshake, *acceptor_handshake);
}

// Positive-path wrapper: the handshake must succeed; returns the connected {client, server} pair.
template <typename Traits>
std::pair<connected_channel, connected_channel> connect_pair (
	const typename Traits::acceptor::options &accept_options,
	const typename Traits::connector::options &connect_options,
	std::string_view peer_name = "server.pal.alt.ee"
)
{
	auto handshake = establish<Traits>(accept_options, connect_options, peer_name);
	REQUIRE_FALSE(handshake.error);
	return {std::move(*handshake.client), std::move(*handshake.server)};
}

// transport traits {{{1

struct stream
{
	using acceptor = stream_acceptor;
	using connector = stream_connector;
	static constexpr bool is_datagram = false;
};

struct datagram
{
	using acceptor = datagram_acceptor;
	using connector = datagram_connector;
	static constexpr bool is_datagram = true;
};

TEST_CASE("crypto/secure_channel/error_category") //{{{1
{
#define __pal_errc(Code, Message) secure_channel_errc::Code,

	SECTION("known")
	{
		const std::error_code ec = GENERATE(values({__pal_secure_channel_errc(__pal_errc)}));
		CAPTURE(ec);
		CHECK_FALSE(ec.message().empty());
		CHECK(ec.message() != "unknown secure channel error");
		CHECK(ec.category() == secure_channel_category());
		CHECK(ec.category().name() == std::string_view{"secure_channel"});
	}

	SECTION("unknown")
	{
		const std::error_code ec = static_cast<secure_channel_errc>(-1);
		CHECK(ec.message() == "unknown secure channel error");
		CHECK(ec.category() == secure_channel_category());
	}

#undef __pal_errc
}

TEMPLATE_TEST_CASE("crypto/secure_channel", "", stream, datagram) //{{{1
{
	using acceptor = TestType::acceptor;
	using connector = TestType::connector;

	auto chain = test_cert::load_pkcs12(test_cert::pkcs12_data);
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);
	const std::array roots{test_cert::load_pem(test_cert::ca)};

	typename acceptor::options accept_options{.certificate_chain = chain, .private_key = *leaf_key};
	typename connector::options connect_options{.trusted_roots = roots, .use_system_trust = false};

	SECTION("round_trip")
	{
		auto [client, server] = connect_pair<TestType>(accept_options, connect_options);

		if constexpr (TestType::is_datagram)
		{
			CHECK(client.max_message_size() > 0);
			CHECK(client.max_message_size() < 65536);
		}

		// Round trip plaintext.
		std::array<std::byte, 4096> buf{};
		auto enc = client.encrypt(pal_test::case_name(), buf);
		REQUIRE(enc);
		CHECK(enc->produced > 0);

		std::array<char, 4096> plain{};
		auto dec = server.decrypt(std::span{buf}.first(enc->produced), plain);
		REQUIRE(dec);
		CHECK(std::string_view{plain.data(), dec->produced} == pal_test::case_name());

		// Server closes, client observes peer_closed.
		std::array<std::byte, 256> close_buf{};
		auto srv_close = server.close_notify(close_buf);
		REQUIRE(srv_close);

		std::array<std::byte, 256> drain{};
		auto cli_dec = client.decrypt(std::span{close_buf}.first(srv_close->produced), drain);
		REQUIRE(cli_dec);

		if constexpr (TestType::is_datagram && pal::os == pal::os_type::windows)
		{
			// Datagram on SChannel cannot generate a DTLS close_notify, so close is a best-effort
			// no-op (the peer detects closure via the transport); still exercise the surface, but
			// don't require it.
			CHECK_NOFAIL(srv_close->produced > 0);
			CHECK_NOFAIL(cli_dec->peer_closed);
		}
		else
		{
			CHECK(srv_close->produced > 0);
			CHECK(cli_dec->peer_closed);
		}
	}

	SECTION("decrypt_tampered")
	{
		auto [client, server] = connect_pair<TestType>(accept_options, connect_options);

		std::array<std::byte, 4096> cipher{};
		auto enc = client.encrypt(pal_test::case_name(), cipher);
		REQUIRE(enc);
		REQUIRE(enc->produced > 6);

		// Flip the last ciphertext byte (the AEAD tag) so the record fails authentication on the peer.
		cipher[enc->produced - 1] ^= std::byte{0xff};

		std::array<std::byte, 4096> plain{};
		auto dec = server.decrypt(std::span{cipher}.first(enc->produced), plain);
		if constexpr (TestType::is_datagram)
		{
			// DTLS silently discards a record that fails authentication (RFC 9147): the bad record is
			// dropped, nothing is produced, and no error surfaces.
			REQUIRE(dec);
			CHECK(dec->produced == 0);
		}
		else
		{
			REQUIRE_FALSE(dec);
			CHECK(dec.error() == secure_channel_errc::decrypt_failed);
		}
	}

	SECTION("peer_certificate")
	{
		auto [client, server] = connect_pair<TestType>(accept_options, connect_options);

		auto peer_on_client = client.peer_certificate();
		REQUIRE(peer_on_client);
		REQUIRE_FALSE(peer_on_client->is_null());
		CHECK(peer_on_client->common_name() == "pal.alt.ee");

		// No mTLS: server should see no peer cert.
		auto peer_on_server = server.peer_certificate();
		REQUIRE(peer_on_server);
		CHECK(peer_on_server->is_null());
	}

	SECTION("peer_hostname_mismatch")
	{
		auto handshake = establish<TestType>(accept_options, connect_options, "wrong.name");
		CHECK(handshake.error == secure_channel_errc::peer_hostname_mismatch);
	}

	SECTION("empty_trust")
	{
		// Empty trust + use_system_trust=false -> strict verification rejects the server cert.
		connect_options.trusted_roots = {};
		auto handshake = establish<TestType>(accept_options, connect_options);
		CHECK(handshake.error == secure_channel_errc::peer_verification_failed);
	}

	SECTION("self_signed")
	{
		// note: here we use EC because it is self-signed
		auto ec_chain = test_cert::load_pkcs12(test_cert::pkcs12_ec_data);
		REQUIRE_FALSE(ec_chain.empty());
		auto ec_key = ec_chain.front().private_key();
		REQUIRE(ec_key);

		const typename acceptor::options ec_accept{.certificate_chain = ec_chain, .private_key = *ec_key};
		const typename connector::options strict_connect{.trusted_roots = {}, .use_system_trust = false};

		auto run = [&] (verify_relax relax)
		{
			auto a = acceptor::make(ec_accept);
			REQUIRE(a);
			auto c = connector::make(strict_connect);
			REQUIRE(c);
			auto connect_handshake = c->connect({.relax = relax});
			REQUIRE(connect_handshake);
			auto accept_handshake = server_accept(*a);
			REQUIRE(accept_handshake);
			return pump(*connect_handshake, *accept_handshake);
		};

		// Without the relaxation the self-signed server cert is rejected.
		auto result = run(verify_relax::none);
		CHECK(result.error == secure_channel_errc::peer_verification_failed);

		// With it the same cert is accepted and the handshake completes.
		result = run(verify_relax::self_signed);
		CHECK_FALSE(result.error);
	}

	SECTION("invalid_configuration")
	{
		// Connector advertising a client-cert chain but no matching private key.
		{
			const typename connector::options bad = {
				.trusted_roots = roots,
				.use_system_trust = false,
				.certificate_chain = chain,
				.private_key = key{},
			};
			auto c = connector::make(bad);
			REQUIRE_FALSE(c);
			CHECK(c.error() == secure_channel_errc::invalid_configuration);
		}

		// ALPN entry exceeding the per-protocol length limit (encoded in a single length byte).
		{
			const std::string proto(256, 'h');
			const std::array<std::string_view, 1> protocols{proto};
			const typename connector::options bad = {
				.trusted_roots = roots,
				.use_system_trust = false,
				.supported_protocols = protocols,
			};
			auto c = connector::make(bad);
			REQUIRE_FALSE(c);
			CHECK(c.error() == secure_channel_errc::invalid_configuration);
		}

		// peer_name longer than the 255-byte SNI limit.
		{
			auto c = connector::make(connect_options);
			REQUIRE(c);
			const std::string name(256, 'a');
			auto h = c->connect({.peer_name = name});
			REQUIRE_FALSE(h);
			CHECK(h.error() == secure_channel_errc::invalid_configuration);
		}
	}

#if TODO_not_enough_memory
	SECTION("make: not_enough_memory")
	{
		const pal_test::bad_alloc_once x;
		auto acc = acceptor::make(accept_options);
		REQUIRE_FALSE(acc);
		CHECK(acc.error() == std::errc::not_enough_memory);
	}

	SECTION("accept: not_enough_memory")
	{
		auto acc = acceptor::make(accept_options);
		REQUIRE(acc);

		const pal_test::bad_alloc_once x;
		auto hs = server_accept(*acc);
		REQUIRE_FALSE(hs);
		CHECK(hs.error() == std::errc::not_enough_memory);
	}

	SECTION("connect: not_enough_memory")
	{
		auto cn = connector::make(connect_options);
		REQUIRE(cn);

		const pal_test::bad_alloc_once x;
		auto hs = cn->connect({});
		REQUIRE_FALSE(hs);
		CHECK(hs.error() == std::errc::not_enough_memory);
	}
#endif

	SECTION("alpn")
	{
		const std::array<std::string_view, 2> server_protocols{"h2", "http/1.1"};
		const std::array<std::string_view, 1> client_protocols{"http/1.1"};
		accept_options.supported_protocols = server_protocols;
		connect_options.supported_protocols = client_protocols;

		auto [client, server] = connect_pair<TestType>(accept_options, connect_options);
		CHECK(client.selected_protocol() == "http/1.1");

		if constexpr (TestType::is_datagram && pal::os == pal::os_type::windows)
		{
			// The SChannel DTLS *server* cannot introspect its own ALPN selection (it wegotiates
			// correctly on the wire, but reports no selected protocol). Other backends/roles report it.
			// Accept either the negotiated protocol or empty so the check holds across backends.
			CHECK_NOFAIL(server.selected_protocol() == "http/1.1");
		}
		else
		{
			CHECK(server.selected_protocol() == "http/1.1");
		}
	}

	SECTION("alpn_mismatch")
	{
		const std::array<std::string_view, 1> server_protocols{"h2"};
		const std::array<std::string_view, 1> client_protocols{"smtp"};
		accept_options.supported_protocols = server_protocols;
		connect_options.supported_protocols = client_protocols;

		auto handshake = establish<TestType>(accept_options, connect_options);
		CHECK(handshake.error == secure_channel_errc::no_application_protocol);
	}

	SECTION("mtls_missing_client_cert")
	{
		accept_options.require_client_certificate = true;
		accept_options.trusted_roots = roots;

		auto handshake = establish<TestType>(accept_options, connect_options);

		// Deterministic per build but varies by TLS version / OpenSSL: the server may reject the missing
		// client cert directly, or the client may surface the resulting fatal alert generically.
		const std::array<std::error_code, 3> expected{
			secure_channel_errc::client_certificate_required,
			secure_channel_errc::peer_verification_failed,
			secure_channel_errc::handshake_failed,
		};
		CHECK(std::ranges::contains(expected, handshake.error));
	}

	SECTION("encrypt_after_close")
	{
		auto client = connect_pair<TestType>(accept_options, connect_options).first;
		std::array<std::byte, 256> buf{};

		REQUIRE(client.close_notify(buf));

		auto enc = client.encrypt(pal_test::case_name(), buf);
		REQUIRE_FALSE(enc);
		CHECK(enc.error() == secure_channel_errc::closed);
	}

	SECTION("close_idempotent")
	{
		auto [client, server] = connect_pair<TestType>(accept_options, connect_options);

		std::array<std::byte, 256> buf{};
		REQUIRE(client.close_notify(buf));

		auto again = client.close_notify(buf);
		REQUIRE(again);
		CHECK(again->produced == 0);
	}

	SECTION("close_undersized_output")
	{
		auto [client, server] = connect_pair<TestType>(accept_options, connect_options);

		std::array<std::byte, 1> tiny{};
		auto tight = client.close_notify(tiny);
		REQUIRE(tight);

		if constexpr (TestType::is_datagram && pal::os == pal::os_type::windows)
		{
			// Stream (and OpenSSL DTLS) needs more than 1 byte for the close_notify record.
			// Only SChannel datagram close is a no-op, so there is no output and nothing to overflow.
			CHECK_NOFAIL(tight->want_output);
		}
		else
		{
			CHECK(tight->want_output);
		}
	}

	if constexpr (!TestType::is_datagram)
	{
		SECTION("decrypt_drain")
		{
			// Encrypt a large message; decrypt with a small output buffer to exercise drain mode.
			auto [client, server] = connect_pair<TestType>(accept_options, connect_options);

			const std::vector<std::byte> msg(8UL * 1024, std::byte{0x42});
			std::vector<std::byte> cipher(32UL * 1024);
			auto enc = client.encrypt(msg, cipher);
			REQUIRE(enc);
			REQUIRE(enc->consumed == msg.size());

			std::array<std::byte, 128> out{};
			size_t total = 0;
			auto remaining = std::span{cipher}.first(enc->produced);

			for (int i = 0; i < 256 && total < msg.size(); ++i)
			{
				auto dec = remaining.empty() ? server.decrypt(out) : server.decrypt(remaining, out);
				REQUIRE(dec);

				total += dec->produced;
				remaining = remaining.subspan(dec->consumed);

				if (remaining.empty() && dec->produced == 0)
				{
					break;
				}
			}

			CHECK(total == msg.size());
		}

		SECTION("decrypt_incremental_input")
		{
			// Ciphertext delivered one byte at a time (slow/hostile peer): the server buffers partial
			// records, emits plaintext only once a record completes, and makes progress every call.
			auto [client, server] = connect_pair<TestType>(accept_options, connect_options);

			std::array<std::byte, 4096> cipher{};
			auto enc = client.encrypt(pal_test::case_name(), cipher);
			REQUIRE(enc);

			std::vector<char> plain;
			std::array<char, 4096> out{};
			for (size_t fed = 0; fed < enc->produced; ++fed)
			{
				auto dec = server.decrypt(std::span{cipher}.subspan(fed, 1), out);
				REQUIRE(dec);
				CHECK(dec->consumed == 1);
				plain.append_range(std::span{out}.first(dec->produced));
			}
			CHECK(std::string_view{plain.data(), plain.size()} == pal_test::case_name());
		}

		SECTION("encrypt_partial_output")
		{
			// Multi-record message (> one 16 KiB TLS record) encrypted into an output buffer that holds
			// only about one record. With SSL_MODE_ENABLE_PARTIAL_WRITE the caller advances by consumed
			// each call, so every call makes forward progress (consumed > 0); without partial writes
			// consumed would be 0.
			auto [client, server] = connect_pair<TestType>(accept_options, connect_options);

			const std::vector<std::byte> msg(40UL * 1000, std::byte{0x42});
			std::array<std::byte, 20000> out{};
			std::vector<std::byte> cipher;

			size_t consumed = 0;
			for (int i = 0; i < 64 && consumed < msg.size(); ++i)
			{
				auto enc = client.encrypt(std::span{msg}.subspan(consumed), out);
				REQUIRE(enc);
				CHECK(enc->consumed > 0);
				cipher.append_range(std::span{out}.first(enc->produced));
				consumed += enc->consumed;
				CHECK(enc->want_output == (consumed < msg.size()));
			}
			REQUIRE(consumed == msg.size());

			// The reassembled ciphertext must decrypt back to the original message.
			std::vector<std::byte> plain;
			std::span<const std::byte> rem{cipher};
			for (int i = 0; i < 64 && plain.size() < msg.size(); ++i)
			{
				auto dec = rem.empty() ? server.decrypt(out) : server.decrypt(rem, out);
				REQUIRE(dec);
				plain.append_range(std::span{out}.first(dec->produced));
				rem = rem.subspan(dec->consumed);
				if (rem.empty() && dec->produced == 0)
				{
					break;
				}
			}
			CHECK(plain == msg);
		}

		SECTION("decrypt_oversized_record")
		{
			// A record header declaring the maximum length, then body bytes that never complete it, dripped
			// in small chunks. The receiver accumulates until its record buffer saturates, at which point
			// decrypt must error instead of buffering forever — on SChannel the cipher_buf saturation
			// guard, on OpenSSL record_overflow at the record layer. Regression for the oversized-record
			// receive livelock. The loop bound caps the test: a regressed build keeps returning want_input,
			// exits the loop with dec still valid, and fails the assertion rather than hanging.
			auto server = connect_pair<TestType>(accept_options, connect_options).second;

			const std::array hdr{
				std::byte{0x17}, // content type: application_data
				std::byte{0x03}, // legacy record version (TLS 1.2)
				std::byte{0x03},
				std::byte{0xff}, // declared length 0xffff — beyond what any record may carry
				std::byte{0xff},
			};
			const std::array<std::byte, 512> body{};
			std::array<std::byte, 256> ignored{};

			auto dec = server.decrypt(hdr, ignored);
			for (int i = 0; i < 256 && dec; ++i)
			{
				dec = server.decrypt(body, ignored);
			}
			REQUIRE_FALSE(dec); // saturation guard fired before the buffer grew unbounded
		}
	}

	if constexpr (TestType::is_datagram)
	{
		SECTION("message_too_large")
		{
			auto client = connect_pair<TestType>(accept_options, connect_options).first;

			const size_t cap = client.max_message_size();
			REQUIRE(cap > 0);

			const std::vector<std::byte> oversized(cap + 1, std::byte{0});
			std::array<std::byte, 4096> out{};
			auto r = client.encrypt(oversized, out);
			REQUIRE_FALSE(r);
			CHECK(r.error() == secure_channel_errc::message_too_large);
		}

		SECTION("decrypt_multi_record")
		{
			auto [client, server] = connect_pair<TestType>(accept_options, connect_options);

			// Two independently encrypted records concatenated into one buffer.
			const auto msg1 = pal_test::case_name() + " 1";
			const auto msg2 = pal_test::case_name() + " 2";
			std::array<std::byte, 2048> rec{};
			std::vector<std::byte> combined;

			auto enc1 = client.encrypt(msg1, rec);
			REQUIRE(enc1);
			combined.append_range(std::span{rec}.first(enc1->produced));

			auto enc2 = client.encrypt(msg2, rec);
			REQUIRE(enc2);
			combined.append_range(std::span{rec}.first(enc2->produced));

			// First decrypt must yield exactly the first record's payload and consume exactly the
			// first record, leaving the second untouched for a follow-up call.
			std::array<char, 2048> plain{};
			auto dec1 = server.decrypt(std::span<const std::byte>{combined}, plain);
			REQUIRE(dec1);
			CHECK(std::string_view{plain.data(), dec1->produced} == msg1);
			CHECK(dec1->consumed == enc1->produced);

			// Second decrypt on the remainder yields the second record.
			const auto rest = std::span{combined}.subspan(dec1->consumed);
			auto dec2 = server.decrypt(rest, plain);
			REQUIRE(dec2);
			CHECK(std::string_view{plain.data(), dec2->produced} == msg2);
		}
	}
}

TEMPLATE_TEST_CASE("crypto/secure_channel/factory_outlives_inputs", "", stream, datagram) //{{{1
{
	// The factory's must hold its own references to the cert chain, key and trusted roots, so sessions keep
	// working after the caller's certificate/key handles are destroyed. Run under sanitize to catch any
	// use-after-free if those references are not actually held.
	std::optional<typename TestType::acceptor> acceptor;
	std::optional<typename TestType::connector> connector;

	{
		auto chain = test_cert::load_pkcs12(test_cert::pkcs12_data);
		REQUIRE_FALSE(chain.empty());
		auto leaf_key = chain.front().private_key();
		REQUIRE(leaf_key);
		const std::array roots{test_cert::load_pem(test_cert::ca)};

		{
			auto r = TestType::acceptor::make({.certificate_chain = chain, .private_key = *leaf_key});
			REQUIRE(r);
			acceptor = std::move(*r);
		}

		{
			auto r = TestType::connector::make({.trusted_roots = roots, .use_system_trust = false});
			REQUIRE(r);
			connector = std::move(*r);
		}
	}
	// chain / leaf_key / roots are destroyed here; the factories must work from SSL_CTX refs alone.

	auto connector_handshake = connector->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(connector_handshake);
	auto acceptor_handshake = server_accept(*acceptor);
	REQUIRE(acceptor_handshake);

	auto handshake = pump(*connector_handshake, *acceptor_handshake);
	REQUIRE_FALSE(handshake.error);
	auto [client, server] = std::pair{std::move(*handshake.client), std::move(*handshake.server)};

	std::array<std::byte, 4096> buf{};
	auto enc = client.encrypt(pal_test::case_name(), buf);
	REQUIRE(enc);

	std::array<char, 4096> plain{};
	auto dec = server.decrypt(std::span{buf}.first(enc->produced), plain);
	REQUIRE(dec);
	CHECK(std::string_view{plain.data(), dec->produced} == pal_test::case_name());
}

// A peer_token_source whose ADL adapter writes `produce` bytes (0 declines), exercising the make() overload.
struct fake_source //{{{1
{
	size_t produce;
};

size_t to_peer_token (const fake_source &source, std::span<std::byte> out) noexcept
{
	const auto n = (std::min)(source.produce, out.size());
	std::ranges::fill(out.first(n), std::byte{0xAB});
	return n;
}

TEST_CASE("crypto/secure_channel/peer_token") //{{{1
{
	SECTION("max_size")
	{
		const std::array<std::byte, peer_token::max_size> bytes{};
		auto token = peer_token::make(bytes);
		REQUIRE(token);
		CHECK(token->bytes().size() == peer_token::max_size);
	}

	SECTION("overflow")
	{
		const std::array<std::byte, peer_token::max_size + 1> bytes{};
		auto token = peer_token::make(bytes);
		REQUIRE_FALSE(token);
		CHECK(token.error() == secure_channel_errc::invalid_configuration);
	}

	SECTION("empty input rejected")
	{
		auto token = peer_token::make(std::span<const std::byte>{});
		REQUIRE_FALSE(token);
		CHECK(token.error() == secure_channel_errc::invalid_configuration);
	}

	SECTION("none")
	{
		CHECK(peer_token::none.empty());
		CHECK(peer_token::none.bytes().empty());
	}

	SECTION("from source")
	{
		auto token = peer_token::make(fake_source{8});
		REQUIRE(token);
		CHECK(token->bytes().size() == 8);
	}

	SECTION("source declines")
	{
		auto token = peer_token::make(fake_source{0});
		REQUIRE_FALSE(token);
		CHECK(token.error() == secure_channel_errc::invalid_configuration);
	}
}

TEMPLATE_TEST_CASE("crypto/secure_channel/dtls_cookie", "[!nonportable]", datagram) //{{{1
{
	auto chain = test_cert::load_pkcs12(test_cert::pkcs12_data);
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);
	const std::array roots{test_cert::load_pem(test_cert::ca)};

	auto acceptor = TestType::acceptor::make({.certificate_chain = chain, .private_key = *leaf_key});
	REQUIRE(acceptor);
	auto connector = TestType::connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(connector);

	// Opaque per-peer identity; the crypto layer never interprets it, only binds the cookie to it.
	const std::array token_bytes{
		std::byte{1},
		std::byte{2},
		std::byte{3},
		std::byte{4},
		std::byte{5},
		std::byte{6},
		std::byte{7},
		std::byte{8},
	};
	auto token = peer_token::make(token_bytes);
	REQUIRE(token);

	SECTION("amplification")
	{
		auto client = connector->connect({.peer_name = "server.pal.alt.ee"});
		REQUIRE(client);
		auto server = acceptor->accept(*token);
		REQUIRE(server);

		std::array<std::byte, io_buffer_size> client_buf{};
		std::array<std::byte, io_buffer_size> server_buf{};

		// ClientHello#1 -> small HelloVerifyRequest; the server withholds its flight, not yet connected.
		auto ch1 = client->step(client_buf);
		REQUIRE(ch1);
		auto hvr = server->step(std::span{client_buf}.first(ch1->produced), server_buf);
		REQUIRE(hvr);
		CHECK_FALSE(hvr->connected);
		REQUIRE(hvr->produced > 0);

		// ClientHello#2 echoes the cookie -> the full ServerHello+Certificate flight.
		auto ch2 = client->step(std::span{server_buf}.first(hvr->produced), client_buf);
		REQUIRE(ch2);
		REQUIRE(ch2->produced > 0);
		auto flight = server->step(std::span{client_buf}.first(ch2->produced), server_buf);
		REQUIRE(flight);

		// The large flight is gated behind the cookie round-trip — the anti-amplification guarantee.
		CHECK(hvr->produced < flight->produced);
	}

	SECTION("empty_token")
	{
		auto client = connector->connect({.peer_name = "server.pal.alt.ee"});
		REQUIRE(client);
		std::array<std::byte, io_buffer_size> client_hello_buf{};
		auto ch1 = client->step(client_hello_buf);
		REQUIRE(ch1);
		const auto client_hello = std::span{client_hello_buf}.first(ch1->produced);

		std::array<std::byte, io_buffer_size> buf{};

		auto server_with_cookie = acceptor->accept(*token);
		REQUIRE(server_with_cookie);
		auto with_cookie = server_with_cookie->step(client_hello, buf);
		REQUIRE(with_cookie);

		auto server_without_cookie = acceptor->accept(peer_token::none);
		REQUIRE(server_without_cookie);
		auto without_cookie = server_without_cookie->step(client_hello, buf);
		REQUIRE(without_cookie);

		if constexpr (pal::os == pal::os_type::windows)
		{
			// SChannel always runs the cookie exchange, so the empty-token server still gates — same HVR.
			CHECK(with_cookie->produced == without_cookie->produced);
		}
		else
		{
			// OpenSSL: an empty token forgoes the cookie, so the unprotected server emits its full flight.
			CHECK(with_cookie->produced < without_cookie->produced);
		}
	}

	SECTION("handshake")
	{
		auto client = connector->connect({.peer_name = "server.pal.alt.ee"});
		REQUIRE(client);
		auto server = acceptor->accept(*token);
		REQUIRE(server);

		auto result = pump(*client, *server);
		REQUIRE_FALSE(result.error);
		REQUIRE(result.client);
		REQUIRE(result.server);
	}
}

// }}}1

} // namespace
