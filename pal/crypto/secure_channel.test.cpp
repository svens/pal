#include <pal/crypto/secure_channel.hpp>
#include <pal/crypto/certificate.hpp>
#include <pal/crypto/certificate_store.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstring>
#include <vector>

namespace
{

namespace test_cert = pal_test::cert;
using namespace pal::crypto;

auto load_cert (const test_cert::info &info)
{
	auto cert = certificate::from_pem(info.pem);
	REQUIRE(cert);
	return std::move(*cert);
}

auto load_pkcs12_chain ()
{
	const auto data = std::as_bytes(std::span{test_cert::pkcs12_data});
	auto store = certificate_store::from_pkcs12(data);
	REQUIRE(store);
	std::vector<certificate> chain;
	for (auto cert: *store)
	{
		chain.push_back(std::move(cert));
	}
	return chain;
}

template <typename Factory>
auto pump_handshake (handshake_channel &client_hs, handshake_channel &server_hs)
{
	// Pump until both produce a connected channel.
	std::vector<std::byte> c2s(size_t{16} * 1024);
	std::vector<std::byte> s2c(size_t{16} * 1024);
	size_t c2s_size = 0;
	size_t s2c_size = 0;

	std::optional<connected_channel> client;
	std::optional<connected_channel> server;

	for (int i = 0; i < 32; ++i)
	{
		if (!client && client_hs)
		{
			auto r = client_hs.step(
				std::span{s2c.data(), s2c_size}, std::span{c2s.data() + c2s_size, c2s.size() - c2s_size}
			);
			if (!r)
			{
				INFO("client step error: " << r.error().message());
				REQUIRE(r);
			}
			s2c.erase(
				s2c.begin(),
				s2c.begin() + static_cast<std::vector<std::byte>::difference_type>(r->consumed)
			);
			s2c_size -= r->consumed;
			c2s_size += r->produced;
			if (r->connected)
			{
				client.emplace(std::move(*r->connected));
			}
		}

		if (!server && server_hs)
		{
			auto r = server_hs.step(
				std::span{c2s.data(), c2s_size}, std::span{s2c.data() + s2c_size, s2c.size() - s2c_size}
			);
			if (!r)
			{
				INFO("server step error: " << r.error().message());
				REQUIRE(r);
			}
			c2s.erase(
				c2s.begin(),
				c2s.begin() + static_cast<std::vector<std::byte>::difference_type>(r->consumed)
			);
			c2s_size -= r->consumed;
			s2c_size += r->produced;
			if (r->connected)
			{
				server.emplace(std::move(*r->connected));
			}
		}

		if (client && server)
		{
			break;
		}
	}

	REQUIRE(client.has_value());
	REQUIRE(server.has_value());

	// NOLINTNEXTLINE(bugprone-unchecked-optional-access)
	return std::pair{std::move(*client), std::move(*server)};
}

} // namespace

TEST_CASE("crypto/secure_channel/sniff_stream")
{
	using pal::crypto::wire_protocol;

	SECTION("empty -> need_more")
	{
		CHECK(sniff_stream(std::span<const std::byte>{}) == wire_protocol::need_more);
	}

	SECTION("plain text")
	{
		const std::array<std::byte, 6> bytes{
			std::byte{'G'}, std::byte{'E'}, std::byte{'T'}, std::byte{' '}, std::byte{'/'}, std::byte{' '}};
		CHECK(sniff_stream(bytes) == wire_protocol::plain);
	}

	SECTION("TLS ClientHello header")
	{
		const std::array<std::byte, 6> bytes{
			std::byte{0x16},
			std::byte{0x03},
			std::byte{0x01},
			std::byte{0x00},
			std::byte{0xC8},
			std::byte{0x01}};
		CHECK(sniff_stream(bytes) == wire_protocol::tls);
	}

	SECTION("partial: need more")
	{
		const std::array<std::byte, 2> short_bytes{std::byte{0x16}, std::byte{0x03}};
		CHECK(sniff_stream(short_bytes) == wire_protocol::need_more);
	}

	SECTION("bad version")
	{
		const std::array<std::byte, 6> bytes{
			std::byte{0x16},
			std::byte{0x05},
			std::byte{0x01},
			std::byte{0x00},
			std::byte{0xC8},
			std::byte{0x01}};
		CHECK(sniff_stream(bytes) == wire_protocol::plain);
	}
}

