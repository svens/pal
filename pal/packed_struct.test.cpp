#include <pal/packed_struct.hpp>
#include <pal/byte_order.hpp>
#include <pal/test.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>

namespace
{

enum class color: uint16_t
{
	red = 0x0102,
	green = 0x0304,
};

struct inner
{
	pal::big_endian<uint16_t> type;
	pal::big_endian<uint16_t> length;
};

struct outer
{
	inner head;
	std::array<std::byte, 3> cookie;
	pal::little_endian<uint32_t> id;
	pal::big_endian<color> tint;
	std::byte flags;
};

struct with_native_integer
{
	std::byte tag;
	uint32_t value;
};

struct with_padding
{
	uint8_t tag;
	uint16_t value;
};

struct with_pointer
{
	const char *p;
};

struct not_trivially_copyable
{
	std::array<std::byte, 4> bytes;

	not_trivially_copyable (const not_trivially_copyable &) noexcept
	{
	}
};

struct not_implicit_lifetime
{
	std::byte b;

	not_implicit_lifetime () noexcept
	{
	}

	not_implicit_lifetime (const not_implicit_lifetime &) = delete;
	not_implicit_lifetime &operator= (const not_implicit_lifetime &) = default;
};

enum unscoped
{
	unscoped_value,
};

enum unscoped_fixed: uint8_t
{
	unscoped_fixed_value,
};

enum class flag: bool
{
	off,
	on,
};

struct empty
{
};

struct with_partial_bit_field
{
	uint8_t low: 4;
};

template <typename T>
concept endian_value = requires
{
	typename pal::big_endian<T>;
};

static_assert(endian_value<uint8_t>);
static_assert(endian_value<int64_t>);
static_assert(endian_value<char16_t>);
static_assert(endian_value<color>);
static_assert(endian_value<unscoped_fixed>);
static_assert(endian_value<std::byte>);
static_assert(!endian_value<bool>);
static_assert(!endian_value<float>);
static_assert(!endian_value<unscoped>);
static_assert(!endian_value<const uint16_t>);
static_assert(!endian_value<flag>);
static_assert(!endian_value<inner>);

static_assert(pal::packed_struct<std::byte>);
static_assert(pal::packed_struct<std::byte[4]>);
static_assert(pal::packed_struct<uint8_t>);
static_assert(pal::packed_struct<std::array<std::byte, 12>>);
static_assert(pal::packed_struct<pal::big_endian<uint16_t>>);
static_assert(pal::packed_struct<pal::little_endian<uint64_t>>);
static_assert(pal::packed_struct<pal::big_endian<color>>);
static_assert(pal::packed_struct<inner>);
static_assert(pal::packed_struct<outer>);
static_assert(!pal::packed_struct<uint16_t>);
static_assert(!pal::packed_struct<with_native_integer>);
static_assert(!pal::packed_struct<with_padding>);
static_assert(!pal::packed_struct<with_pointer>);
static_assert(!pal::packed_struct<not_trivially_copyable>);
static_assert(!pal::packed_struct<not_implicit_lifetime>);
static_assert(!pal::packed_struct<empty>);
static_assert(!pal::packed_struct<with_partial_bit_field>);
static_assert(!pal::packed_struct<const inner>);
static_assert(!pal::packed_struct<volatile std::byte>);

static_assert(sizeof(inner) == 4);
static_assert(sizeof(outer) == 4 + 3 + 4 + 2 + 1);
static_assert(alignof(outer) == 1);

template <typename T>
constexpr auto bytes_of (T value) noexcept
{
	return std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
}

static_assert(
	bytes_of(pal::big_endian<uint32_t>{0x01020304})
	== std::array{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}}
);
static_assert(
	bytes_of(pal::little_endian<uint32_t>{0x01020304})
	== std::array{std::byte{0x04}, std::byte{0x03}, std::byte{0x02}, std::byte{0x01}}
);
static_assert(bytes_of(pal::big_endian<color>{color::red}) == std::array{std::byte{0x01}, std::byte{0x02}});
static_assert(bytes_of(pal::little_endian<color>{color::red}) == std::array{std::byte{0x02}, std::byte{0x01}});
static_assert(bytes_of(pal::big_endian<uint16_t>{}) == std::array{std::byte{0x00}, std::byte{0x00}});
static_assert(pal::big_endian<uint16_t>{0x1234} == 0x1234);
static_assert(pal::little_endian<uint16_t>{0x1234} == 0x1234);
static_assert(pal::big_endian<int16_t>{-2} == -2);
static_assert(pal::big_endian<color>{color::green} == color::green);
static_assert(pal::big_endian<uint16_t>{0x1234} == pal::big_endian<uint16_t>{0x1234});
static_assert(std::same_as<pal::big_endian<uint16_t>::value_type, uint16_t>);
static_assert(std::same_as<decltype(pal::big_endian<uint16_t>{}.value()), uint16_t>);
static_assert(std::same_as<decltype(pal::big_endian<color>{}.value()), color>);
static_assert(pal::little_endian<uint32_t>{0x01020304}.value() == 0x01020304);
static_assert((true ? pal::big_endian<uint16_t>{0x1234}.value() : 0) == 0x1234);
static_assert(std::to_underlying(pal::big_endian<color>{color::green}.value()) == 0x0304);

