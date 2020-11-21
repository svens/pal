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
			})
		);
		auto cert = certificate::from_pem(pem);
		CHECK(cert.version() == expected_version);
	}

	SECTION("version: null")
	{
		CHECK(null.version() == 0);
	}

	SECTION("not_before / not_after")
	{
		auto cert = certificate::from_pem(GENERATE(
			test_cert::ca_pem,
			test_cert::intermediate_pem,
			test_cert::server_pem,
			test_cert::client_pem
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
}


} // namespace
