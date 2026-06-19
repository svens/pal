#include <pal/crypto/tls_wire.hpp>
#include <pal/crypto/secure_channel.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <vector>

namespace
{

using namespace pal::crypto;

template <typename Connector>
std::vector<std::byte> make_client_hello ()
{
	auto connector = Connector::make({.use_system_trust = false});
	REQUIRE(connector);
	auto handshake = connector->connect({.peer_name = "server.pal.alt.ee"});
	REQUIRE(handshake);

	constexpr auto io_buffer_size = 16 * 1024;
	std::vector<std::byte> hello(io_buffer_size);
	auto r = handshake->step(hello);
	REQUIRE(r);
	REQUIRE(r->produced > 0);
	hello.resize(r->produced);
	return hello;
}

TEST_CASE("crypto/tls_wire")
{
	SECTION("verify_relax")
	{
		SECTION("any")
		{
			CHECK_FALSE(any(verify_relax::none));
			CHECK(any(verify_relax::self_signed));
		}

		SECTION("combine")
		{
			CHECK((verify_relax::none | verify_relax::self_signed) == verify_relax::self_signed);
			CHECK((verify_relax::self_signed | verify_relax::none) == verify_relax::self_signed);
		}

		SECTION("mask")
		{
			CHECK((verify_relax::self_signed & verify_relax::self_signed) == verify_relax::self_signed);
			CHECK((verify_relax::self_signed & verify_relax::none) == verify_relax::none);
		}

		SECTION("clear")
		{
			CHECK((verify_relax::self_signed & ~verify_relax::self_signed) == verify_relax::none);
		}
	}

	SECTION("sniff_stream")
	{
		SECTION("empty")
		{
			CHECK(sniff_stream(std::span<const std::byte>{}) == wire_protocol::need_more);
		}

		SECTION("plaintext")
		{
			// A realistic non-TLS payload (HTTP request) multiplexed on the same port classifies as plain.
			CHECK(sniff_stream(std::string_view{"GET / "}) == wire_protocol::plain);
		}

		SECTION("client_hello")
		{
			const auto hello = make_client_hello<stream_connector>();
			CHECK(sniff_stream(hello) == wire_protocol::tls);

			// Inconclusive until the full 6-byte record header is buffered.
			CHECK(sniff_stream(std::span{hello}.first(2)) == wire_protocol::need_more);
			CHECK(sniff_stream(std::span{hello}.first(5)) == wire_protocol::need_more);

			// Corrupt one gated header field at a time; each demotes an otherwise-valid record to plain.
			const auto corrupt = [&] (size_t i, uint8_t v)
			{
				auto bytes = hello;
				bytes[i] = std::byte{v};
				return bytes;
			};

			// not a handshake record
			CHECK(sniff_stream(corrupt(0, 0x17)) == wire_protocol::plain);

			// major version
			CHECK(sniff_stream(corrupt(1, 0x05)) == wire_protocol::plain);

			// minor version out of range
			CHECK(sniff_stream(corrupt(2, 0x99)) == wire_protocol::plain);

			// handshake type != ClientHello
			CHECK(sniff_stream(corrupt(5, 0x02)) == wire_protocol::plain);
		}
	}

	SECTION("sniff_datagram")
	{
		SECTION("too_short")
		{
			const std::array<std::byte, 8> bytes{};
			CHECK(sniff_datagram(bytes) == wire_protocol::plain);
		}

		SECTION("client_hello")
		{
			const auto hello = make_client_hello<datagram_connector>();
			CHECK(sniff_datagram(hello) == wire_protocol::dtls);

			// A datagram shorter than the 13-byte record header is never DTLS.
			CHECK(sniff_datagram(std::span{hello}.first(12)) == wire_protocol::plain);

			// Corrupt one gated header field at a time; each demotes an otherwise-valid record to plain.
			const auto corrupt = [&] (size_t i, uint8_t v)
			{
				auto bytes = hello;
				bytes[i] = std::byte{v};
				return bytes;
			};

			// not a handshake record
			CHECK(sniff_datagram(corrupt(0, 0x17)) == wire_protocol::plain);

			// version too low
			CHECK(sniff_datagram(corrupt(2, 0x00)) == wire_protocol::plain);

			// epoch != 0
			CHECK(sniff_datagram(corrupt(3, 0x01)) == wire_protocol::plain);

			// handshake type != ClientHello
			CHECK(sniff_datagram(corrupt(13, 0x02)) == wire_protocol::plain);
		}
	}

	SECTION("dtls_record_size")
	{
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

		SECTION("short_buffer")
		{
			const std::array<std::byte, 8> bytes{};
			CHECK(dtls_record_size(bytes) == bytes.size());
		}

		SECTION("empty_payload")
		{
			const auto bytes = make_record(0, 13);
			CHECK(dtls_record_size(bytes) == 13);
		}

		SECTION("exact_record")
		{
			// 13 header + 4 payload
			const auto bytes = make_record(4, 17);
			CHECK(dtls_record_size(bytes) == 17);
		}

		SECTION("first_of_many")
		{
			// first record is 17, trailing bytes follow
			const auto bytes = make_record(4, 30);
			CHECK(dtls_record_size(bytes) == 17);
		}

		SECTION("length_overruns")
		{
			// claims 113, only 17 present
			const auto bytes = make_record(100, 17);
			CHECK(dtls_record_size(bytes) == bytes.size());
		}
	}
}

// }}}1

} // namespace
