#include <pal/conv>
#include <pal/test>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
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
		{ "Z" },
		{ "Zg" },
		{ "Zg=" },
		{ "Zm9v=" },
		{ "Zm9v==" },

		// 'test' -> 'dGVzdA=='
		{ ".GVzdA==" },
		{ "d.VzdA==" },
		{ "dG.zdA==" },
		{ "dGV.dA==" },
		{ "dGVz.A==" },
		{ "dGVzd.==" },
		{ "dGVzdA.=" },
		{ "dGVzdA=." },

		// 'test1' -> 'dGVzdDE='
		{ ".GVzdDE=" },
		{ "d.VzdDE=" },
		{ "dG.zdDE=" },
		{ "dGV.dDE=" },
		{ "dGVz.DE=" },
		{ "dGVzd.E=" },
		{ "dGVzdD.=" },
		{ "dGVzdDE." },

		// 'test12' -> 'dGVzdDEx'
		{ ".GVzdDEx" },
		{ "d.VzdDEx" },
		{ "dG.zdDEx" },
		{ "dGV.dDEx" },
		{ "dGVz.DEx" },
		{ "dGVzd.Ex" },
		{ "dGVzdD.x" },
		{ "dGVzdDE." },
	};

	static char *to (const std::string_view &in, char *out) noexcept
	{
		return pal::to_base64(in, out);
	}

	static size_t to_size (const std::string_view &in) noexcept
	{
		return pal::to_base64_size(in);
	}

	static char *from (const std::string_view &in, char *out) noexcept
	{
		return pal::from_base64(in, out);
	}

	static size_t from_size (const std::string_view &in) noexcept
	{
		return pal::from_base64_size(in);
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
		{ "7" },
		{ "746" },
		{ "74657" },
		{ "7465737" },
		{ ".4657374" },
		{ "7.657374" },
		{ "74.57374" },
		{ "746.7374" },
		{ "7465.374" },
		{ "74657.74" },
		{ "746573.4" },
		{ "7465737." },
	};

	static char *to (const std::string_view &in, char *out) noexcept
	{
		return pal::to_hex(in, out);
	}

	static size_t to_size (const std::string_view &in) noexcept
	{
		return pal::to_hex_size(in);
	}

	static char *from (const std::string_view &in, char *out) noexcept
	{
		return pal::from_hex(in, out);
	}

	static size_t from_size (const std::string_view &in) noexcept
	{
		return pal::from_hex_size(in);
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
			auto last_out = TestType::to(decoded, buf);
			std::string result{buf, last_out};
			CHECK(result == encoded);
		}

		SECTION("to_size")
		{
			CHECK(TestType::to_size(decoded) >= encoded.size());
		}

		SECTION("from")
		{
			auto out = TestType::from(encoded, buf);
			std::string result(buf, out);
			CHECK(result == decoded);
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
			CHECK(TestType::from_size(encoded) == 0);
		}

		SECTION("from")
		{
			auto &[encoded] = GENERATE(values(TestType::from_failure));
			CAPTURE(encoded);
			CHECK(TestType::from(encoded, buf) == nullptr);
		}
	}

	SECTION("binary")
	{
		constexpr auto table = generate_table();

		std::string_view in{reinterpret_cast<const char *>(table.data()), table.size()};
		auto last_out = TestType::to(in, buf);
		std::string result{buf, last_out};

		last_out = TestType::from(result, buf);
		std::string out{buf, last_out};

		CHECK(in == out);
	}
}


} // namespace
