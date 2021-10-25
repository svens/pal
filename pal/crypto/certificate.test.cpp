#include <pal/crypto/certificate>
#include <pal/crypto/oid>
#include <pal/crypto/test>


namespace {


using namespace pal::crypto;
namespace test_cert = pal_test::cert;
using namespace std::chrono_literals;


inline certificate::time_type now () noexcept
{
	return certificate::clock_type::now();
}


inline certificate::time_type far_past () noexcept
{
	return (certificate::time_type::min)() + 24h;
}


inline certificate::time_type far_future () noexcept
{
	return (certificate::time_type::max)() - 24h;
}


TEST_CASE("crypto/certificate", "[!nonportable]")
{
	const certificate null{};
	std::error_code error;

	SECTION("from_pem")
	{
		auto client = certificate::from_pem(test_cert::client_pem);
		CHECK(client);
	}

	SECTION("from_pem: empty")
	{
		auto client = certificate::from_pem("");
		REQUIRE(!client);
		CHECK(client.error() == std::errc::invalid_argument);
	}

	SECTION("from_pem: first from multiple")
	{
		std::string pem{test_cert::client_pem};
		pem += test_cert::server_pem;
		auto client = certificate::from_pem(pem);
		CHECK(client);
	}

	SECTION("from_pem: oversized")
	{
		std::string pem{test_cert::client_pem};
		pem.insert(pem.size() / 2, (16 * 1024) / 3 * 4, '\n');
		auto client = certificate::from_pem(pem);
		CHECK(client);
	}

	SECTION("from_pem: alloc error")
	{
		std::string pem{test_cert::client_pem};
		pem.insert(pem.size() / 2, (16 * 1024) / 3 * 4, '\n');
		pal_test::bad_alloc_once x;
		auto client = certificate::from_pem(pem);
		REQUIRE(!client);
		CHECK(client.error() == std::errc::not_enough_memory);
	}

	SECTION("from_pem: invalid header")
	{
		auto pem = test_cert::client_pem;
		pem.remove_prefix(1);
		auto client = certificate::from_pem(pem);
		REQUIRE(!client);
		CHECK(client.error() == std::errc::invalid_argument);
	}

	SECTION("from_pem: invalid footer")
	{
		auto pem = test_cert::client_pem;
		pem.remove_suffix(1);
		auto client = certificate::from_pem(pem);
		REQUIRE(!client);
		CHECK(client.error() == std::errc::invalid_argument);
	}

	SECTION("from_pem: invalid base64 content")
	{
		std::string pem{test_cert::client_pem};
		pem[pem.size()/2] = '-';
		auto client = certificate::from_pem(pem);
		REQUIRE(!client);
		CHECK(client.error() == std::errc::invalid_argument);
	}

	SECTION("from_pem: invalid cert content")
	{
		std::string pem{test_cert::client_pem};
		pem.insert(pem.size() / 2, 8, 'X');
		auto client = certificate::from_pem(pem);
		REQUIRE(!client);
		CHECK(client.error() == std::errc::invalid_argument);
	}

	SECTION("from_der")
	{
		auto der = pal_test::to_der(test_cert::client_pem);
		auto client = certificate::from_der(der);
		CHECK(client);
	}

	SECTION("from_der: invalid content")
	{
		auto der = pal_test::to_der(test_cert::client_pem);
		der.insert(der.size() / 2, 8, 'X');
		auto client = certificate::from_der(der);
		REQUIRE(!client);
		CHECK(client.error() == std::errc::invalid_argument);
	}

	SECTION("compare")
	{
		CHECK(null == null);

		auto client = certificate::from_pem(test_cert::client_pem).value();
		CHECK(client != null);
		CHECK(null != client);
		CHECK(client == client);

		auto server = certificate::from_pem(test_cert::server_pem).value();
		CHECK(server != null);
		CHECK(null != server);
		CHECK(server == server);

		CHECK(server != client);
		CHECK(client != server);
	}

	SECTION("copy constructor")
	{
		auto c1 = certificate::from_pem(test_cert::client_pem).value();
		auto c2{c1};
		CHECK(c2 == c1);
		CHECK(c2);
	}

	SECTION("copy constructor: null")
	{
		auto c = null;
		CHECK(!c);
	}

	SECTION("copy assign")
	{
		auto c1 = certificate::from_pem(test_cert::client_pem).value();
		certificate c2;
		c2 = c1;
		CHECK(c2 == c1);
		CHECK(c2);
	}

	SECTION("move constructor")
	{
		auto c1 = certificate::from_pem(test_cert::client_pem).value();
		auto c2{std::move(c1)};
		CHECK(c2);
		CHECK(c2);
		CHECK(c1.is_null());
	}

	SECTION("move assign")
	{
		auto c1 = certificate::from_pem(test_cert::client_pem).value();
		certificate c2;
		c2 = std::move(c1);
		CHECK(c2 != c1);
		CHECK(c2);
		CHECK(c1.is_null());
	}

	SECTION("swap")
	{
		auto c1 = certificate::from_pem(test_cert::client_pem).value();
		certificate c2;
		CHECK(c2.is_null());

		c1.swap(c2);
		CHECK(c1.is_null());
		CHECK(c2);
	}

	SECTION("is_null")
	{
		CHECK(null.is_null());
		CHECK_FALSE(null);

		auto client = certificate::from_pem(test_cert::client_pem).value();
		CHECK_FALSE(client.is_null());
		CHECK(client);
	}

	SECTION("version")
	{
		auto [pem, expected_version] = GENERATE(
			table<std::string_view, int>(
			{
				{ test_cert::ca_pem, 3 },
				{ test_cert::intermediate_pem, 3 },
				{ test_cert::server_pem, 3 },
				{ test_cert::client_pem, 3 },
			})
		);
		auto cert = certificate::from_pem(pem).value();
		CHECK(cert.version() == expected_version);
	}

	SECTION("not_before / not_after")
	{
		auto cert = certificate::from_pem(GENERATE(
			test_cert::ca_pem,
			test_cert::intermediate_pem,
			test_cert::server_pem,
			test_cert::client_pem,
			test_cert::self_signed_pem
		)).value();

		CHECK(cert.not_before() != certificate::time_type{});
		CHECK(cert.not_before() > far_past());
		CHECK(cert.not_before() < now());

		CHECK(cert.not_after() != certificate::time_type{});
		CHECK(cert.not_after() > now());
		CHECK(cert.not_after() < far_future());
	}

	SECTION("not_expired_at")
	{
		auto cert = certificate::from_pem(GENERATE(
			test_cert::ca_pem,
			test_cert::intermediate_pem,
			test_cert::server_pem,
			test_cert::client_pem
		)).value();
		CHECK(cert.not_expired_at(now()));
		CHECK_FALSE(cert.not_expired_at(far_past()));
		CHECK_FALSE(cert.not_expired_at(far_future()));
	}

	SECTION("not_expired_for")
	{
		auto cert = certificate::from_pem(GENERATE(
			test_cert::ca_pem,
			test_cert::intermediate_pem,
			test_cert::server_pem,
			test_cert::client_pem
		)).value();
		CHECK(cert.not_expired_for(24h, now()));
		CHECK_FALSE(cert.not_expired_for(24h * 365 * 100, now()));
		CHECK_FALSE(cert.not_expired_for(24h, far_past()));
		CHECK_FALSE(cert.not_expired_for(24h, far_future()));
	}

	SECTION("digest: sha1")
	{
		auto [pem, expected] = GENERATE(table<std::string_view, std::string_view>({
			{
				test_cert::ca_pem,
				"f89bd9fc4050afa352104e7ec4e06c7f5237f88c"
			},
			{
				test_cert::intermediate_pem,
				"919bbbf95b543f58498c5300e65b857aa7ff83b8"
			},
			{
				test_cert::server_pem,
				"3bd31e7b696c888cd9b54aab6e31ce5b2636d271"
			},
			{
				test_cert::client_pem,
				"a1e42e5a8a5af09fa70cf57524f8214f7b027352"
			},
		}));
		auto cert = certificate::from_pem(pem).value();
		auto digest = pal_test::to_hex(cert.digest<pal::crypto::sha1>());
		CHECK(digest == expected);
	}

	SECTION("digest: sha512")
	{
		auto [pem, expected] = GENERATE(table<std::string_view, std::string_view>({
			{
				test_cert::ca_pem,
				"394fde2014b1e620c5218a01b0d2eae6d99b2e9aec52090e1536821d2d7ca2930b849cf75cd019e1ffa3d24f7c0dd0517321a100b37b59eb1a21cc79e0dac4de"
			},
			{
				test_cert::intermediate_pem,
				"4c2c8840edb8d8244ff7de4cd5daa9dcf35222788d08274485bbb7650d5c808cfdc9f5df70fda6e5b668575bd6b99543a219c8e19b31368155c7cf08d3f65b4d"
			},
			{
				test_cert::server_pem,
				"90606545603819c4c5f8d9bd69ad700f3dfcedf7b12702d96331aa84b3d58dee606ad8155324376d614131a8a82fedb041c66c658fb696bcd08395ed47ae357b"
			},
			{
				test_cert::client_pem,
				"9e3bfe8d98a99299f4a604e97ad5df2ccbbc04b1e600a1899f2dd1e96840edc346f958b95c532f45b244ecce5a346304438e7f34e20a0419743a0e91accac252"
			},
		}));
		auto cert = certificate::from_pem(pem).value();
		auto digest = pal_test::to_hex(cert.digest<pal::crypto::sha512>());
		CHECK(digest == expected);
	}

	SECTION("serial_number")
	{
		auto [pem, expected] = GENERATE(table<std::string_view, std::string_view>({
			{ test_cert::ca_pem, "ffc401131091ec63" },
			{ test_cert::intermediate_pem, "1000" },
			{ test_cert::server_pem, "1001" },
			{ test_cert::client_pem, "1002" },
		}));
		auto cert = certificate::from_pem(pem).value();

		uint8_t buf[64];
		auto serial_number = pal_test::to_hex(cert.serial_number(buf).value());
		CHECK(serial_number == expected);
	}

	SECTION("serial_number: buffer too small")
	{
		auto cert = certificate::from_pem(test_cert::ca_pem).value();
		uint8_t buf[1];
		auto span = cert.serial_number(buf);
		REQUIRE(!span);
		CHECK(span.error() == std::errc::result_out_of_range);
	}

	SECTION("serial_number: alloc failure")
	{
		if constexpr (pal::is_linux_build)
		{
			auto cert = certificate::from_pem(test_cert::ca_pem).value();
			uint8_t buf[64];
			pal_test::bad_alloc_once x;
			auto span = cert.serial_number(buf);
			REQUIRE(!span);
			CHECK(span.error() == std::errc::not_enough_memory);
		}
	}

	SECTION("authority_key_identifier")
	{
		auto [pem, expected] = GENERATE(table<std::string_view, std::string_view>({
			{ test_cert::ca_pem, "8103f8b5aea1e5228d16d63073ebb52f65d9b87f" },
			{ test_cert::intermediate_pem, "8103f8b5aea1e5228d16d63073ebb52f65d9b87f" },
			{ test_cert::server_pem, "12a96089b19bcb9b97fa2d173532158664930668" },
			{ test_cert::client_pem, "12a96089b19bcb9b97fa2d173532158664930668" },
		}));
		auto cert = certificate::from_pem(pem).value();

		uint8_t buf[64];
		auto aki = pal_test::to_hex(cert.authority_key_identifier(buf).value());
		CHECK(aki == expected);
	}

	SECTION("authority_key_identifier: no extension")
	{
		auto cert = certificate::from_pem(test_cert::self_signed_pem).value();
		uint8_t buf[64];
		auto aki = cert.authority_key_identifier(buf);
		REQUIRE(!aki);
		CHECK(aki.error() == pal::crypto::errc::certificate_extension_not_found);
	}

	SECTION("authority_key_identifier: buffer too small")
	{
		auto cert = certificate::from_pem(test_cert::ca_pem).value();
		uint8_t buf[1];
		auto aki = cert.authority_key_identifier(buf);
		REQUIRE(!aki);
		CHECK(aki.error() == std::errc::result_out_of_range);
	}

	SECTION("authority_key_identifier: allocation error")
	{
		if constexpr (pal::is_linux_build)
		{
			auto cert = certificate::from_pem(test_cert::ca_pem).value();
			uint8_t buf[64];
			pal_test::bad_alloc_once x;
			auto aki = cert.authority_key_identifier(buf);
			REQUIRE(!aki);
			CHECK(aki.error() == pal::crypto::errc::certificate_extension_decoding_error);
		}
	}

	SECTION("subject_key_identifier")
	{
		auto [pem, expected] = GENERATE(table<std::string_view, std::string_view>({
			{ test_cert::ca_pem, "8103f8b5aea1e5228d16d63073ebb52f65d9b87f" },
			{ test_cert::intermediate_pem, "12a96089b19bcb9b97fa2d173532158664930668" },
			{ test_cert::server_pem, "1d559063dee9d2b3f8ba1f358d2d07176e91aabc" },
			{ test_cert::client_pem, "214ee57389b3c2300ffffac93e64291c782f328b" },
		}));
		auto cert = certificate::from_pem(pem).value();

		uint8_t buf[64];
		auto ski = pal_test::to_hex(cert.subject_key_identifier(buf).value());
		CHECK(ski == expected);
	}

	SECTION("subject_key_identifier: no extension")
	{
		auto cert = certificate::from_pem(test_cert::self_signed_pem).value();
		uint8_t buf[64];
		auto ski = cert.subject_key_identifier(buf);
		REQUIRE(!ski);
		CHECK(ski.error() == pal::crypto::errc::certificate_extension_not_found);
	}

	SECTION("subject_key_identifier: buffer too small")
	{
		auto cert = certificate::from_pem(test_cert::ca_pem).value();
		uint8_t buf[1];
		auto ski = cert.subject_key_identifier(buf);
		REQUIRE(!ski);
		CHECK(ski.error() == std::errc::result_out_of_range);
	}

	SECTION("subject_key_identifier: allocation error")
	{
		if constexpr (pal::is_linux_build)
		{
			auto cert = certificate::from_pem(test_cert::ca_pem).value();
			uint8_t buf[64];
			pal_test::bad_alloc_once x;
			auto ski = cert.subject_key_identifier(buf);
			REQUIRE(!ski);
			CHECK(ski.error() == pal::crypto::errc::certificate_extension_decoding_error);
		}
	}

	SECTION("issued_by / is_self_signed")
	{
		auto ca = certificate::from_pem(test_cert::ca_pem).value();
		auto intermediate = certificate::from_pem(test_cert::intermediate_pem).value();
		auto server = certificate::from_pem(test_cert::server_pem).value();
		auto client = certificate::from_pem(test_cert::client_pem).value();

		CHECK(ca.is_self_signed());
		CHECK(ca.issued_by(ca));
		CHECK_FALSE(ca.issued_by(intermediate));
		CHECK_FALSE(ca.issued_by(server));
		CHECK_FALSE(ca.issued_by(client));

		CHECK_FALSE(intermediate.is_self_signed());
		CHECK(intermediate.issued_by(ca));
		CHECK_FALSE(intermediate.issued_by(intermediate));
		CHECK_FALSE(intermediate.issued_by(server));
		CHECK_FALSE(intermediate.issued_by(client));

		CHECK_FALSE(server.is_self_signed());
		CHECK_FALSE(server.issued_by(ca));
		CHECK(server.issued_by(intermediate));
		CHECK_FALSE(server.issued_by(server));
		CHECK_FALSE(server.issued_by(client));

		CHECK_FALSE(client.is_self_signed());
		CHECK_FALSE(client.issued_by(ca));
		CHECK(client.issued_by(intermediate));
		CHECK_FALSE(client.issued_by(server));
		CHECK_FALSE(client.issued_by(client));
	}

	SECTION("issuer")
	{
		auto [pem, expected] = GENERATE(table<std::string_view, certificate::name_type>({
			{
				test_cert::ca_pem,
				{
					{ "2.5.4.6", "EE" },
					{ "2.5.4.8", "Estonia" },
					{ "2.5.4.10", "PAL" },
					{ "2.5.4.11", "PAL CA" },
					{ "2.5.4.3", "PAL Root CA" },
				}
			},
			{
				test_cert::intermediate_pem,
				{
					{ "2.5.4.6", "EE" },
					{ "2.5.4.8", "Estonia" },
					{ "2.5.4.10", "PAL" },
					{ "2.5.4.11", "PAL CA" },
					{ "2.5.4.3", "PAL Root CA" },
				}
			},
			{
				test_cert::server_pem,
				{
					{ "2.5.4.6", "EE" },
					{ "2.5.4.8", "Estonia" },
					{ "2.5.4.10", "PAL" },
					{ "2.5.4.11", "PAL CA" },
					{ "2.5.4.3", "PAL Intermediate CA" },
				}
			},
			{
				test_cert::client_pem,
				{
					{ "2.5.4.6", "EE" },
					{ "2.5.4.8", "Estonia" },
					{ "2.5.4.10", "PAL" },
					{ "2.5.4.11", "PAL CA" },
					{ "2.5.4.3", "PAL Intermediate CA" },
				}
			},
		}));
		auto cert = certificate::from_pem(pem).value();
		CHECK(cert.issuer() == expected);

		CHECK(cert.issuer("invalid_oid").empty());

		CHECK(cert.issuer(oid::country_name)[0] == expected[0]);
		CHECK(cert.issuer(oid::state_or_province_name)[0] == expected[1]);
		CHECK(cert.issuer(oid::organization_name)[0] == expected[2]);
		CHECK(cert.issuer(oid::organizational_unit_name)[0] == expected[3]);
		CHECK(cert.issuer(oid::common_name)[0] == expected[4]);
	}

	SECTION("subject")
	{
		auto [pem, expected] = GENERATE(table<std::string_view, certificate::name_type>({
			{
				test_cert::ca_pem,
				{
					{ "2.5.4.6", "EE" },
					{ "2.5.4.8", "Estonia" },
					{ "2.5.4.10", "PAL" },
					{ "2.5.4.11", "PAL CA" },
					{ "2.5.4.3", "PAL Root CA" },
				}
			},
			{
				test_cert::intermediate_pem,
				{
					{ "2.5.4.6", "EE" },
					{ "2.5.4.8", "Estonia" },
					{ "2.5.4.10", "PAL" },
					{ "2.5.4.11", "PAL CA" },
					{ "2.5.4.3", "PAL Intermediate CA" },
				}
			},
			{
				test_cert::server_pem,
				{
					{ "2.5.4.6", "EE" },
					{ "2.5.4.8", "Estonia" },
					{ "2.5.4.10", "PAL" },
					{ "2.5.4.11", "PAL Test" },
					{ "2.5.4.3", "pal.alt.ee" },
				}
			},
			{
				test_cert::client_pem,
				{
					{ "2.5.4.6", "EE" },
					{ "2.5.4.8", "Estonia" },
					{ "2.5.4.10", "PAL" },
					{ "2.5.4.11", "PAL Test" },
					{ "2.5.4.3", "pal.alt.ee" },
					{ "1.2.840.113549.1.9.1", "pal@alt.ee" },
				}
			},
		}));
		auto cert = certificate::from_pem(pem).value();
		CHECK(cert.subject() == expected);

		CHECK(cert.subject("invalid_oid").empty());

		CHECK(cert.subject(oid::country_name)[0] == expected[0]);
		CHECK(cert.subject(oid::state_or_province_name)[0] == expected[1]);
		CHECK(cert.subject(oid::organization_name)[0] == expected[2]);
		CHECK(cert.subject(oid::organizational_unit_name)[0] == expected[3]);
		CHECK(cert.subject(oid::common_name)[0] == expected[4]);
	}

	SECTION("issuer_alt_name")
	{
		auto [pem, expected] = GENERATE(table<std::string_view, certificate::alt_name_type>({
			{
				test_cert::ca_pem, { }
			},
			{
				test_cert::intermediate_pem, { }
			},
			{
				test_cert::server_pem,
				{
					{ alt_name::dns, "ca.pal.alt.ee" },
					{ alt_name::uri, "https://ca.pal.alt.ee/path" },
				}
			},
			{
				test_cert::client_pem, { }
			},
		}));
		auto cert = certificate::from_pem(pem).value();
		CHECK(cert.issuer_alt_name() == expected);
	}

	SECTION("subject_alt_name")
	{
		auto [pem, expected] = GENERATE(table<std::string_view, certificate::alt_name_type>({
			{
				test_cert::ca_pem, { }
			},
			{
				test_cert::intermediate_pem, { }
			},
			{
				test_cert::server_pem,
				{
					{ alt_name::ip, "1.2.3.4" },
					{ alt_name::ip, "2001:db8:85a3::8a2e:370:7334" },
					{ alt_name::dns, "*.pal.alt.ee" },
					{ alt_name::dns, "server.pal.alt.ee" },
					{ alt_name::email, "pal@alt.ee" },
					{ alt_name::uri, "https://pal.alt.ee/path" },
				}
			},
			{
				test_cert::client_pem,
				{
					{ alt_name::email, "pal@alt.ee" },
					{ alt_name::dns, "client.pal.alt.ee" },
				}
			},
		}));
		auto cert = certificate::from_pem(pem).value();
		CHECK(cert.subject_alt_name() == expected);
	}

	SECTION("import_pkcs12")
	{
		auto pkcs12 = pal_test::to_der(test_cert::pkcs12);
		auto chain = certificate::import_pkcs12(std::span{pkcs12}, "TestPassword");
		REQUIRE(chain.size() == 4);

		// server
		CHECK(chain[0].issued_by(chain[2]));
		auto [oid, value] = chain[0].subject(oid::common_name)[0];
		CHECK(value == "pal.alt.ee");

		// client
		CHECK(chain[1].issued_by(chain[2]));
		std::tie(oid, value) = chain[1].subject(oid::common_name)[0];
		CHECK(value == "pal.alt.ee");

		// intermediate
		CHECK(chain[2].issued_by(chain[3]));
		std::tie(oid, value) = chain[2].subject(oid::common_name)[0];
		CHECK(value == "PAL Intermediate CA");

		// root
		CHECK(chain[3].is_self_signed());
		std::tie(oid, value) = chain[3].subject(oid::common_name)[0];
		CHECK(value == "PAL Root CA");
	}

	SECTION("import_pkcs12: private_key")
	{
		pal::crypto::private_key key;
		CHECK(key.is_null());

		auto pkcs12 = pal_test::to_der(test_cert::pkcs12);
		certificate::import_pkcs12(std::span{pkcs12}, "TestPassword", &key);
		REQUIRE_FALSE(key.is_null());
		CHECK(key.size_bits() == 2048);
		CHECK(key.algorithm() == pal::crypto::key_algorithm::rsa);
	}

	SECTION("import_pkcs12: empty password")
	{
		auto pkcs12 = pal_test::to_der(test_cert::pkcs12);
		auto chain = certificate::import_pkcs12(std::span{pkcs12}, "");
		CHECK(chain.empty());
	}

	SECTION("import_pkcs12: no password")
	{
		if constexpr (!pal::is_macos_build)
		{
			auto pkcs12 = pal_test::to_der(test_cert::pkcs12_no_passphrase);
			auto chain = certificate::import_pkcs12(std::span{pkcs12}, "");
			CHECK(chain.size() == 4);
		}
		// else MacOS refuses to load password-less PKCS12
	}

	SECTION("import_pkcs12: invalid password")
	{
		auto pkcs12 = pal_test::to_der(test_cert::pkcs12);
		auto chain = certificate::import_pkcs12(std::span{pkcs12}, pal_test::case_name());
		CHECK(chain.empty());
	}

	SECTION("import_pkcs12: invalid PKCS12")
	{
		auto buf = pal_test::case_name();
		auto chain = certificate::import_pkcs12(std::span{buf}, "TestPassword");
		CHECK(chain.empty());
	}
}


TEST_CASE("crypto/certificate/store", "[!mayfail]")
{
	SECTION("load_one(with_common_name)")
	{
		auto cert = certificate::load_one(with_common_name("pal.alt.ee"));
		REQUIRE(cert);

		auto [_, value] = cert->subject(oid::common_name)[0];
		CHECK(value == "pal.alt.ee");
	}

	SECTION("load_one(with_common_name) not found")
	{
		auto cert = certificate::load_one(with_common_name(pal_test::case_name()));
		REQUIRE(!cert);
		CHECK(cert.error() == std::errc::no_such_file_or_directory);
	}

	SECTION("load_all(with_common_name)")
	{
		auto certs = certificate::load_all(with_common_name("pal.alt.ee"));
		REQUIRE(certs.size() == 2);

		for (auto &cert: certs)
		{
			auto [_, value] = cert.subject(oid::common_name)[0];
			CHECK(value == "pal.alt.ee");
		}
	}

	SECTION("load_all(with_common_name) not found")
	{
		auto certs = certificate::load_all(with_common_name(pal_test::case_name()));
		CHECK(certs.empty());
	}

	SECTION("load_all(with_fqdn)")
	{
		auto certs = certificate::load_all(with_fqdn("client.pal.alt.ee"));
		REQUIRE(certs.size() == 2);

		bool tested_client_cert = false, tested_server_cert = false;
		for (auto &cert: certs)
		{
			uint8_t buf[16];
			auto serial_number = pal_test::to_hex(cert.serial_number(buf).value());

			if (serial_number == "1001")
			{
				// server with wildcard match
				tested_server_cert = true;
				auto names = cert.subject_alt_name();
				REQUIRE(names.size() == 6);
				auto [type, value] = names[3];
				CHECK(type == alt_name::dns);
				CHECK(value == "server.pal.alt.ee");
			}
			else if (serial_number == "1002")
			{
				// client with exact match
				tested_client_cert = true;
				auto names = cert.subject_alt_name();
				REQUIRE(names.size() == 2);
				auto [type, value] = names[1];
				CHECK(type == alt_name::dns);
				CHECK(value == "client.pal.alt.ee");
			}
		}
		CHECK(tested_client_cert);
		CHECK(tested_server_cert);
	}

	SECTION("load_all(with_fqdn) exact not found")
	{
		auto certs = certificate::load_all(with_fqdn(pal_test::case_name()));
		CHECK(certs.empty());
	}

	SECTION("load_all(with_fqdn) wildcard")
	{
		auto certs = certificate::load_all(with_fqdn("ok.pal.alt.ee"));
		REQUIRE(certs.size() == 1);

		auto names = certs[0].subject_alt_name();
		REQUIRE(names.size() == 6);
		auto &[type, value] = names[3];
		CHECK(type == alt_name::dns);
		CHECK(value == "server.pal.alt.ee");
	}

	SECTION("load_all(with_fqdn) wildcard matches too much")
	{
		auto certs = certificate::load_all(with_fqdn("not.ok.pal.alt.ee"));
		CHECK(certs.empty());
	}

	SECTION("load_all(with_fqdn) wildcard not found")
	{
		auto certs = certificate::load_all(with_fqdn("fail-pal.alt.ee"));
		CHECK(certs.empty());
	}

	SECTION("load_all(with_thumbprint<sha1>)")
	{
		std::array<uint8_t, 20> thumbprint =
		{
			0xa1, 0xe4, 0x2e, 0x5a, 0x8a, 0x5a, 0xf0, 0x9f, 0xa7, 0x0c,
			0xf5, 0x75, 0x24, 0xf8, 0x21, 0x4f, 0x7b, 0x02, 0x73, 0x52,
		};
		const auto &thumbprint_bytes = reinterpret_cast<typename basic_hash<sha1>::result_type &>(thumbprint);

		auto certs = certificate::load_all(with_thumbprint<sha1>(thumbprint_bytes));
		REQUIRE(certs.size() == 1);

		auto [_, value] = certs[0].subject("1.2.840.113549.1.9.1")[0];
		CHECK(value == "pal@alt.ee");
	}

	SECTION("load_all(with_thumbprint<sha1>) not found")
	{
		std::array<std::byte, 20> thumbprint = { };
		auto certs = certificate::load_all(with_thumbprint<sha1>(thumbprint));
		REQUIRE(certs.size() == 0);
	}

	SECTION("load_all(with_thumbprint<sha256>)")
	{
		std::array<uint8_t, 32> thumbprint =
		{
			0x25, 0x8c, 0xbd, 0x3a, 0x9e, 0x09, 0xf2, 0x96,
			0x77, 0x06, 0xd0, 0x70, 0x15, 0x3b, 0x5e, 0x8e,
			0xf1, 0x13, 0xaf, 0xa6, 0x05, 0x01, 0xc2, 0x24,
			0x77, 0x8e, 0xec, 0xe6, 0x51, 0x31, 0xac, 0x44,
		};
		const auto &thumbprint_bytes = reinterpret_cast<typename basic_hash<sha256>::result_type &>(thumbprint);

		auto certs = certificate::load_all(with_thumbprint<sha256>(thumbprint_bytes));
		REQUIRE(certs.size() == 1);

		auto [_, value] = certs[0].subject("1.2.840.113549.1.9.1")[0];
		CHECK(value == "pal@alt.ee");
	}

	SECTION("load_all(with_thumbprint<sha256>) not found")
	{
		std::array<std::byte, 32> thumbprint = { };
		auto certs = certificate::load_all(with_thumbprint<sha256>(thumbprint));
		REQUIRE(certs.size() == 0);
	}
}


} // namespace