TEST_CASE("crypto/secure_channel/sniff_datagram")
{
	using pal::crypto::wire_protocol;

	SECTION("too short -> plain")
	{
		const std::array<std::byte, 8> bytes{};
		CHECK(sniff_datagram(bytes) == wire_protocol::plain);
	}

	SECTION("DTLS ClientHello header")
	{
		const std::array<std::byte, 16> bytes{
			std::byte{0x16},
			std::byte{0xFE},
			std::byte{0xFD},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x03},
			std::byte{0x01},
			std::byte{0x00},
			std::byte{0x00}};
		CHECK(sniff_datagram(bytes) == wire_protocol::dtls);
	}

	SECTION("non-handshake content type -> plain")
	{
		const std::array<std::byte, 16> bytes{
			std::byte{0x17},
			std::byte{0xFE},
			std::byte{0xFD},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x00},
			std::byte{0x03},
			std::byte{0x01},
			std::byte{0x00},
			std::byte{0x00}};
		CHECK(sniff_datagram(bytes) == wire_protocol::plain);
	}
}

TEST_CASE("crypto/secure_channel/dtls_record_size")
{
	using pal::crypto::__secure_channel::dtls_record_size;

	// DTLS 1.2 record header is 13 bytes with a 16-bit length at offset 11.
	const auto make_record = [] (uint16_t length, size_t total)
	{
		std::vector<std::byte> bytes(total, std::byte{0});
		if (total > 12)
		{
			bytes[11] = std::byte{static_cast<unsigned char>(length >> 8U)};
			bytes[12] = std::byte{static_cast<unsigned char>(length & 0xffU)};
		}
		return bytes;
	};

	SECTION("buffer shorter than record header -> whole buffer")
	{
		const std::array<std::byte, 8> bytes{};
		CHECK(dtls_record_size(bytes) == bytes.size());
	}

	SECTION("header only, zero-length payload")
	{
		const auto bytes = make_record(0, 13);
		CHECK(dtls_record_size(bytes) == 13);
	}

	SECTION("single record sized exactly")
	{
		const auto bytes = make_record(4, 17); // 13 header + 4 payload
		CHECK(dtls_record_size(bytes) == 17);
	}

	SECTION("first of multiple records")
	{
		const auto bytes = make_record(4, 30); // first record is 17, trailing bytes follow
		CHECK(dtls_record_size(bytes) == 17);
	}

	SECTION("claimed length overruns buffer -> clamped to whole buffer")
	{
		const auto bytes = make_record(100, 17); // claims 113, only 17 present
		CHECK(dtls_record_size(bytes) == bytes.size());
	}
}

TEST_CASE("crypto/secure_channel/error_category")
{
	auto ec = make_error_code(secure_channel_errc::handshake_failed);
	CHECK(ec);
	CHECK(ec.category().name() == std::string_view{"secure_channel"});
	CHECK_FALSE(ec.message().empty());
}