TEMPLATE_TEST_CASE(
	"packed_struct/basic_endian",
	"",
	uint8_t,
	uint16_t,
	uint32_t,
	uint64_t,
	int8_t,
	int16_t,
	int32_t,
	int64_t)
{
	using limits = std::numeric_limits<TestType>;
	const auto value = GENERATE(
		(limits::min)(), (limits::max)(), static_cast<TestType>((limits::max)() / 2), static_cast<TestType>(1)
	);
	CAPTURE(value);

	SECTION("big")
	{
		const pal::big_endian<TestType> field = value;
		CHECK(field == value);
		const auto network = pal::hton(value);
		CHECK(std::memcmp(&field, &network, sizeof(value)) == 0);
	}

	SECTION("little")
	{
		pal::little_endian<TestType> field;
		CHECK(field == TestType{});
		field = value;
		CHECK(field == value);
		const auto swapped = std::endian::native == std::endian::little ? value : std::byteswap(value);
		CHECK(std::memcmp(&field, &swapped, sizeof(value)) == 0);
	}
}

TEST_CASE("packed_struct/packed_as")
{
	alignas(uint64_t) std::array<std::byte, 1 + sizeof(outer) + 1> storage{};
	const std::span buffer{storage};
	const auto bytes = buffer.subspan<1, sizeof(outer)>();

	SECTION("write")
	{
		auto &o = pal::packed_as<outer>(bytes);
		static_assert(std::same_as<decltype(pal::packed_as<outer>(bytes)), outer &>);
		CHECK(static_cast<void *>(&o) == bytes.data());

		o.head.type = 0x0102;
		o.head.length = 0x0304;
		o.cookie = {std::byte{0x05}, std::byte{0x06}, std::byte{0x07}};
		o.id = 0x08090a0b;
		o.tint = color::green;
		o.flags = std::byte{0x0c};

		CHECK(pal_test::to_hex(storage) == "0001020304050607" "0b0a0908" "0304" "0c" "00");

		o.head.length = static_cast<uint16_t>(o.head.length + 4);
		CHECK(o.head.length == 0x0308);
		CHECK(pal_test::to_hex(buffer.subspan<3, 2>()) == "0308");
	}

	SECTION("read")
	{
		for (size_t i = 0; i < storage.size(); ++i)
		{
			storage[i] = static_cast<std::byte>(i);
		}

		const std::span<const std::byte, sizeof(outer)> const_bytes = bytes;
		const auto &o = pal::packed_as<outer>(const_bytes);
		static_assert(std::same_as<decltype(pal::packed_as<outer>(const_bytes)), const outer &>);
		CHECK(static_cast<const void *>(&o) == bytes.data());

		CHECK(o.head.type == 0x0102);
		CHECK(o.head.length == 0x0304);
		CHECK(o.cookie == std::array{std::byte{0x05}, std::byte{0x06}, std::byte{0x07}});
		CHECK(o.id == 0x0b0a0908);
		CHECK(o.tint == static_cast<color>(0x0c0d));
		CHECK(o.flags == std::byte{0x0e});

		const outer copy = o;
		storage.fill(std::byte{0xff});
		CHECK(copy.head.type == 0x0102);
		CHECK(o.head.type == 0xffff);
	}

	SECTION("field")
	{
		auto &length = pal::packed_as<pal::big_endian<uint16_t>>(buffer.subspan<3, 2>());
		length = 0xa1b2;
		CHECK(pal::packed_as<outer>(bytes).head.length == 0xa1b2);
		CHECK(pal_test::to_hex(buffer.subspan<3, 2>()) == "a1b2");
	}
}

} // namespace
