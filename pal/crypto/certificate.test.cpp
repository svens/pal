#include <pal/crypto/certificate>
#include <pal/crypto/oid>
#include <pal/crypto/test>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
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
		struct cert_info
		{
			std::string_view pem;
			int version;
			std::string_view serial_number;
			std::string_view common_name;
			std::string_view fingerprint;
		};

		const auto &[pem, version, serial_number, common_name, fingerprint] = GENERATE(values<cert_info>(
		{
			{
				.pem = test_cert::ca_pem,
				.version = 3,
				.serial_number = "ffc401131091ec63",
				.common_name = "PAL Root CA",
				.fingerprint = "f89bd9fc4050afa352104e7ec4e06c7f5237f88c"
			},
			{
				.pem = test_cert::intermediate_pem,
				.version = 3,
				.serial_number = "1000",
				.common_name = "PAL Intermediate CA",
				.fingerprint = "919bbbf95b543f58498c5300e65b857aa7ff83b8"
			},
			{
				.pem = test_cert::server_pem,
				.version = 3,
				.serial_number = "1001",
				.common_name = "pal.alt.ee",
				.fingerprint = "3bd31e7b696c888cd9b54aab6e31ce5b2636d271"
			},
			{
				.pem = test_cert::client_pem,
				.version = 3,
				.serial_number = "1002",
				.common_name = "pal.alt.ee",
				.fingerprint = "a1e42e5a8a5af09fa70cf57524f8214f7b027352"
			},
			{
				.pem = test_cert::self_signed_pem,
				.version = 3,
				.serial_number = "5feebd7481e6029946cfc05cd0b6c645",
				.common_name = "Test",
				.fingerprint = "f7c05805853defda0f8e3376c4e70d4f09f5060e"
			},
		}));
		CAPTURE(fingerprint);

		auto c = certificate::from_pem(pem).value();
		CHECK(c.version() == version);
		CHECK(pal_test::to_hex(c.serial_number()) == serial_number);
		CHECK(c.common_name() == common_name);
		CHECK(c.fingerprint() == fingerprint);

		auto d = certificate::from_der(c.as_bytes()).value();
		CHECK(d.version() == version);
		CHECK(pal_test::to_hex(d.serial_number()) == serial_number);
		CHECK(d.common_name() == common_name);
		CHECK(d.fingerprint() == fingerprint);
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
		std::string pem{test_cert::self_signed_pem};

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
		std::string pem{test_cert::self_signed_pem};
		pem.insert(pem.size() / 2, (16 * 1024) / 3 * 4, '\n');
		pal_test::bad_alloc_once x;
		auto c = certificate::from_pem(pem);
		REQUIRE_FALSE(c);
		CHECK(c.error() == std::errc::not_enough_memory);
	}

	SECTION("from_der: invalid_argument") //{{{1
	{
		std::string der = pal_test::to_der(test_cert::self_signed_pem);;

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
		auto der = pal_test::to_der(test_cert::self_signed_pem);
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
			test_cert::client_pem,
			test_cert::self_signed_pem
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

	SECTION("is_issued_by / is_self_signed") //{{{1
	{
		auto ca = certificate::from_pem(test_cert::ca_pem).value();
		auto intermediate = certificate::from_pem(test_cert::intermediate_pem).value();
		auto server = certificate::from_pem(test_cert::server_pem).value();
		auto client = certificate::from_pem(test_cert::client_pem).value();

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
		const auto &[pem, expected] = GENERATE(table<std::string_view, std::string>(
		{
			{
				test_cert::ca_pem,
				"C=EE\n"
				"ST=Estonia\n"
				"O=PAL\n"
				"OU=PAL CA\n"
				"CN=PAL Root CA\n"
			},
			{
				test_cert::intermediate_pem,
				"C=EE\n"
				"ST=Estonia\n"
				"O=PAL\n"
				"OU=PAL CA\n"
				"CN=PAL Intermediate CA\n"
			},
			{
				test_cert::server_pem,
				"C=EE\n"
				"ST=Estonia\n"
				"O=PAL\n"
				"OU=PAL Test\n"
				"CN=pal.alt.ee\n"
			},
			{
				test_cert::client_pem,
				"C=EE\n"
				"ST=Estonia\n"
				"O=PAL\n"
				"OU=PAL Test\n"
				"CN=pal.alt.ee\n"
				"1.2.840.113549.1.9.1=pal@alt.ee\n"
			},
			{
				test_cert::self_signed_pem,
				"CN=Test\n"
			},
		}));
		auto c = certificate::from_pem(pem).value();
		auto name = to_string(c.subject_name().value());
		CHECK(name == expected);
	}

	SECTION("subject_name: not_enough_memory") //{{{1
	{
		auto c = certificate::from_pem(test_cert::self_signed_pem).value();
		pal_test::bad_alloc_once x;
		auto name = c.subject_name();
		REQUIRE_FALSE(name);
		CHECK(name.error() == std::errc::not_enough_memory);
	}

	SECTION("issuer_name") //{{{1
	{
		const auto &[pem, expected] = GENERATE(table<std::string_view, std::string>(
		{
			{
				test_cert::ca_pem,
				"C=EE\n"
				"ST=Estonia\n"
				"O=PAL\n"
				"OU=PAL CA\n"
				"CN=PAL Root CA\n"
			},
			{
				test_cert::intermediate_pem,
				"C=EE\n"
				"ST=Estonia\n"
				"O=PAL\n"
				"OU=PAL CA\n"
				"CN=PAL Root CA\n"
			},
			{
				test_cert::server_pem,
				"C=EE\n"
				"ST=Estonia\n"
				"O=PAL\n"
				"OU=PAL CA\n"
				"CN=PAL Intermediate CA\n"
			},
			{
				test_cert::client_pem,
				"C=EE\n"
				"ST=Estonia\n"
				"O=PAL\n"
				"OU=PAL CA\n"
				"CN=PAL Intermediate CA\n"
			},
			{
				test_cert::self_signed_pem,
				"CN=Test\n"
			},
		}));
		auto issuer_name = to_string(certificate::from_pem(pem).value().issuer_name().value());
		CHECK(issuer_name == expected);
	}

	SECTION("issuer_name: not_enough_memory") //{{{1
	{
		auto c = certificate::from_pem(test_cert::self_signed_pem).value();
		pal_test::bad_alloc_once x;
		auto issuer_name = c.issuer_name();
		REQUIRE_FALSE(issuer_name);
		CHECK(issuer_name.error() == std::errc::not_enough_memory);
	}

	SECTION("issuer_alternative_name") //{{{1
	{
		const auto &[pem, expected] = GENERATE(table<std::string_view, std::string>(
		{
			{
				test_cert::ca_pem,
				""
			},
			{
				test_cert::intermediate_pem,
				""
			},
			{
				test_cert::server_pem,
				"dns=ca.pal.alt.ee\n"
				"uri=https://ca.pal.alt.ee/path\n"
			},
			{
				test_cert::client_pem,
				""
			},
			{
				test_cert::self_signed_pem,
				""
			},
		}));
		auto c = certificate::from_pem(pem).value();
		auto name = to_string(c.issuer_alternative_name().value());
		CHECK(name == expected);
	}

	SECTION("issuer_alternative_name: not_enough_memory") //{{{1
	{
		auto c = certificate::from_pem(test_cert::server_pem).value();
		pal_test::bad_alloc_once x;
		auto name = c.issuer_alternative_name();
		REQUIRE_FALSE(name);
		CHECK(name.error() == std::errc::not_enough_memory);
	}

	SECTION("subject_alternative_name") //{{{1
	{
		const auto &[pem, expected] = GENERATE(table<std::string_view, std::string>(
		{
			{
				test_cert::ca_pem,
				""
			},
			{
				test_cert::intermediate_pem,
				""
			},
			{
				test_cert::server_pem,
				"ip=1.2.3.4\n"
				"ip=2001:db8:85a3::8a2e:370:7334\n"
				"dns=*.pal.alt.ee\n"
				"dns=server.pal.alt.ee\n"
				"email=pal@alt.ee\n"
				"uri=https://pal.alt.ee/path\n"
			},
			{
				test_cert::client_pem,
				"email=pal@alt.ee\n"
				"dns=client.pal.alt.ee\n"
			},
			{
				test_cert::self_signed_pem,
				""
			},
		}));
		auto c = certificate::from_pem(pem).value();
		auto name = to_string(c.subject_alternative_name().value());
		CHECK(name == expected);
	}

	SECTION("subject_alternative_name: not_enough_memory") //{{{1
	{
		auto c = certificate::from_pem(test_cert::server_pem).value();
		pal_test::bad_alloc_once x;
		auto name = c.subject_alternative_name();
		REQUIRE_FALSE(name);
		CHECK(name.error() == std::errc::not_enough_memory);
	}

	SECTION("subject_alternative_name: has_fqdn_match") //{{{1
	{
		{
			auto c = certificate::from_pem(test_cert::server_pem).value();
			auto name = c.subject_alternative_name().value();

			CHECK(name.has_fqdn_match("server.pal.alt.ee"));
			CHECK(name.has_fqdn_match("client.pal.alt.ee"));

			CHECK_FALSE(name.has_fqdn_match("ee"));
			CHECK_FALSE(name.has_fqdn_match(".ee"));
			CHECK_FALSE(name.has_fqdn_match("alt.ee"));
			CHECK_FALSE(name.has_fqdn_match(".alt.ee"));
			CHECK_FALSE(name.has_fqdn_match("pal.alt.ee"));
			CHECK_FALSE(name.has_fqdn_match(".pal.alt.ee"));
			CHECK_FALSE(name.has_fqdn_match("*.pal.alt.ee"));
			CHECK_FALSE(name.has_fqdn_match("subdomain1.subdomain2.pal.alt.ee"));
		}

		{
			auto c = certificate::from_pem(test_cert::client_pem).value();
			auto name = c.subject_alternative_name().value();

			CHECK(name.has_fqdn_match("client.pal.alt.ee"));

			CHECK_FALSE(name.has_fqdn_match("server.pal.alt.ee"));
			CHECK_FALSE(name.has_fqdn_match("ee"));
			CHECK_FALSE(name.has_fqdn_match(".ee"));
			CHECK_FALSE(name.has_fqdn_match("alt.ee"));
			CHECK_FALSE(name.has_fqdn_match(".alt.ee"));
			CHECK_FALSE(name.has_fqdn_match("pal.alt.ee"));
			CHECK_FALSE(name.has_fqdn_match(".pal.alt.ee"));
			CHECK_FALSE(name.has_fqdn_match("*.pal.alt.ee"));
			CHECK_FALSE(name.has_fqdn_match("subdomain1.subdomain2.pal.alt.ee"));
		}
	}

	SECTION("public_key") //{{{1
	{
		const auto &[pem, size_bits, max_block_size] = GENERATE(table<std::string_view, size_t, size_t>(
		{
			{ test_cert::ca_pem,		4096,	512 },
			{ test_cert::intermediate_pem,	4096,	512 },
			{ test_cert::server_pem,	2048,	256 },
			{ test_cert::client_pem,	2048,	256 },
		}));
		auto c = certificate::from_pem(pem).value();
		auto key = c.public_key().value();
		CHECK(key.size_bits() == size_bits);
		CHECK(key.max_block_size() == max_block_size);
	}

	SECTION("public_key: not_enough_memory") //{{{1
	{
		auto c = certificate::from_pem(test_cert::server_pem).value();
		pal_test::bad_alloc_once x;
		auto key = c.public_key();
		REQUIRE_FALSE(key);
		CHECK(key.error() == std::errc::not_enough_memory);
	}

	//}}}1
}

} // namespace