TEST_CASE("crypto/secure_channel/stream/handshake")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	const stream_acceptor::options acc_opts{
		.certificate_chain = chain,
		.private_key = *leaf_key,
	};
	auto acc = stream_acceptor::make(acc_opts);
	REQUIRE(acc);

	const stream_connector::options cn_opts{
		.trusted_roots = roots,
		.use_system_trust = false,
	};
	auto cn = stream_connector::make(cn_opts);
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	auto [client, server] = pump_handshake<stream_acceptor>(*cn_hs, *ac_hs);

	CHECK(client);
	CHECK(server);

	// Round trip plaintext
	std::array<std::byte, 4096> buf{};
	const std::string_view msg = "hello world";
	auto enc = client.encrypt(msg, buf);
	REQUIRE(enc);
	CHECK(enc->produced > 0);

	std::array<std::byte, 4096> plain{};
	auto dec = server.decrypt(std::span{buf.data(), enc->produced}, plain);
	REQUIRE(dec);
	CHECK(dec->produced == msg.size());
	CHECK(std::memcmp(plain.data(), msg.data(), msg.size()) == 0);

	// Server close, client observes
	std::array<std::byte, 256> close_buf{};
	auto srv_close = server.close(close_buf);
	REQUIRE(srv_close);
	CHECK(srv_close->produced > 0);

	std::array<std::byte, 256> drain{};
	auto cli_dec = client.decrypt(std::span{close_buf.data(), srv_close->produced}, drain);
	REQUIRE(cli_dec);
	CHECK(cli_dec->peer_closed);
}

TEST_CASE("crypto/secure_channel/factory_outlives_inputs")
{
	// The factory's SSL_CTX must hold its own references to the cert chain, key and trusted roots, so
	// sessions keep working after the caller's certificate/key handles are destroyed. Run under sanitize
	// to catch any use-after-free if those references are not actually held.
	std::optional<stream_acceptor> acc;
	std::optional<stream_connector> cn;
	{
		auto chain = load_pkcs12_chain();
		REQUIRE_FALSE(chain.empty());
		auto leaf_key = chain.front().private_key();
		REQUIRE(leaf_key);
		auto ca = load_cert(test_cert::ca);
		std::array<certificate, 1> roots{ca};

		auto acc_result = stream_acceptor::make({.certificate_chain = chain, .private_key = *leaf_key});
		REQUIRE(acc_result);
		acc = std::move(*acc_result);

		auto cn_result = stream_connector::make({.trusted_roots = roots, .use_system_trust = false});
		REQUIRE(cn_result);
		cn = std::move(*cn_result);
	}
	// chain / leaf_key / ca / roots are destroyed here; the factories must work from SSL_CTX refs alone.

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	auto [client, server] = pump_handshake<stream_acceptor>(*cn_hs, *ac_hs);
	REQUIRE(client);
	REQUIRE(server);

	std::array<std::byte, 4096> buf{};
	const std::string_view msg = "hello after inputs freed";
	auto enc = client.encrypt(msg, buf);
	REQUIRE(enc);

	std::array<std::byte, 4096> plain{};
	auto dec = server.decrypt(std::span{buf.data(), enc->produced}, plain);
	REQUIRE(dec);
	CHECK(dec->produced == msg.size());
	CHECK(std::memcmp(plain.data(), msg.data(), msg.size()) == 0);
}

TEST_CASE("crypto/secure_channel/stream/decrypt_tampered")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	auto acc = stream_acceptor::make({.certificate_chain = chain, .private_key = *leaf_key});
	REQUIRE(acc);
	auto cn = stream_connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	auto [client, server] = pump_handshake<stream_acceptor>(*cn_hs, *ac_hs);
	REQUIRE(client);
	REQUIRE(server);

	std::array<std::byte, 4096> cipher{};
	const std::string_view msg = "hello world";
	auto enc = client.encrypt(msg, cipher);
	REQUIRE(enc);
	REQUIRE(enc->produced > 6);

	// Flip the last ciphertext byte (the AEAD tag) so the record fails authentication on the peer.
	cipher[enc->produced - 1] ^= std::byte{0xff};

	std::array<std::byte, 4096> plain{};
	auto dec = server.decrypt(std::span{cipher.data(), enc->produced}, plain);
	REQUIRE_FALSE(dec);
	CHECK(dec.error() == secure_channel_errc::decrypt_failed);
}

