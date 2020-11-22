#include <pal/crypto/certificate>
#include <pal/crypto/test>


namespace {


using pal::crypto::certificate;
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


TEST_CASE("crypto/certificate")
{
	const certificate null{};
	std::error_code error;

	SECTION("from_pem")
	{
		auto client = certificate::from_pem(test_cert::client_pem, error);
		REQUIRE(!error);
		CHECK(client);
	}

	SECTION("from_pem: empty")
	{
		auto client = certificate::from_pem("", error);
		CHECK(error == std::errc::invalid_argument);
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
		auto client = certificate::from_pem(pem, error);
		REQUIRE(!error);
		CHECK(client);
	}

	SECTION("from_pem: alloc error")
	{
		std::string pem{test_cert::client_pem};
		pem.insert(pem.size() / 2, (16 * 1024) / 3 * 4, '\n');
		pal_test::bad_alloc_once x;
		auto client = certificate::from_pem(pem, error);
		CHECK(error == std::errc::not_enough_memory);
	}

	SECTION("from_pem: invalid header")
	{
		auto pem = test_cert::client_pem;
		pem.remove_prefix(1);
		auto client = certificate::from_pem(pem, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			certificate::from_pem(pem),
			std::system_error
		);
	}

	SECTION("from_pem: invalid footer")
	{
		auto pem = test_cert::client_pem;
		pem.remove_suffix(1);
		auto client = certificate::from_pem(pem, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			certificate::from_pem(pem),
			std::system_error
		);
	}

	SECTION("from_pem: invalid base64 content")
	{
		std::string pem{test_cert::client_pem};
		pem[pem.size()/2] = '-';
		auto client = certificate::from_pem(pem, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			certificate::from_pem(pem),
			std::system_error
		);
	}

	SECTION("from_pem: invalid cert content")
	{
		std::string pem{test_cert::client_pem};
		pem.insert(pem.size() / 2, 8, 'X');
		auto client = certificate::from_pem(pem, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			certificate::from_pem(pem),
			std::system_error
		);
	}

	SECTION("from_der")
	{
		auto der = pal_test::to_der(test_cert::client_pem);
		auto client = certificate::from_der(std::span{der}, error);
		REQUIRE(!error);
		CHECK(client);

		CHECK_NOTHROW(certificate::from_der(std::span{der}));
	}

	SECTION("from_der: invalid content")
	{
		auto der = pal_test::to_der(test_cert::client_pem);
		der.insert(der.size() / 2, 8, 'X');
		auto client = certificate::from_der(std::span{der}, error);
		CHECK(error == std::errc::invalid_argument);

		CHECK_THROWS_AS(
			certificate::from_der(std::span{der}),
			std::system_error
		);
	}

	SECTION("compare")
	{
		CHECK(null == null);

		auto client = certificate::from_pem(test_cert::client_pem);
		CHECK(client != null);
		CHECK(null != client);
		CHECK(client == client);

		auto server = certificate::from_pem(test_cert::server_pem);
		CHECK(server != null);
		CHECK(null != server);
		CHECK(server == server);

		CHECK(server != client);
		CHECK(client != server);
	}

	SECTION("copy constructor")
	{
		auto c1 = certificate::from_pem(test_cert::client_pem);
		auto c2{c1};
		CHECK(c2 == c1);
		CHECK(c2);
	}

	SECTION("copy assign")
	{
		auto c1 = certificate::from_pem(test_cert::client_pem);
		certificate c2;
		c2 = c1;
		CHECK(c2 == c1);
		CHECK(c2);
	}

	SECTION("move constructor")
	{
		auto c1 = certificate::from_pem(test_cert::client_pem);
		CHECK(c1);

		auto c2{std::move(c1)};
		CHECK(c2);
		CHECK(c2);
		CHECK(c1.is_null());
	}

	SECTION("move assign")
	{
		auto c1 = certificate::from_pem(test_cert::client_pem);
		CHECK(c1);

		certificate c2;
		c2 = std::move(c1);
		CHECK(c2 != c1);
		CHECK(c2);
		CHECK(c1.is_null());
	}

	SECTION("swap")
	{
		auto c1 = certificate::from_pem(test_cert::client_pem);
		CHECK(c1);

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

		auto client = certificate::from_pem(test_cert::client_pem);
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
				{ "", 0 },
			})
		);
		auto cert = pem.size() ? certificate::from_pem(pem) : null;
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
		));

		CHECK(cert.not_before() != certificate::time_type{});
		CHECK(cert.not_before() > far_past());
		CHECK(cert.not_before() < now());

		CHECK(cert.not_after() != certificate::time_type{});
		CHECK(cert.not_after() > now());
		CHECK(cert.not_after() < far_future());
	}

	SECTION("not_before / not_after: null")
	{
		CHECK(null.not_before() == certificate::time_type{});
		CHECK(null.not_after() == certificate::time_type{});
	}

	SECTION("not_expired_at")
	{
		auto cert = certificate::from_pem(GENERATE(
			test_cert::ca_pem,
			test_cert::intermediate_pem,
			test_cert::server_pem,
			test_cert::client_pem
		));
		CHECK(cert.not_expired_at(now()));
		CHECK_FALSE(cert.not_expired_at(far_past()));
		CHECK_FALSE(cert.not_expired_at(far_future()));
	}

	SECTION("not_expired_at: null")
	{
		CHECK_FALSE(null.not_expired_at(now()));
		CHECK_FALSE(null.not_expired_at(far_past()));
		CHECK_FALSE(null.not_expired_at(far_future()));
	}

	SECTION("not_expired_for")
	{
		auto cert = certificate::from_pem(GENERATE(
			test_cert::ca_pem,
			test_cert::intermediate_pem,
			test_cert::server_pem,
			test_cert::client_pem
		));
		CHECK(cert.not_expired_for(24h, now()));
		CHECK_FALSE(cert.not_expired_for(24h * 365 * 100, now()));
		CHECK_FALSE(cert.not_expired_for(24h, far_past()));
		CHECK_FALSE(cert.not_expired_for(24h, far_future()));
	}

	SECTION("not_expired_for: null")
	{
		CHECK_FALSE(null.not_expired_for(24h, now()));
		CHECK_FALSE(null.not_expired_for(24h, far_past()));
		CHECK_FALSE(null.not_expired_for(24h, far_future()));
	}

	SECTION("digest: sha1")
	{
		auto [pem, expected] = GENERATE(table<std::string_view, std::string_view>({
			{
				test_cert::ca_pem,
				"27566d2bae77ab393faaf1080c528c9973a6ffb4"
			},
			{
				test_cert::intermediate_pem,
				"fafd5f4eee2e99c7f1591425dfaf2abee2b01527"
			},
			{
				test_cert::server_pem,
				"776f3c5bb3b39da38ab80e5f7f909fbcff36d42a"
			},
			{
				test_cert::client_pem,
				"c9483ca1d965e9a769f0e7d705f3d6bb3ce248ee"
			},
			{
				"",
				"0000000000000000000000000000000000000000"
			},
		}));
		auto cert = pem.size() ? certificate::from_pem(pem) : null;
		auto digest = pal_test::to_hex(cert.digest<pal::crypto::sha1>());
		CHECK(digest == expected);
	}

	SECTION("digest: sha512")
	{
		auto [pem, expected] = GENERATE(table<std::string_view, std::string_view>({
			{
				test_cert::ca_pem,
				"360599d2e088e6ee9d8e7398f903f03d145194c6b4e5766de1afc8280c1424f382f49d9c14017e36c60045c9605dead8342ae7281a8b6f70484e7783bc4bad93"
			},
			{
				test_cert::intermediate_pem,
				"f8902a809c310db4a324442ae9045d688af6a8ee7ce7a0625bcb38072668715c7d86f887b1299fe5e53946eb320e46786f4bdcf9f892fa49e0396a86babc45d1"
			},
			{
				test_cert::server_pem,
				"581f0348a6ceb99932de61a6f757ed096df8fc4d5de3525a59ae5a77cfc0cb3a27f7072f798ed769320b60901c2da0178670802799df7c6aa7753b21e134e10f"
			},
			{
				test_cert::client_pem,
				"4612c792a0ec069ad1f21f8ec1f6af827b473451535b9220655a59cc6402641ab2fed5eae77b9b406a7544f693a13afb4cbd3d43638c42434ae0a6678ec6528d"
			},
			{
				"",
				"00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
			},
		}));
		auto cert = pem.size() ? certificate::from_pem(pem) : null;
		auto digest = pal_test::to_hex(cert.digest<pal::crypto::sha512>());
		CHECK(digest == expected);
	}

	SECTION("serial_number")
	{
		auto [pem, expected] = GENERATE(table<std::string_view, std::string_view>({
			{ test_cert::ca_pem, "852749945d39e1c9" },
			{ test_cert::intermediate_pem, "1000" },
			{ test_cert::server_pem, "1001" },
			{ test_cert::client_pem, "1002" },
			{ "", "" },
		}));
		auto cert = pem.size() ? certificate::from_pem(pem) : null;

		uint8_t buf[64];
		auto serial_number = pal_test::to_hex(cert.serial_number(buf));
		CHECK(serial_number == expected);

		serial_number = pal_test::to_hex(cert.serial_number());
		CHECK(serial_number == expected);
	}

	SECTION("serial_number: buffer too small")
	{
		auto cert = certificate::from_pem(test_cert::ca_pem);
		uint8_t buf[1];
		auto span = cert.serial_number(buf);
		CHECK(span.data() == nullptr);
		CHECK(span.size_bytes() == 8);
	}

	SECTION("serial_number: alloc failure")
	{
		auto cert = certificate::from_pem(test_cert::ca_pem);

		if constexpr (pal::is_linux_build)
		{
			uint8_t buf[64];
			pal_test::bad_alloc_once x;
			auto span = cert.serial_number(buf);
			CHECK(span.data() == nullptr);
			CHECK(span.size_bytes() == 8);
		}

		auto f = [&]()
		{
			pal_test::bad_alloc_once x;
			(void)cert.serial_number();
		};
		CHECK_THROWS_AS(f(), std::bad_alloc);
	}

	SECTION("authority_key_identifier")
	{
		auto [pem, expected] = GENERATE(table<std::string_view, std::string_view>({
			{ test_cert::ca_pem, "8ff911972ac64007a28ec7d461a66680ab106649" },
			{ test_cert::intermediate_pem, "8ff911972ac64007a28ec7d461a66680ab106649" },
			{ test_cert::server_pem, "423097971d0f67f28993bf9e4173bba30932d0e1" },
			{ test_cert::client_pem, "423097971d0f67f28993bf9e4173bba30932d0e1" },
			{ "", "" },
		}));
		auto cert = pem.size() ? certificate::from_pem(pem) : null;

		uint8_t buf[64];
		auto aik = pal_test::to_hex(cert.authority_key_identifier(buf));
		CHECK(aik == expected);

		aik = pal_test::to_hex(cert.authority_key_identifier());
		CHECK(aik == expected);
	}

	SECTION("authority_key_identifier: no extension")
	{
		auto cert = certificate::from_pem(test_cert::self_signed_pem);
		uint8_t buf[64];
		auto aik = pal_test::to_hex(cert.authority_key_identifier(buf));
		CHECK(aik.data() != nullptr);
		CHECK(aik.empty());
	}

	SECTION("authority_key_identifier: buffer too small")
	{
		auto cert = certificate::from_pem(test_cert::ca_pem);
		uint8_t buf[1];
		auto span = cert.authority_key_identifier(buf);
		CHECK(span.data() == nullptr);
		CHECK(span.size_bytes() == 20);
	}

	SECTION("authority_key_identifier: alloc failure")
	{
		auto cert = certificate::from_pem(test_cert::ca_pem);

		if constexpr (pal::is_linux_build)
		{
			// alloc failure hits querying X509v3 extension
			pal_test::bad_alloc_once x;
			auto aik = cert.authority_key_identifier();
			CHECK(aik.data() == nullptr);
			CHECK(aik.size() == 0);
		}
		else
		{
			// alloc failure hits vector
			auto f = [&]()
			{
				pal_test::bad_alloc_once x;
				(void)cert.authority_key_identifier();
			};
			CHECK_THROWS_AS(f(), std::bad_alloc);
		}
	}
}


} // namespace
