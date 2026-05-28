#include <pal/crypto/certificate.hpp>
#include <pal/crypto/oid.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <array>
#include <format>
#include <string>

namespace
{

namespace test_cert = pal_test::cert;
using namespace pal::crypto;

auto subject (const test_cert::info &info)
{
	return (*certificate::from_pem(info.pem)).subject_name();
}

TEST_CASE("crypto/distinguished_name")
{
	SECTION("iterator/encoding_order")
	{
		// encoding order is least-to-most-significant (C → ST → O → OU → CN)
		// no_cn: O=PAL, OU=PAL Test
		std::string result;
		for (auto [oid, value]: subject(test_cert::no_cn))
		{
			result += oid::alias_or(oid);
			result += '=';
			result += value;
			result += '\n';
		}
		CHECK(result == "O=PAL\nOU=PAL Test\n");
	}

	SECTION("iterator/oid_is_dotted_decimal")
	{
		// iterator yields raw dotted-decimal OIDs, alias_or maps them for display
		for (auto [oid, value]: subject(test_cert::self_signed))
		{
			CHECK(oid == oid::common_name);
			CHECK(value == "Test");
		}
	}

	SECTION("iterator/count")
	{
		int n = 0;
		for ([[maybe_unused]] auto entry: subject(test_cert::ca))
		{
			++n;
		}
		CHECK(n == 5); // C, ST, O, OU, CN
	}

	SECTION("iterator/arrow_operator")
	{
		const auto dn = subject(test_cert::self_signed);
		auto it = dn.begin();
		CHECK(it->oid == oid::common_name);
		CHECK(it->value == "Test");
	}

	SECTION("iterator/equality")
	{
		const auto dn = subject(test_cert::self_signed);
		auto a = dn.begin();
		auto b = dn.begin();
		CHECK(a == b);
		++a;
		CHECK(a != b);
		CHECK(a == std::default_sentinel);
	}

	SECTION("iterator/equality_diverged_position")
	{
		const auto dn = subject(test_cert::no_cn);
		auto a = dn.begin();
		auto b = dn.begin();
		CHECK(a == b);
		++a;
		CHECK(a != b);
	}

	SECTION("to_chars/fits")
	{
		// self_signed: "CN=Test" (7 chars); to_chars writes "CN=Test, " (9) then trims
		const auto dn = subject(test_cert::self_signed);
		std::array<char, 9> buf{};
		auto [ptr, ec] = dn.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(ec == std::errc{});
		CHECK(std::string_view{buf.data(), ptr} == "CN=Test");
	}

	SECTION("to_chars/too_small")
	{
		const auto dn = subject(test_cert::self_signed);
		std::array<char, 8> buf{};
		auto [ptr, ec] = dn.to_chars(buf.data(), buf.data() + buf.size());
		CHECK(ec == std::errc::value_too_large);
		CHECK(ptr == buf.data() + buf.size());
	}

	SECTION("format/bad_spec")
	{
		const auto dn = subject(test_cert::self_signed);
		CHECK_THROWS_AS(std::vformat("{:x}", std::make_format_args(dn)), std::format_error);
	}

	SECTION("to_chars/all_certs")
	{
		// to_chars output matches std::format for all test certs
		const auto *info = GENERATE(from_range(test_cert::data));
		CAPTURE(info->fingerprint);
		const auto dn = subject(*info);
		std::array<char, 512> buf{};
		auto [ptr, ec] = dn.to_chars(buf.data(), buf.data() + buf.size());
		REQUIRE(ec == std::errc{});
		CHECK(std::string_view{buf.data(), ptr} == info->subject_name);
	}
}

} // namespace