TEST_CASE("crypto/secure_channel/stream/peer_hostname_mismatch")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	auto acc = stream_acceptor::make({.certificate_chain = chain, .private_key = *leaf_key});
	REQUIRE(acc);
	auto cn = stream_connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "wrong.name"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	std::vector<std::byte> c2s(size_t{16} * 1024);
	std::vector<std::byte> s2c(size_t{16} * 1024);
	size_t c2s_size = 0;
	size_t s2c_size = 0;
	std::error_code last_err;

	for (int i = 0; i < 32; ++i)
	{
		if (*cn_hs)
		{
			auto r = cn_hs->step(
				std::span{s2c.data(), s2c_size}, std::span{c2s.data() + c2s_size, c2s.size() - c2s_size}
			);
			if (!r)
			{
				last_err = r.error();
				break;
			}
			s2c.erase(
				s2c.begin(),
				s2c.begin() + static_cast<std::vector<std::byte>::difference_type>(r->consumed)
			);
			s2c_size -= r->consumed;
			c2s_size += r->produced;
			if (r->connected)
			{
				break;
			}
		}
		if (*ac_hs)
		{
			auto r = ac_hs->step(
				std::span{c2s.data(), c2s_size}, std::span{s2c.data() + s2c_size, s2c.size() - s2c_size}
			);
			if (!r)
			{
				last_err = r.error();
				break;
			}
			c2s.erase(
				c2s.begin(),
				c2s.begin() + static_cast<std::vector<std::byte>::difference_type>(r->consumed)
			);
			c2s_size -= r->consumed;
			s2c_size += r->produced;
			if (r->connected)
			{
				break;
			}
		}
	}

	CHECK(last_err == secure_channel_errc::peer_hostname_mismatch);
}

TEST_CASE("crypto/secure_channel/datagram/handshake")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	auto acc = datagram_acceptor::make({.certificate_chain = chain, .private_key = *leaf_key});
	REQUIRE(acc);
	auto cn = datagram_connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	auto [client, server] = pump_handshake<datagram_acceptor>(*cn_hs, *ac_hs);

	CHECK(client.max_message_size() > 0);
	CHECK(client.max_message_size() < 65536);

	std::array<std::byte, 2048> buf{};
	const std::string_view msg = "datagram hello";
	auto enc = client.encrypt(msg, buf);
	REQUIRE(enc);
	REQUIRE(enc->produced > 0);

	std::array<std::byte, 2048> plain{};
	auto dec = server.decrypt(std::span{buf.data(), enc->produced}, plain);
	REQUIRE(dec);
	CHECK(dec->produced == msg.size());
}

TEST_CASE("crypto/secure_channel/datagram/decrypt_multi_record")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	auto acc = datagram_acceptor::make({.certificate_chain = chain, .private_key = *leaf_key});
	REQUIRE(acc);
	auto cn = datagram_connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	auto [client, server] = pump_handshake<datagram_acceptor>(*cn_hs, *ac_hs);

	// Two independently encrypted records concatenated into one buffer.
	std::array<std::byte, 2048> rec1{};
	std::array<std::byte, 2048> rec2{};
	const std::string_view msg1 = "first record";
	const std::string_view msg2 = "second record";
	auto enc1 = client.encrypt(msg1, rec1);
	REQUIRE(enc1);
	auto enc2 = client.encrypt(msg2, rec2);
	REQUIRE(enc2);

	std::vector<std::byte> combined;
	combined.insert(combined.end(), rec1.data(), rec1.data() + enc1->produced);
	combined.insert(combined.end(), rec2.data(), rec2.data() + enc2->produced);

	// First decrypt must yield exactly the first record's payload and consume exactly the
	// first record, leaving the second untouched for a follow-up call.
	std::array<std::byte, 2048> plain1{};
	auto dec1 = server.decrypt(std::span<const std::byte>{combined}, plain1);
	REQUIRE(dec1);
	CHECK(dec1->produced == msg1.size());
	CHECK(std::memcmp(plain1.data(), msg1.data(), msg1.size()) == 0);
	CHECK(dec1->consumed == enc1->produced);

	// Second decrypt on the remainder yields the second record.
	const std::span<const std::byte> rest{combined.data() + dec1->consumed, combined.size() - dec1->consumed};
	std::array<std::byte, 2048> plain2{};
	auto dec2 = server.decrypt(rest, plain2);
	REQUIRE(dec2);
	CHECK(dec2->produced == msg2.size());
	CHECK(std::memcmp(plain2.data(), msg2.data(), msg2.size()) == 0);
}

