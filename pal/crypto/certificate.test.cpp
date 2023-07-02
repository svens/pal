#include <pal/crypto/certificate>
#include <pal/crypto/test>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {

using pal::crypto::certificate;
namespace test_cert = pal_test::cert;
using namespace std::chrono_literals;

certificate::time_type now () noexcept
{
	return certificate::clock_type::now();
}

certificate::time_type far_past () noexcept
{
	return (certificate::time_type::min)() + 24h;
}

certificate::time_type far_future () noexcept
{
	return (certificate::time_type::max)() - 24h;
}

TEST_CASE("crypto/certificate")
{
	SECTION("default") //{{{1
	{
		certificate c;
		CHECK(c.is_null());
	}

	SECTION("from") //{{{1
	{
		auto [pem, version, common_name, fingerprint] = GENERATE(
			table<std::string_view, int, std::string_view, std::string_view>(
			{
				{
					test_cert::ca_pem,
					3,
					"PAL Root CA",
					"f89bd9fc4050afa352104e7ec4e06c7f5237f88c"
				},
				{
					test_cert::intermediate_pem,
					3,
					"PAL Intermediate CA",
					"919bbbf95b543f58498c5300e65b857aa7ff83b8"
				},
				{
					test_cert::server_pem,
					3,
					"pal.alt.ee",
					"3bd31e7b696c888cd9b54aab6e31ce5b2636d271"
				},
				{
					test_cert::client_pem,
					3,
					"pal.alt.ee",
					"a1e42e5a8a5af09fa70cf57524f8214f7b027352"
				},
			})
		);

		SECTION("pem")
		{
			auto c = certificate::from_pem(pem);
			REQUIRE(c);
			CHECK(c->version() == version);
			CHECK(c->common_name() == common_name);
			CHECK(c->fingerprint() == fingerprint);
		}

		SECTION("der")
		{
			auto c = certificate::from_der(pal_test::to_der(pem));
			REQUIRE(c);
			CHECK(c->version() == version);
			CHECK(c->common_name() == common_name);
			CHECK(c->fingerprint() == fingerprint);
		}
	}

	SECTION("from_pem: first from multiple") //{{{1
	{
		std::string pem{test_cert::client_pem};
		pem += test_cert::server_pem;
		auto c = certificate::from_pem(pem);
		REQUIRE(c);
	}

	SECTION("from_pem: invalid_argument") //{{{1
	{
		std::string pem{test_cert::client_pem};

		SECTION("empty")
		{
			pem.clear();
		}

		SECTION("invalid header")
		{
			pem.replace(pem.find("BEGIN"), 5, "");
		}

		SECTION("invalid footer")
		{
			pem.replace(pem.find("END"), 3, "");
		}

		SECTION("invalid base64")
		{
			SECTION("bad character")
			{
				pem[pem.size() / 2] = '-';
			}

			SECTION("misplaced padding")
			{
				pem[pem.size() / 2] = '=';
			}
		}

		SECTION("invalid content")
		{
			pem.insert(pem.size() / 2, 8, 'X');
		}

		auto c = certificate::from_pem(pem);
		REQUIRE_FALSE(c);
		CHECK(c.error() == std::errc::invalid_argument);
	}

	SECTION("from_pem: not enough memory") //{{{1
	{
		std::string pem{test_cert::client_pem};
		pem.insert(pem.size() / 2, (16 * 1024) / 3 * 4, '\n');
		pal_test::bad_alloc_once x;
		auto c = certificate::from_pem(pem);
		REQUIRE_FALSE(c);
		CHECK(c.error() == std::errc::not_enough_memory);
	}

	SECTION("from_der: invalid_argument") //{{{1
	{
		std::string der = pal_test::to_der(test_cert::client_pem);;

		SECTION("empty")
		{
			der.clear();
		}

		SECTION("invalid header")
		{
			der.erase(der.begin());
		}

		SECTION("invalid footer")
		{
			der.pop_back();
		}

		SECTION("invalid content")
		{
			der.insert(der.size() / 2, 8, 'X');
		}

		auto c = certificate::from_der(der);
		REQUIRE_FALSE(c);
		CHECK(c.error() == std::errc::invalid_argument);
	}

	SECTION("from_der: not_enough_memory") //{{{1
	{
		auto der = pal_test::to_der(test_cert::client_pem);
		pal_test::bad_alloc_once x;
		auto c = certificate::from_der(der);
		REQUIRE_FALSE(c);
		CHECK(c.error() == std::errc::not_enough_memory);
	}

	SECTION("expired") //{{{1
	{
		auto c = certificate::from_pem(GENERATE
		(
		    test_cert::ca_pem,
		    test_cert::intermediate_pem,
		    test_cert::server_pem,
		    test_cert::client_pem
		));
		REQUIRE(c);
		CAPTURE(c->fingerprint());

		SECTION("not_before")
		{
			CHECK(c->not_before() != certificate::time_type{});
			CHECK(c->not_before() > far_past());
			CHECK(c->not_before() < now());
		}

		SECTION("not_after")
		{
			CHECK(c->not_after() != certificate::time_type{});
			CHECK(c->not_after() > now());
			CHECK(c->not_after() < far_future());
		}

		SECTION("not_expired_at")
		{
			CHECK(c->not_expired_at(now()));
			CHECK_FALSE(c->not_expired_at(far_past()));
			CHECK_FALSE(c->not_expired_at(far_future()));
		}

		SECTION("not_expired_for")
		{
			CHECK(c->not_expired_for(24h, now()));
			CHECK_FALSE(c->not_expired_for(24h * 365 * 100, now()));
			CHECK_FALSE(c->not_expired_for(24h, far_past()));
			CHECK_FALSE(c->not_expired_for(24h, far_future()));
		}
	}

	//}}}1
}

} // namespace
