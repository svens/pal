#include <pal/crypto/certificate_filter.hpp>
#include <pal/crypto/certificate_store.hpp>
#include <pal/crypto/test.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

namespace
{

using pal::crypto::certificate;
using pal::crypto::certificate_store;
namespace cf = pal::crypto::certificate_filter;
namespace test_cert = pal_test::cert;

certificate_store load_store ()
{
	return certificate_store::from_pkcs12(test_cert::pkcs12_data).value();
}

TEST_CASE("crypto/certificate_filter")
{
	const auto store = load_store();

	SECTION("with_common_name")
	{
		SECTION("match")
		{
			const auto cert = store.find_first(cf::with_common_name("pal.alt.ee"));
			REQUIRE(cert);
			CHECK(cert.fingerprint() == test_cert::server.fingerprint);
		}

		SECTION("no_match")
		{
			const auto cert = store.find_first(cf::with_common_name("nonexistent.example"));
			CHECK(cert.is_null());
		}

		SECTION("case_sensitive")
		{
			// CN match is byte-equal; no case folding.
			const auto cert = store.find_first(cf::with_common_name("PAL.ALT.EE"));
			CHECK(cert.is_null());
		}
	}

	SECTION("with_thumbprint")
	{
		SECTION("match")
		{
			const auto cert = store.find_first(cf::with_thumbprint(test_cert::server.fingerprint));
			REQUIRE(cert);
			CHECK(cert.common_name() == test_cert::server.common_name);
		}

		SECTION("no_match")
		{
			const auto cert =
				store.find_first(cf::with_thumbprint("0000000000000000000000000000000000000000"));
			CHECK(cert.is_null());
		}

		SECTION("strict_uppercase_rejected")
		{
			// Strict policy: input must be canonical lowercase hex.
			std::string upper{test_cert::server.fingerprint};
			std::ranges::transform(
				upper,
				upper.begin(),
				[] (char c) { return static_cast<char>((c >= 'a' && c <= 'f') ? c - ('a' - 'A') : c); }
			);
			const auto cert = store.find_first(cf::with_thumbprint(upper));
			CHECK(cert.is_null());
		}

		SECTION("strict_separators_rejected")
		{
			const auto cert = store.find_first(cf::with_thumbprint("8d:35:13:e3:5a:17"));
			CHECK(cert.is_null());
		}
	}

	SECTION("with_fqdn")
	{
		SECTION("exact_san_match")
		{
			const auto cert = store.find_first(cf::with_fqdn("server.pal.alt.ee"));
			REQUIRE(cert);
			CHECK(cert.fingerprint() == test_cert::server.fingerprint);
		}

		SECTION("wildcard_san_match")
		{
			// SAN contains "*.pal.alt.ee" — matches single-level subdomain.
			const auto cert = store.find_first(cf::with_fqdn("api.pal.alt.ee"));
			REQUIRE(cert);
			CHECK(cert.fingerprint() == test_cert::server.fingerprint);
		}

		SECTION("wildcard_does_not_cross_dots")
		{
			// "*.pal.alt.ee" must NOT match "a.b.pal.alt.ee".
			const auto cert = store.find_first(cf::with_fqdn("a.b.pal.alt.ee"));
			CHECK(cert.is_null());
		}

		SECTION("no_match")
		{
			const auto cert = store.find_first(cf::with_fqdn("other.example.com"));
			CHECK(cert.is_null());
		}
	}

	SECTION("with_ip")
	{
		SECTION("match_v4")
		{
			const auto cert = store.find_first(cf::with_ip("1.2.3.4"));
			REQUIRE(cert);
			CHECK(cert.fingerprint() == test_cert::server.fingerprint);
		}

		SECTION("no_match")
		{
			const auto cert = store.find_first(cf::with_ip("9.9.9.9"));
			CHECK(cert.is_null());
		}
	}

	SECTION("has_private_key")
	{
		SECTION("matches_leaf")
		{
			const auto cert = store.find_first(cf::has_private_key());
			REQUIRE(cert);
			// PKCS#12 import: only the leaf carries a key.
			CHECK(cert.fingerprint() == test_cert::server.fingerprint);
		}

		SECTION("count_via_filter")
		{
			const auto count = std::ranges::count_if(store, cf::has_private_key());
			CHECK(count == 1);
		}
	}

	SECTION("is_self_signed")
	{
		SECTION("match")
		{
			const auto cert = store.find_first(cf::is_self_signed());
			REQUIRE(cert);
			// CA root is the only self-signed cert in the bundle.
			CHECK(cert.fingerprint() == test_cert::ca.fingerprint);
		}
	}

	SECTION("not_expired_at")
	{
		SECTION("now_matches_valid_certs")
		{
			const auto count = std::ranges::count_if(store, cf::not_expired_at());
			CHECK(count == 4); // all test fixture certs are valid at "now"
		}

		SECTION("far_future_matches_none")
		{
			using namespace std::chrono;
			const auto when = certificate::clock_type::now() + hours{24 * 365 * 100};
			const auto count = std::ranges::count_if(store, cf::not_expired_at(when));
			CHECK(count == 0);
		}
	}

	SECTION("is_issued_by")
	{
		const auto ca = store.find_first(cf::with_thumbprint(test_cert::ca.fingerprint));
		REQUIRE(ca);

		SECTION("intermediate_issued_by_ca")
		{
			// The CA is self-signed, so it also matches is_issued_by(ca); we want
			// the intermediate. Compose: issued by CA but not self-signed.
			auto issued_by_ca = cf::is_issued_by(ca);
			auto self_signed = cf::is_self_signed();

			// clang-format off
			const auto cert = store.find_first([issued_by_ca, self_signed] (const certificate &c) noexcept
			{
				return issued_by_ca(c) && !self_signed(c);
			});
			// clang-format on

			REQUIRE(cert);
			CHECK(cert.fingerprint() == test_cert::intermediate.fingerprint);
		}

		SECTION("ca_matches_itself")
		{
			// Self-signed cert is, by definition, issued by itself.
			const auto cert = store.find_first(cf::is_issued_by(ca));
			REQUIRE(cert);
			// Either the CA itself or whatever it issued can appear first in
			// iteration order; verify the predicate works without locking down
			// which specific cert was first.
			const auto issuer_matches = cert.fingerprint() == test_cert::ca.fingerprint
				|| cert.fingerprint() == test_cert::intermediate.fingerprint;
			CHECK(issuer_matches);
		}
	}

	SECTION("composition")
	{
		SECTION("views_filter")
		{
			// Multi-match via std::views::filter, as the design intends.
			std::vector<std::string_view> fps;
			for (const auto &c: store | std::views::filter(cf::not_expired_at()))
			{
				fps.push_back(c.fingerprint());
			}
			CHECK(fps.size() == 4);
		}

		SECTION("logical_and")
		{
			// "Leaf with a private key valid right now" — typical real-world query.
			auto pred = [] (const certificate &c) noexcept
			{
				return c.private_key().has_value() && c.not_expired_at(certificate::clock_type::now());
			};
			const auto cert = store.find_first(pred);
			REQUIRE(cert);
			CHECK(cert.fingerprint() == test_cert::server.fingerprint);
		}
	}
}

TEST_CASE("crypto/certificate_store/find_first")
{
	SECTION("empty_store")
	{
		certificate_store store;
		const auto cert = store.find_first(cf::is_self_signed());
		CHECK(cert.is_null());
	}

	SECTION("no_match")
	{
		const auto store = load_store();
		const auto cert = store.find_first([] (const certificate &) noexcept { return false; });
		CHECK(cert.is_null());
	}

	SECTION("first_match_returned")
	{
		const auto store = load_store();
		// Always-true predicate must return the head of iteration.
		const auto cert = store.find_first([] (const certificate &) noexcept { return true; });
		REQUIRE(cert);
		CHECK(cert.fingerprint() == test_cert::server.fingerprint);
	}
}

} // namespace