TEST_CASE("crypto/secure_channel/stream/self_signed")
{
	auto self = load_cert(test_cert::self_signed);
	const std::array<certificate, 1> chain{self};

	// self_signed test_cert does not carry a private key; we need an identity
	// with a key for the acceptor. Use the PKCS#12 chain leaf as identity, and
	// then run a connector with empty trust to exercise verify_relax path.
	auto pkcs12 = load_pkcs12_chain();
	REQUIRE_FALSE(pkcs12.empty());
	auto leaf_key = pkcs12.front().private_key();
	REQUIRE(leaf_key);

	auto acc = stream_acceptor::make({.certificate_chain = pkcs12, .private_key = *leaf_key});
	REQUIRE(acc);

	// Empty trust + use_system_trust=false → strict verification fails by default.
	auto cn_strict = stream_connector::make({.trusted_roots = {}, .use_system_trust = false});
	REQUIRE(cn_strict);

	auto strict_client = cn_strict->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(strict_client);
	auto strict_server = acc->accept();
	REQUIRE(strict_server);

	std::vector<std::byte> c2s(size_t{16} * 1024);
	std::vector<std::byte> s2c(size_t{16} * 1024);
	size_t c2s_size = 0;
	size_t s2c_size = 0;
	std::error_code last_err;

	for (int i = 0; i < 32; ++i)
	{
		auto cr = strict_client->step(
			std::span{s2c.data(), s2c_size}, std::span{c2s.data() + c2s_size, c2s.size() - c2s_size}
		);
		if (!cr)
		{
			last_err = cr.error();
			break;
		}
		s2c.erase(
			s2c.begin(), s2c.begin() + static_cast<std::vector<std::byte>::difference_type>(cr->consumed)
		);
		s2c_size -= cr->consumed;
		c2s_size += cr->produced;

		auto sr = strict_server->step(
			std::span{c2s.data(), c2s_size}, std::span{s2c.data() + s2c_size, s2c.size() - s2c_size}
		);
		if (!sr)
		{
			last_err = sr.error();
			break;
		}
		c2s.erase(
			c2s.begin(), c2s.begin() + static_cast<std::vector<std::byte>::difference_type>(sr->consumed)
		);
		c2s_size -= sr->consumed;
		s2c_size += sr->produced;
	}

	CHECK(last_err == secure_channel_errc::peer_verification_failed);
}

TEST_CASE("crypto/secure_channel/stream/alpn")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	std::array<std::string_view, 2> server_protos{"h2", "http/1.1"};
	std::array<std::string_view, 1> client_protos{"http/1.1"};

	auto acc = stream_acceptor::make({
		.certificate_chain = chain,
		.private_key = *leaf_key,
		.supported_protocols = server_protos,
	});
	REQUIRE(acc);
	auto cn = stream_connector::make({
		.trusted_roots = roots,
		.use_system_trust = false,
		.supported_protocols = client_protos,
	});
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	auto [client, server] = pump_handshake<stream_acceptor>(*cn_hs, *ac_hs);

	CHECK(client.selected_protocol() == "http/1.1");
	CHECK(server.selected_protocol() == "http/1.1");
}

