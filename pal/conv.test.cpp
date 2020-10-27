#include <pal/conv>
#include <pal/test>


namespace {


struct success_type
{
	using list = std::initializer_list<success_type>;
	std::string decoded, encoded;
};


struct failure_type
{
	using list = std::initializer_list<failure_type>;
	std::string encoded;
	size_t buf_size;
	size_t expected_size;
	std::errc expected_ec;
};


struct base64
{
	static inline const success_type::list success =
	{
		{ "", "" },
		{ "f", "Zg==" },
		{ "fo", "Zm8=" },
		{ "foo", "Zm9v" },
		{ "foob", "Zm9vYg==" },
		{ "fooba", "Zm9vYmE=" },
		{ "foobar", "Zm9vYmFy" },
	};

	static inline const failure_type::list failure =
	{
		// 'test' -> 'dGVzdA=='
		{ "dGVzdA=",  64, 0, std::errc::message_size },
		{ "dGVzdA==",  0, 0, std::errc::value_too_large },
		{ "dGVzdA==",  3, 0, std::errc::value_too_large },
		{ "dGVzdAE=",  4, 0, std::errc::value_too_large },
		{ "dGVzdAEx",  5, 0, std::errc::value_too_large },
		{ ".GVzdA==", 64, 0, std::errc::invalid_argument },
		{ "d.VzdA==", 64, 1, std::errc::invalid_argument },
		{ "dG.zdA==", 64, 2, std::errc::invalid_argument },
		{ "dGV.dA==", 64, 3, std::errc::invalid_argument },
		{ "dGVz.A==", 64, 4, std::errc::invalid_argument },
		{ "dGVzd.==", 64, 5, std::errc::invalid_argument },
		{ "dGVzdA.=", 64, 6, std::errc::invalid_argument },
		{ "dGVzdA=.", 64, 7, std::errc::invalid_argument },

		// 'test1' -> 'dGVzdDE='
		{ ".GVzdDE=", 64, 0, std::errc::invalid_argument },
		{ "d.VzdDE=", 64, 1, std::errc::invalid_argument },
		{ "dG.zdDE=", 64, 2, std::errc::invalid_argument },
		{ "dGV.dDE=", 64, 3, std::errc::invalid_argument },
		{ "dGVz.DE=", 64, 4, std::errc::invalid_argument },
		{ "dGVzd.E=", 64, 5, std::errc::invalid_argument },
		{ "dGVzdD.=", 64, 6, std::errc::invalid_argument },
		{ "dGVzdDE.", 64, 7, std::errc::invalid_argument },

		// 'test12' -> 'dGVzdDEx'
		{ ".GVzdDEx", 64, 0, std::errc::invalid_argument },
		{ "d.VzdDEx", 64, 1, std::errc::invalid_argument },
		{ "dG.zdDEx", 64, 2, std::errc::invalid_argument },
		{ "dGV.dDEx", 64, 3, std::errc::invalid_argument },
		{ "dGVz.DEx", 64, 4, std::errc::invalid_argument },
		{ "dGVzd.Ex", 64, 5, std::errc::invalid_argument },
		{ "dGVzdD.x", 64, 6, std::errc::invalid_argument },
		{ "dGVzdDE.", 64, 7, std::errc::invalid_argument },
	};

	static pal::conv_result to (
		const pal::mutable_buffer &dest,
		const pal::const_buffer &source) noexcept
	{
		return pal::to_base64(dest, source);
	}

	static pal::conv_result from (
		const pal::mutable_buffer &dest,
		const pal::const_buffer &source) noexcept
	{
		return pal::from_base64(dest, source);
	}
};


TEMPLATE_TEST_CASE("conv", "",
	base64)
{
	char buf[1024];

	SECTION("success")
	{
		auto &[decoded, encoded] =
			GENERATE(values(TestType::success));
		CAPTURE(decoded);
		CAPTURE(encoded);

		SECTION("to")
		{
			auto [size, ec] = TestType::to(
				pal::buffer(buf),
				pal::buffer(decoded)
			);
			CHECK(ec == std::errc{});
			CHECK(std::string(buf, size) == encoded);
		}

		SECTION("from")
		{
			auto [size, ec] = TestType::from(
				pal::buffer(buf),
				pal::buffer(encoded)
			);
			CHECK(ec == std::errc{});
			CHECK(std::string(buf, size) == decoded);
		}
	}

	SECTION("value_too_large")
	{
		auto buf_size = GENERATE(1, 7);
		auto [size, ec] = TestType::to(
			pal::buffer(buf, buf_size),
			pal::buffer("test")
		);
		CHECK(ec == std::errc::value_too_large);
	}

	SECTION("failure")
	{
		auto &[encoded, buf_size, expected_size, expected_ec] =
			GENERATE(values(TestType::failure));
		CAPTURE(encoded);

		auto [size, ec] = TestType::from(
			pal::buffer(buf, buf_size),
			pal::buffer(encoded)
		);
		CHECK(ec == expected_ec);
		CHECK(size == expected_size);
	}
}


} // namespace
