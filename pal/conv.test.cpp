#include <pal/conv>
#include <pal/test>


namespace {


struct success_type
{
	using list = std::initializer_list<success_type>;
	std::string_view decoded, encoded;
};


struct size_failure_type
{
	using list = std::initializer_list<size_failure_type>;
	std::string_view encoded;
};


struct from_failure_type
{
	using list = std::initializer_list<from_failure_type>;
	std::string_view encoded;
	size_t expected_failure_offset;
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

	static inline const size_failure_type::list size_failure =
	{
		{ "Z" },
		{ "Zg" },
		{ "Zg=" },
		{ "Zm9v=" },
		{ "Zm9v==" },
	};

	static inline const from_failure_type::list from_failure =
	{
		// 'test' -> 'dGVzdA=='
		{ ".GVzdA==", 0 },
		{ "d.VzdA==", 1 },
		{ "dG.zdA==", 2 },
		{ "dGV.dA==", 3 },
		{ "dGVz.A==", 4 },
		{ "dGVzd.==", 5 },
		{ "dGVzdA.=", 6 },
		{ "dGVzdA=.", 7 },

		// 'test1' -> 'dGVzdDE='
		{ ".GVzdDE=", 0 },
		{ "d.VzdDE=", 1 },
		{ "dG.zdDE=", 2 },
		{ "dGV.dDE=", 3 },
		{ "dGVz.DE=", 4 },
		{ "dGVzd.E=", 5 },
		{ "dGVzdD.=", 6 },
		{ "dGVzdDE.", 7 },

		// 'test12' -> 'dGVzdDEx'
		{ ".GVzdDEx", 0 },
		{ "d.VzdDEx", 1 },
		{ "dG.zdDEx", 2 },
		{ "dGV.dDEx", 3 },
		{ "dGVz.DEx", 4 },
		{ "dGVzd.Ex", 5 },
		{ "dGVzdD.x", 6 },
		{ "dGVzdDE.", 7 },
	};

	static pal::conv_result to (const std::string_view &in, char *out) noexcept
	{
		return pal::to_base64(in.data(), in.data() + in.size(), out);
	}

	static pal::conv_size_result to_size (const std::string_view &in) noexcept
	{
		return pal::to_base64_size(in.data(), in.data() + in.size());
	}

	static pal::conv_result from (const std::string_view &in, char *out) noexcept
	{
		return pal::from_base64(in.data(), in.data() + in.size(), out);
	}

	static pal::conv_size_result from_size (const std::string_view &in) noexcept
	{
		return pal::from_base64_size(in.data(), in.data() + in.size());
	}
};


TEMPLATE_TEST_CASE("conv", "",
	base64)
{
	char buf[1024];

	SECTION("success")
	{
		auto &[decoded, encoded] = GENERATE(values(TestType::success));
		CAPTURE(decoded);
		CAPTURE(encoded);

		SECTION("to")
		{
			auto [last_in, last_out] = TestType::to(decoded, buf);
			std::string result{buf, last_out};
			CHECK(result == encoded);
			CHECK(last_in == decoded.data() + decoded.size());
		}

		SECTION("to_size")
		{
			auto [estimated_max_size, ec] = TestType::to_size(decoded);
			CHECK(ec == std::errc{});
			CHECK(estimated_max_size >= encoded.size());
		}

		SECTION("from")
		{
			auto [last_in, last_out] = TestType::from(encoded, buf);
			std::string result(buf, last_out);
			CHECK(result == decoded);
			CHECK(last_in == encoded.data() + encoded.size());
		}

		SECTION("from_size")
		{
			auto [estimated_max_size, ec] = TestType::from_size(encoded);
			CHECK(ec == std::errc{});
			CHECK(estimated_max_size >= decoded.size());
		}
	}

	SECTION("failure")
	{
		SECTION("from_size")
		{
			auto &[encoded] = GENERATE(values(TestType::size_failure));
			CAPTURE(encoded);

			auto [estimated_max_size, ec] = TestType::from_size(encoded);
			CHECK(ec == std::errc::invalid_argument);
		}

		SECTION("from")
		{
			auto &[encoded, expected_failure_offset] = GENERATE(values(TestType::from_failure));
			CAPTURE(encoded);
			auto [last_in, _] = TestType::from(encoded, buf);
			size_t failure_offset = last_in - encoded.data();
			CHECK(failure_offset == expected_failure_offset);
		}
	}
}


} // namespace
