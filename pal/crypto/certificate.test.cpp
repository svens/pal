#include <pal/crypto/certificate>
#include <pal/crypto/oid>
#include <pal/crypto/test>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <sstream>

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

std::string to_string (const pal::crypto::distinguished_name &name)
{
	std::ostringstream result;
	for (const auto &[oid, value]: name)
	{
		result << pal::crypto::oid::alias_or(oid) << '=' << value << '\n';
	}
	return result.str();
}

std::string to_string (const pal::crypto::alternative_name &name)
{
	std::ostringstream result;
	for (const auto &entry: name)
	{
		if (const auto *dns = std::get_if<pal::crypto::dns_name>(&entry))
		{
			result << "dns=" << *dns << '\n';
		}
		else if (const auto *email = std::get_if<pal::crypto::email_address>(&entry))
		{
			result << "email=" << *email << '\n';
		}
		else if (const auto *ip = std::get_if<pal::crypto::ip_address>(&entry))
		{
			result << "ip=" << *ip << '\n';
		}
		else if (const auto *uri = std::get_if<pal::crypto::uri>(&entry))
		{
			result << "uri=" << *uri << '\n';
		}
		else
		{
			result << "<unexpected>\n";
		}
	}
	return result.str();
}

TEST_CASE("crypto/certificate")
{
	SECTION("default") //{{{1
	{
		certificate c;
		CHECK(c.is_null());
	}

	SECTION("from_pem / from_der") //{{{1
	{
		const auto &data = GENERATE(from_range(test_cert::data));
		CAPTURE(data.fingerprint);

		auto c = certificate::from_pem(data.pem).value();
		CHECK(c.version() == data.version);
		CHECK(pal_test::to_hex(c.serial_number()) == data.serial_number);
		CHECK(c.common_name() == data.common_name);
		CHECK(c.fingerprint() == data.fingerprint);

		auto d = certificate::from_der(c.as_bytes()).value();
		CHECK(d.version() == data.version);
		CHECK(pal_test::to_hex(d.serial_number()) == data.serial_number);
		CHECK(d.common_name() == data.common_name);
		CHECK(d.fingerprint() == data.fingerprint);
	}

	SECTION("from_pem: first from multiple") //{{{1
	{
		std::string pem{test_cert::client.pem};
		pem += test_cert::server.pem;
		auto c = certificate::from_pem(pem);
		REQUIRE(c);
	}

	SECTION("from_pem: invalid_argument") //{{{1
	{
		std::string pem{test_cert::self_signed.pem};

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
		std::string pem{test_cert::self_signed.pem};
		pem.insert(pem.size() / 2, (16 * 1024) / 3 * 4, '\n');
		pal_test::bad_alloc_once x;
		auto c = certificate::from_pem(pem);
		REQUIRE_FALSE(c);
		CHECK(c.error() == std::errc::not_enough_memory);
	}

	SECTION("from_der: invalid_argument") //{{{1
	{
		std::string der = pal_test::to_der(test_cert::self_signed.pem);;

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
		auto der = pal_test::to_der(test_cert::self_signed.pem);
		pal_test::bad_alloc_once x;
		auto c = certificate::from_der(der);
		REQUIRE_FALSE(c);
		CHECK(c.error() == std::errc::not_enough_memory);
	}

	SECTION("expired") //{{{1
	{
		auto c = certificate::from_pem(GENERATE(from_range(test_cert::data)).pem);
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

	SECTION("is_issued_by / is_self_signed") //{{{1
	{
		auto ca = certificate::from_pem(test_cert::ca.pem).value();
		auto intermediate = certificate::from_pem(test_cert::intermediate.pem).value();
		auto server = certificate::from_pem(test_cert::server.pem).value();
		auto client = certificate::from_pem(test_cert::client.pem).value();

		CHECK(ca.is_self_signed());
		CHECK(ca.is_issued_by(ca));
		CHECK_FALSE(ca.is_issued_by(intermediate));
		CHECK_FALSE(ca.is_issued_by(server));
		CHECK_FALSE(ca.is_issued_by(client));

		CHECK_FALSE(intermediate.is_self_signed());
		CHECK(intermediate.is_issued_by(ca));
		CHECK_FALSE(intermediate.is_issued_by(intermediate));
		CHECK_FALSE(intermediate.is_issued_by(server));
		CHECK_FALSE(intermediate.is_issued_by(client));

		CHECK_FALSE(server.is_self_signed());
		CHECK_FALSE(server.is_issued_by(ca));
		CHECK(server.is_issued_by(intermediate));
		CHECK_FALSE(server.is_issued_by(server));
		CHECK_FALSE(server.is_issued_by(client));

		CHECK_FALSE(client.is_self_signed());
		CHECK_FALSE(client.is_issued_by(ca));
		CHECK(client.is_issued_by(intermediate));
		CHECK_FALSE(client.is_issued_by(server));
		CHECK_FALSE(client.is_issued_by(client));
	}

	SECTION("subject_name") //{{{1
	{
		const auto &data = GENERATE(from_range(test_cert::data));
		auto c = certificate::from_pem(data.pem).value();
		auto name = to_string(c.subject_name().value());
		CHECK(name == data.subject_name);
	}

	SECTION("subject_name: not_enough_memory") //{{{1
	{
		auto c = certificate::from_pem(test_cert::self_signed.pem).value();
		pal_test::bad_alloc_once x;
		auto name = c.subject_name();
		REQUIRE_FALSE(name);
		CHECK(name.error() == std::errc::not_enough_memory);
	}

	SECTION("issuer_name") //{{{1
	{
		const auto &data = GENERATE(from_range(test_cert::data));
		auto c = certificate::from_pem(data.pem).value();
		auto name = to_string(c.issuer_name().value());
		CHECK(name == data.issuer_name);
	}

	SECTION("issuer_name: not_enough_memory") //{{{1
	{
		auto c = certificate::from_pem(test_cert::self_signed.pem).value();
		pal_test::bad_alloc_once x;
		auto issuer_name = c.issuer_name();
		REQUIRE_FALSE(issuer_name);
		CHECK(issuer_name.error() == std::errc::not_enough_memory);
	}

	SECTION("issuer_alternative_name") //{{{1
	{
		const auto &data = GENERATE(from_range(test_cert::data));
		auto c = certificate::from_pem(data.pem).value();
		auto name = to_string(c.issuer_alternative_name().value());
		CHECK(name == data.issuer_alternative_name);
	}

	SECTION("issuer_alternative_name: not_enough_memory") //{{{1
	{
		auto c = certificate::from_pem(test_cert::server.pem).value();
		pal_test::bad_alloc_once x;
		auto name = c.issuer_alternative_name();
		REQUIRE_FALSE(name);
		CHECK(name.error() == std::errc::not_enough_memory);
	}

	SECTION("subject_alternative_name") //{{{1
	{
		const auto &data = GENERATE(from_range(test_cert::data));
		auto c = certificate::from_pem(data.pem).value();
		auto name = to_string(c.subject_alternative_name().value());
		CHECK(name == data.subject_alternative_name);
	}

	SECTION("subject_alternative_name: not_enough_memory") //{{{1
	{
		auto c = certificate::from_pem(test_cert::server.pem).value();
		pal_test::bad_alloc_once x;
		auto name = c.subject_alternative_name();
		REQUIRE_FALSE(name);
		CHECK(name.error() == std::errc::not_enough_memory);
	}

	SECTION("subject_alternative_name_values") //{{{1
	{
		{
			auto san = certificate::from_pem(test_cert::server.pem).value().subject_alternative_name_values();

			CHECK(san.contains("server.pal.alt.ee"));
			CHECK(san.contains("client.pal.alt.ee"));

			CHECK_FALSE(san.contains("ee"));
			CHECK_FALSE(san.contains(".ee"));
			CHECK_FALSE(san.contains("alt.ee"));
			CHECK_FALSE(san.contains(".alt.ee"));
			CHECK_FALSE(san.contains("pal.alt.ee"));
			CHECK_FALSE(san.contains(".pal.alt.ee"));
			CHECK_FALSE(san.contains("*.pal.alt.ee"));
			CHECK_FALSE(san.contains("subdomain1.subdomain2.pal.alt.ee"));
		}

		{
			auto san = certificate::from_pem(test_cert::client.pem).value().subject_alternative_name_values();

			CHECK(san.contains("client.pal.alt.ee"));

			CHECK_FALSE(san.contains("server.pal.alt.ee"));
			CHECK_FALSE(san.contains("ee"));
			CHECK_FALSE(san.contains(".ee"));
			CHECK_FALSE(san.contains("alt.ee"));
			CHECK_FALSE(san.contains(".alt.ee"));
			CHECK_FALSE(san.contains("pal.alt.ee"));
			CHECK_FALSE(san.contains(".pal.alt.ee"));
			CHECK_FALSE(san.contains("*.pal.alt.ee"));
			CHECK_FALSE(san.contains("subdomain1.subdomain2.pal.alt.ee"));
		}
	}

	SECTION("public_key") //{{{1
	{
		const auto &data = GENERATE(from_range(test_cert::data));
		auto c = certificate::from_pem(data.pem).value();
		auto key = c.public_key().value();
		CHECK(key.size_bits() == data.size_bits);
		CHECK(key.max_block_size() == data.max_block_size);
	}

	SECTION("public_key: not_enough_memory") //{{{1
	{
		auto c = certificate::from_pem(test_cert::server.pem).value();
		pal_test::bad_alloc_once x;
		auto key = c.public_key();
		REQUIRE_FALSE(key);
		CHECK(key.error() == std::errc::not_enough_memory);
	}

	//}}}1
}

} // namespace