TEST_CASE("crypto/secure_channel/stream/alpn_mismatch")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	std::array<std::string_view, 1> server_protos{"h2"};
	std::array<std::string_view, 1> client_protos{"smtp"};

	auto acc = stream_acceptor::make({
		.certificate_chain = chain,
		.private_key = *leaf_key,
		.supported_protocols = server_protos,
	});
	REQUIRE(acc);
	auto cn = stream_connector::make({
		.trusted_roots = roots,
		.use_system_trust = false,
		.supported_protocols = client_protos,
	});
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	std::vector<std::byte> c2s(size_t{16} * 1024);
	std::vector<std::byte> s2c(size_t{16} * 1024);
	size_t c2s_size = 0;
	size_t s2c_size = 0;
	std::error_code last_err;

	for (int i = 0; i < 32; ++i)
	{
		auto cr = cn_hs->step(
			std::span{s2c.data(), s2c_size}, std::span{c2s.data() + c2s_size, c2s.size() - c2s_size}
		);
		if (!cr)
		{
			last_err = cr.error();
			break;
		}
		s2c.erase(
			s2c.begin(), s2c.begin() + static_cast<std::vector<std::byte>::difference_type>(cr->consumed)
		);
		s2c_size -= cr->consumed;
		c2s_size += cr->produced;

		auto sr = ac_hs->step(
			std::span{c2s.data(), c2s_size}, std::span{s2c.data() + s2c_size, s2c.size() - s2c_size}
		);
		if (!sr)
		{
			last_err = sr.error();
			break;
		}
		c2s.erase(
			c2s.begin(), c2s.begin() + static_cast<std::vector<std::byte>::difference_type>(sr->consumed)
		);
		c2s_size -= sr->consumed;
		s2c_size += sr->produced;
	}

	CHECK(last_err == secure_channel_errc::no_application_protocol);
}

TEST_CASE("crypto/secure_channel/stream/peer_certificate")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	auto acc = stream_acceptor::make({.certificate_chain = chain, .private_key = *leaf_key});
	REQUIRE(acc);
	auto cn = stream_connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	auto [client, server] = pump_handshake<stream_acceptor>(*cn_hs, *ac_hs);

	auto peer_on_client = client.peer_certificate();
	REQUIRE(peer_on_client);
	CHECK_FALSE(peer_on_client->is_null());
	CHECK(peer_on_client->common_name() == "pal.alt.ee");

	// No mTLS: server should see no peer cert.
	auto peer_on_server = server.peer_certificate();
	REQUIRE(peer_on_server);
	CHECK(peer_on_server->is_null());
}

TEST_CASE("crypto/secure_channel/stream/mtls_missing_client_cert")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	auto acc = stream_acceptor::make({
		.certificate_chain = chain,
		.private_key = *leaf_key,
		.require_client_certificate = true,
		.trusted_roots = roots,
	});
	REQUIRE(acc);
	auto cn = stream_connector::make({
		.trusted_roots = roots,
		.use_system_trust = false,
	});
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	std::vector<std::byte> c2s(size_t{16} * 1024);
	std::vector<std::byte> s2c(size_t{16} * 1024);
	size_t c2s_size = 0;
	size_t s2c_size = 0;
	std::error_code last_err;

	for (int i = 0; i < 32; ++i)
	{
		auto cr = cn_hs->step(
			std::span{s2c.data(), s2c_size}, std::span{c2s.data() + c2s_size, c2s.size() - c2s_size}
		);
		if (!cr)
		{
			last_err = cr.error();
			break;
		}
		s2c.erase(
			s2c.begin(), s2c.begin() + static_cast<std::vector<std::byte>::difference_type>(cr->consumed)
		);
		s2c_size -= cr->consumed;
		c2s_size += cr->produced;

		auto sr = ac_hs->step(
			std::span{c2s.data(), c2s_size}, std::span{s2c.data() + s2c_size, s2c.size() - s2c_size}
		);
		if (!sr)
		{
			last_err = sr.error();
			break;
		}
		c2s.erase(
			c2s.begin(), c2s.begin() + static_cast<std::vector<std::byte>::difference_type>(sr->consumed)
		);
		c2s_size -= sr->consumed;
		s2c_size += sr->produced;
	}

	CHECK((last_err == secure_channel_errc::client_certificate_required
	       || last_err == secure_channel_errc::peer_verification_failed
	       || last_err == secure_channel_errc::handshake_failed));
}

