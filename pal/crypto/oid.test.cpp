#include <pal/crypto/oid.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace
{

using namespace pal::crypto::oid;
using namespace std::string_view_literals;

static_assert(alias_or(common_name) == "CN");
static_assert(alias_or(country_name) == "C");
static_assert(alias_or("unknown"sv) == "unknown"sv);

TEST_CASE("crypto/oid")
{
	// clang-format off
	auto [oid, alias] = GENERATE(table<std::string_view, std::string_view>({
		{ common_name, "CN"sv },
		{ country_name, "C"sv },
		{ locality_name, "L"sv },
		{ organization_name, "O"sv },
		{ organizational_unit_name, "OU"sv },
		{ state_or_province_name, "ST"sv },
		{ street_address, "STREET"sv },
		{ "unknown"sv, "unknown"sv },
	}));
	// clang-format on

	CHECK(alias_or(oid) == alias);
}

} // namespace
