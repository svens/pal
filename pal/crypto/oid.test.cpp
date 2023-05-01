#include <pal/crypto/oid>
#include <pal/crypto/test>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {

using namespace pal::crypto::oid;
using namespace std::string_view_literals;

TEST_CASE("crypto/oid")
{
	auto [oid, alias] = GENERATE(table<std::string_view, std::string_view>({
		{ common_name, "CN" },
		{ country_name, "C" },
		{ locality_name, "L" },
		{ organization_name, "O" },
		{ organizational_unit_name, "OU" },
		{ state_or_province_name, "ST" },
		{ street_address, "STREET" },
	}));
	CHECK(alias_or(oid) == alias);

	auto x = "unknown"sv;
	CHECK(alias_or(x) == x);
}

} // namespace
