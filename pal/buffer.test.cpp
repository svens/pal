#include <pal/buffer.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{

#pragma pack(push, 1)
struct packed_struct
{
	uint16_t id;
	uint32_t value;
};
#pragma pack(pop)
static_assert(std::is_trivially_copyable_v<packed_struct>);
static_assert(sizeof(packed_struct) == 6);

TEMPLATE_TEST_CASE("pal/buffer", "", char, int, packed_struct, std::string_view, std::string)
{
	SECTION("mutable T")
	{
		if constexpr (pal::__buffer::is_byte_safe_v<TestType>)
		{
			TestType t{};
			auto b = pal::buffer(&t);
			static_assert(pal::mutable_buffer_sequence<decltype(b)>);
			REQUIRE(b.size() == 1);
			CHECK(b[0].size_bytes() == sizeof(TestType));
			CHECK(b[0].data() == reinterpret_cast<std::byte *>(&t));
		}
		else if constexpr (std::is_same_v<TestType, std::string>)
		{
			// caller pre-sizes, buffer() just converts — lifecycle is caller's responsibility
			std::string s(5, '\0');
			auto b = pal::buffer(s);
			static_assert(pal::mutable_buffer_sequence<decltype(b)>);
			REQUIRE(b.size() == 1);
			CHECK(b[0].size_bytes() == s.size());
			CHECK(b[0].data() == reinterpret_cast<std::byte *>(s.data()));
		}
		else
		{
			// string_view: mutable overload unreachable — elements are const char
			static_assert(std::is_same_v<TestType, std::string_view>);
		}
	}

	SECTION("const T")
	{
		if constexpr (pal::const_buffer<TestType>)
		{
			const TestType t = "hello";
			auto b = pal::buffer(t);
			static_assert(pal::const_buffer_sequence<decltype(b)>);
			REQUIRE(b.size() == 1);
			CHECK(b[0].size_bytes() == std::size(t) * sizeof(*std::data(t)));
			CHECK(b[0].data() == reinterpret_cast<const std::byte *>(std::data(t)));
		}
		else
		{
			const TestType t{};
			auto b = pal::buffer(&t);
			static_assert(pal::const_buffer_sequence<decltype(b)>);
			REQUIRE(b.size() == 1);
			CHECK(b[0].size_bytes() == sizeof(TestType));
			CHECK(b[0].data() == reinterpret_cast<const std::byte *>(&t));
		}
	}

	SECTION("mutable T[]")
	{
		if constexpr (pal::__buffer::is_byte_safe_v<TestType>)
		{
			TestType arr[5]{};
			std::span s{arr};
			auto b = pal::buffer(s);
			static_assert(pal::mutable_buffer_sequence<decltype(b)>);
			REQUIRE(b.size() == 1);
			CHECK(b[0].size_bytes() == sizeof(arr));
			CHECK(b[0].data() == reinterpret_cast<std::byte *>(arr));
		}
		else
		{
			static_assert(
				std::is_same_v<TestType, std::string_view> || std::is_same_v<TestType, std::string>
			);
		}
	}

	SECTION("const T[]")
	{
		if constexpr (pal::__buffer::is_byte_safe_v<TestType>)
		{
			const TestType arr[5]{};
			auto b = pal::buffer(std::span{arr});
			static_assert(pal::const_buffer_sequence<decltype(b)>);
			REQUIRE(b.size() == 1);
			CHECK(b[0].size_bytes() == sizeof(arr));
			CHECK(b[0].data() == reinterpret_cast<const std::byte *>(arr));
		}
		else
		{
			static_assert(
				std::is_same_v<TestType, std::string_view> || std::is_same_v<TestType, std::string>
			);
		}
	}

	SECTION("mutable std::array<T>")
	{
		if constexpr (pal::__buffer::is_byte_safe_v<TestType>)
		{
			std::array<TestType, 5> arr{};
			auto b = pal::buffer(arr);
			static_assert(pal::mutable_buffer_sequence<decltype(b)>);
			REQUIRE(b.size() == 1);
			CHECK(b[0].size_bytes() == sizeof(arr));
			CHECK(b[0].data() == reinterpret_cast<std::byte *>(arr.data()));
		}
		else
		{
			static_assert(
				std::is_same_v<TestType, std::string_view> || std::is_same_v<TestType, std::string>
			);
		}
	}

	SECTION("const std::array<T>")
	{
		if constexpr (pal::__buffer::is_byte_safe_v<TestType>)
		{
			const std::array<TestType, 5> arr{};
			auto b = pal::buffer(arr);
			static_assert(pal::const_buffer_sequence<decltype(b)>);
			REQUIRE(b.size() == 1);
			CHECK(b[0].size_bytes() == sizeof(arr));
			CHECK(b[0].data() == reinterpret_cast<const std::byte *>(arr.data()));
		}
		else
		{
			static_assert(
				std::is_same_v<TestType, std::string_view> || std::is_same_v<TestType, std::string>
			);
		}
	}

	SECTION("mutable std::vector<T>")
	{
		if constexpr (pal::__buffer::is_byte_safe_v<TestType>)
		{
			std::vector<TestType> v(5);
			auto b = pal::buffer(v);
			static_assert(pal::mutable_buffer_sequence<decltype(b)>);
			REQUIRE(b.size() == 1);
			CHECK(b[0].size_bytes() == v.size() * sizeof(TestType));
			CHECK(b[0].data() == reinterpret_cast<std::byte *>(v.data()));
		}
		else
		{
			static_assert(
				std::is_same_v<TestType, std::string_view> || std::is_same_v<TestType, std::string>
			);
		}
	}

	SECTION("const std::vector<T>")
	{
		if constexpr (pal::__buffer::is_byte_safe_v<TestType>)
		{
			const std::vector<TestType> v(5);
			auto b = pal::buffer(v);
			static_assert(pal::const_buffer_sequence<decltype(b)>);
			REQUIRE(b.size() == 1);
			CHECK(b[0].size_bytes() == v.size() * sizeof(TestType));
			CHECK(b[0].data() == reinterpret_cast<const std::byte *>(v.data()));
		}
		else
		{
			static_assert(
				std::is_same_v<TestType, std::string_view> || std::is_same_v<TestType, std::string>
			);
		}
	}

	SECTION("mutable scatter/gather")
	{
		if constexpr (pal::__buffer::is_byte_safe_v<TestType>)
		{
			TestType a[3]{}, b[4]{};
			std::span sa{a};
			std::span sb{b};
			auto bufs = pal::buffer(sa, sb);
			static_assert(pal::mutable_buffer_sequence<decltype(bufs)>);
			REQUIRE(bufs.size() == 2);
			CHECK(bufs[0].size_bytes() == sizeof(a));
			CHECK(bufs[0].data() == reinterpret_cast<std::byte *>(a));
			CHECK(bufs[1].size_bytes() == sizeof(b));
			CHECK(bufs[1].data() == reinterpret_cast<std::byte *>(b));
		}
		else
		{
			static_assert(
				std::is_same_v<TestType, std::string_view> || std::is_same_v<TestType, std::string>
			);
		}
	}

	SECTION("const scatter/gather")
	{
		if constexpr (pal::__buffer::is_byte_safe_v<TestType>)
		{
			const TestType a[3]{}, b[4]{};
			auto bufs = pal::buffer(std::span{a}, std::span{b});
			static_assert(pal::const_buffer_sequence<decltype(bufs)>);
			REQUIRE(bufs.size() == 2);
			CHECK(bufs[0].size_bytes() == sizeof(a));
			CHECK(bufs[0].data() == reinterpret_cast<const std::byte *>(a));
			CHECK(bufs[1].size_bytes() == sizeof(b));
			CHECK(bufs[1].data() == reinterpret_cast<const std::byte *>(b));
		}
		else
		{
			static_assert(
				std::is_same_v<TestType, std::string_view> || std::is_same_v<TestType, std::string>
			);
		}
	}
}

} // namespace