TEST_CASE("crypto/secure_channel/connected_channel/encrypt_after_close")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	auto acc = stream_acceptor::make({.certificate_chain = chain, .private_key = *leaf_key});
	REQUIRE(acc);
	auto cn = stream_connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	auto [client, server] = pump_handshake<stream_acceptor>(*cn_hs, *ac_hs);

	std::array<std::byte, 256> close_buf{};
	auto close_r = client.close(close_buf);
	REQUIRE(close_r);

	std::array<std::byte, 256> enc_buf{};
	const std::string_view msg = "post-close";
	auto enc = client.encrypt(msg, enc_buf);
	REQUIRE_FALSE(enc);
	CHECK(enc.error() == secure_channel_errc::closed);
}

TEST_CASE("crypto/secure_channel/datagram/message_too_large")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	auto acc = datagram_acceptor::make({.certificate_chain = chain, .private_key = *leaf_key});
	REQUIRE(acc);
	auto cn = datagram_connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	auto [client, server] = pump_handshake<datagram_acceptor>(*cn_hs, *ac_hs);

	const size_t cap = client.max_message_size();
	REQUIRE(cap > 0);
	const std::vector<std::byte> oversized(cap + 1, std::byte{0});
	std::array<std::byte, 4096> out{};

	auto r = client.encrypt(oversized, out);
	REQUIRE_FALSE(r);
	CHECK(r.error() == secure_channel_errc::message_too_large);
}

TEST_CASE("crypto/secure_channel/stream/handshake_handoff_exactly_once")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	auto acc = stream_acceptor::make({.certificate_chain = chain, .private_key = *leaf_key});
	REQUIRE(acc);
	auto cn = stream_connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	std::vector<std::byte> c2s(size_t{16} * 1024);
	std::vector<std::byte> s2c(size_t{16} * 1024);
	size_t c2s_size = 0;
	size_t s2c_size = 0;

	int client_connected_count = 0;
	int server_connected_count = 0;
	std::optional<connected_channel> client;
	std::optional<connected_channel> server;

	for (int i = 0; i < 32; ++i)
	{
		if (*cn_hs)
		{
			auto r = cn_hs->step(
				std::span{s2c.data(), s2c_size}, std::span{c2s.data() + c2s_size, c2s.size() - c2s_size}
			);
			REQUIRE(r);
			s2c.erase(
				s2c.begin(),
				s2c.begin() + static_cast<std::vector<std::byte>::difference_type>(r->consumed)
			);
			s2c_size -= r->consumed;
			c2s_size += r->produced;
			if (r->connected)
			{
				++client_connected_count;
				client.emplace(std::move(*r->connected));
				// After hand-off, handshake_channel is null.
				CHECK(cn_hs->is_null());
			}
		}
		if (*ac_hs)
		{
			auto r = ac_hs->step(
				std::span{c2s.data(), c2s_size}, std::span{s2c.data() + s2c_size, s2c.size() - s2c_size}
			);
			REQUIRE(r);
			c2s.erase(
				c2s.begin(),
				c2s.begin() + static_cast<std::vector<std::byte>::difference_type>(r->consumed)
			);
			c2s_size -= r->consumed;
			s2c_size += r->produced;
			if (r->connected)
			{
				++server_connected_count;
				server.emplace(std::move(*r->connected));
				CHECK(ac_hs->is_null());
			}
		}
		if (client && server)
		{
			break;
		}
	}

	CHECK(client_connected_count == 1);
	CHECK(server_connected_count == 1);
	REQUIRE(client.has_value());
	REQUIRE(server.has_value());
}

