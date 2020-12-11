#include <pal/conv>
#include <pal/test>
#include <array>


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

	static size_t to_size (const std::string_view &in) noexcept
	{
		return pal::to_base64_size(std::span{in});
	}

	static pal::conv_result from (const std::string_view &in, char *out) noexcept
	{
		return pal::from_base64(in.data(), in.data() + in.size(), out);
	}

	static size_t from_size (const std::string_view &in) noexcept
	{
		return pal::from_base64_size(std::span{in});
	}
};


struct hex
{
	static inline const success_type::list success =
	{
		{"", ""},
		{"hex_string", "6865785f737472696e67"},
		{"HEX_STRING", "4845585f535452494e47"},
		{"hex\nstring", "6865780a737472696e67"},
		{"hello, world", "68656c6c6f2c20776f726c64"},
	};

	static inline const size_failure_type::list size_failure =
	{
		{ "7" },
		{ "746" },
	};

	static inline const from_failure_type::list from_failure =
	{
		{ ".4657374", 0 },
		{ "7.657374", 1 },
		{ "74.57374", 2 },
		{ "746.7374", 3 },
		{ "7465.374", 4 },
		{ "74657.74", 5 },
		{ "746573.4", 6 },
		{ "7465737.", 7 },
	};

	static pal::conv_result to (const std::string_view &in, char *out) noexcept
	{
		return pal::to_hex(in.data(), in.data() + in.size(), out);
	}

	static size_t to_size (const std::string_view &in) noexcept
	{
		return pal::to_hex_size(std::span{in});
	}

	static pal::conv_result from (const std::string_view &in, char *out) noexcept
	{
		return pal::from_hex(in.data(), in.data() + in.size(), out);
	}

	static size_t from_size (const std::string_view &in) noexcept
	{
		return pal::from_hex_size(std::span{in});
	}
};


constexpr auto generate_table () noexcept
{
	std::array<uint8_t, 256> table{};
	for (uint16_t v = 0;  v < table.size();  ++v)
	{
		table[v] = static_cast<uint8_t>(v);
	}
	return table;
}


TEMPLATE_TEST_CASE("conv", "",
	base64,
	hex)
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
			CHECK(TestType::to_size(decoded) >= encoded.size());
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
			CHECK(TestType::from_size(encoded) >= decoded.size());
		}
	}

	SECTION("failure")
	{
		SECTION("from_size")
		{
			auto &[encoded] = GENERATE(values(TestType::size_failure));
			CAPTURE(encoded);
			CHECK(!TestType::from_size(encoded));
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

	SECTION("binary")
	{
		constexpr auto table = generate_table();

		std::string_view in{reinterpret_cast<const char *>(table.data()), table.size()};
		auto conv_result = TestType::to(in, buf);
		std::string result{buf, conv_result.last_out};

		conv_result = TestType::from(result, buf);
		std::string_view out(buf, conv_result.last_out - buf);

		CHECK(in == out);
	}
}


} // namespace
