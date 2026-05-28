#include <pal/crypto/certificate.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <array>
#include <format>

namespace
{

namespace test_cert = pal_test::cert;
using namespace pal::crypto;

auto subject_san (const test_cert::info &info)
{
	return (*certificate::from_pem(info.pem)).subject_alternative_name().value();
}

TEST_CASE("crypto/alternative_name")
{
	SECTION("iterator/count")
	{
		// server cert has 6 SAN entries: 2 IP, 2 DNS, 1 email, 1 URI
		int n = 0;
		for ([[maybe_unused]] const auto &entry: subject_san(test_cert::server))
		{
			++n;
		}
		CHECK(n == 6);
	}

	SECTION("iterator/entry_types")
	{
		// first entry of server SAN is alt_name::ip("1.2.3.4")
		const auto an = subject_san(test_cert::server);
		auto it = an.begin();
		REQUIRE(it != std::default_sentinel);
		const auto *ip = std::get_if<alt_name::ip>(&*it);
		REQUIRE(ip != nullptr);
		CHECK(*ip == "1.2.3.4");
	}

	SECTION("iterator/arrow_operator")
	{
		const auto an = subject_san(test_cert::server);
		auto it = an.begin();
		REQUIRE(it != std::default_sentinel);
		const auto *ip = std::get_if<alt_name::ip>(it.operator->());
		REQUIRE(ip != nullptr);
		CHECK(*ip == "1.2.3.4");
	}

	SECTION("iterator/equality")
	{
		const auto an = subject_san(test_cert::server);
		auto a = an.begin();
		auto b = an.begin();
		CHECK(a == b);
		++a;
		CHECK(a != b);
	}

	SECTION("iterator/empty_range")
	{
		const auto an = subject_san(test_cert::self_signed);
		CHECK(an.begin() == std::default_sentinel);
	}

	SECTION("to_chars/fits")
	{
		// client SAN: "email=pal@alt.ee, dns=client.pal.alt.ee" (39 chars)
		// to_chars writes each entry + ", " (18+23=41 bytes) before stripping the last ", "
		const auto an = subject_san(test_cert::client);
		std::array<char, 41> buf{};
		auto [ptr, ec] = an.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(ec == std::errc{});
		CHECK(std::string_view{buf.data(), ptr} == "email=pal@alt.ee, dns=client.pal.alt.ee");
	}

	SECTION("to_chars/too_small")
	{
		const auto an = subject_san(test_cert::client);
		std::array<char, 40> buf{};
		auto [ptr, ec] = an.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(ec == std::errc::value_too_large);
		CHECK(ptr == buf.data() + buf.size());
	}

	SECTION("to_chars/empty")
	{
		const auto an = subject_san(test_cert::self_signed);
		std::array<char, 64> buf{};
		auto [ptr, ec] = an.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(ec == std::errc{});
		CHECK(std::string_view{buf.data(), ptr}.empty());
	}

	SECTION("format/bad_spec")
	{
		const auto an = subject_san(test_cert::server);
		CHECK_THROWS_AS(std::vformat("{:x}", std::make_format_args(an)), std::format_error);
	}

	SECTION("to_chars/all_certs_subject")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		CAPTURE(info->fingerprint);
		const auto an = subject_san(*info);
		std::array<char, 512> buf{};
		auto [ptr, ec] = an.to_chars(buf.data(), buf.data() + buf.size());
		REQUIRE(ec == std::errc{});
		CHECK(std::string_view{buf.data(), ptr} == info->subject_alternative_name);
	}

	SECTION("to_chars/all_certs_issuer")
	{
		const auto *info = GENERATE(from_range(test_cert::data));
		CAPTURE(info->fingerprint);
		const auto an = (*certificate::from_pem(info->pem)).issuer_alternative_name().value();
		std::array<char, 512> buf{};
		auto [ptr, ec] = an.to_chars(buf.data(), buf.data() + buf.size());
		REQUIRE(ec == std::errc{});
		CHECK(std::string_view{buf.data(), ptr} == info->issuer_alternative_name);
	}
}

} // namespace