TEST_CASE("crypto/secure_channel/stream/decrypt_drain")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	auto acc = stream_acceptor::make({.certificate_chain = chain, .private_key = *leaf_key});
	REQUIRE(acc);
	auto cn = stream_connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	auto [client, server] = pump_handshake<stream_acceptor>(*cn_hs, *ac_hs);

	// Encrypt a large message; decrypt with a small output buffer to exercise
	// drain mode.
	const std::vector<std::byte> msg(size_t{8} * 1024, std::byte{0x42});
	std::vector<std::byte> cipher(size_t{32} * 1024);
	auto enc = client.encrypt(msg, cipher);
	REQUIRE(enc);
	REQUIRE(enc->consumed == msg.size());

	std::array<std::byte, 128> small_out{};
	size_t total = 0;
	std::span<const std::byte> remaining{cipher.data(), enc->produced};

	for (int i = 0; i < 256 && total < msg.size(); ++i)
	{
		auto dec = remaining.empty() ? server.decrypt(small_out) : server.decrypt(remaining, small_out);
		REQUIRE(dec);
		if (dec->produced > 0)
		{
			total += dec->produced;
		}
		remaining = remaining.subspan(dec->consumed);
		if (remaining.empty() && dec->produced == 0)
		{
			break;
		}
	}

	CHECK(total == msg.size());
}

TEST_CASE("crypto/secure_channel/stream/encrypt_partial_output")
{
	auto chain = load_pkcs12_chain();
	REQUIRE_FALSE(chain.empty());
	auto leaf_key = chain.front().private_key();
	REQUIRE(leaf_key);

	auto ca = load_cert(test_cert::ca);
	std::array<certificate, 1> roots{ca};

	auto acc = stream_acceptor::make({.certificate_chain = chain, .private_key = *leaf_key});
	REQUIRE(acc);
	auto cn = stream_connector::make({.trusted_roots = roots, .use_system_trust = false});
	REQUIRE(cn);

	auto cn_hs = cn->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(cn_hs);
	auto ac_hs = acc->accept();
	REQUIRE(ac_hs);

	auto [client, server] = pump_handshake<stream_acceptor>(*cn_hs, *ac_hs);

	// Multi-record message (> one 16 KiB TLS record) encrypted into an output buffer that holds only
	// about one record. With SSL_MODE_ENABLE_PARTIAL_WRITE the caller advances by consumed each call,
	// so every call makes forward progress (consumed > 0); without partial writes consumed would be 0.
	const std::vector<std::byte> msg(size_t{40} * 1000, std::byte{0x42});
	std::array<std::byte, 20000> out{};
	std::vector<std::byte> cipher;

	size_t consumed = 0;
	for (int i = 0; i < 64 && consumed < msg.size(); ++i)
	{
		auto enc = client.encrypt(std::span{msg}.subspan(consumed), out);
		REQUIRE(enc);
		CHECK(enc->consumed > 0);
		cipher.insert(cipher.end(), out.data(), out.data() + enc->produced);
		consumed += enc->consumed;
		CHECK(enc->want_output == (consumed < msg.size()));
	}
	REQUIRE(consumed == msg.size());

	// The reassembled ciphertext must decrypt back to the original message.
	std::vector<std::byte> plain;
	std::array<std::byte, 16384> dout{};
	std::span<const std::byte> rem{cipher};
	for (int i = 0; i < 64 && plain.size() < msg.size(); ++i)
	{
		auto dec = rem.empty() ? server.decrypt(dout) : server.decrypt(rem, dout);
		REQUIRE(dec);
		plain.insert(plain.end(), dout.data(), dout.data() + dec->produced);
		rem = rem.subspan(dec->consumed);
		if (rem.empty() && dec->produced == 0)
		{
			break;
		}
	}
	CHECK(plain.size() == msg.size());
	CHECK(plain == msg);
}
