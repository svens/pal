#include <pal/span>
#include <pal/test>
#include <array>
#include <vector>


namespace {


#if !__has_include(<span>)


constexpr ptrdiff_t N = 4;


TEMPLATE_TEST_CASE("span", "",
  (pal::span<uint8_t, N>),
  (pal::span<uint16_t, N>),
  (pal::span<uint32_t, N>),
  (pal::span<uint64_t, N>),
  (pal::span<uint8_t, pal::dynamic_extent>),
  (pal::span<uint16_t, pal::dynamic_extent>),
  (pal::span<uint32_t, pal::dynamic_extent>),
  (pal::span<uint64_t, pal::dynamic_extent>)
)
{
	using T = typename TestType::value_type;
	constexpr auto extent = TestType::extent;

	constexpr auto size_bytes = N * sizeof(T);

	T array[N] = { 1, 2, 3, 4 };
	const auto &const_array = array;

	std::array<T, N> std_array = { 1, 2, 3, 4 };
	const auto &const_std_array = std_array;

	std::vector<T> std_vector{array, array + N};
	const auto &const_std_vector = std_vector;


	SECTION("ctor")
	{
		constexpr auto empty_extent = extent == pal::dynamic_extent
			? pal::dynamic_extent
			: 0
		;
		pal::span<T, empty_extent> span;
		CHECK(span.empty());
		CHECK(span.size() == 0U);
		CHECK(span.size_bytes() == 0U);
		CHECK(span.data() == nullptr);
	}


	SECTION("ctor_with_ptr_and_count")
	{
		auto span = pal::span<T, extent>(array, N);
		CHECK_FALSE(span.empty());
		CHECK(span.data() == array);
		CHECK(span.size() == N);
		CHECK(span.size_bytes() == size_bytes);
	}


	SECTION("ctor_with_const_ptr_and_count")
	{
		auto span = pal::span<const T, extent>(const_array, N);
		CHECK_FALSE(span.empty());
		CHECK(span.data() == array);
		CHECK(span.size() == N);
		CHECK(span.size_bytes() == size_bytes);
	}


	SECTION("ctor_with_range")
	{
		auto span = pal::span<T, extent>(array, array + N);
		CHECK_FALSE(span.empty());
		CHECK(span.data() == array);
		CHECK(span.size() == N);
		CHECK(span.size_bytes() == size_bytes);
	}


	SECTION("ctor_with_const range")
	{
		auto span = pal::span<const T, extent>(const_array, const_array + N);
		CHECK_FALSE(span.empty());
		CHECK(span.data() == array);
		CHECK(span.size() == N);
		CHECK(span.size_bytes() == size_bytes);
	}


	SECTION("ctor_with_array")
	{
		auto span = pal::span<T, extent>(array);
		CHECK_FALSE(span.empty());
		CHECK(span.data() == array);
		CHECK(span.size() == N);
		CHECK(span.size_bytes() == size_bytes);
	}


	SECTION("ctor_with_const_array")
	{
		auto span = pal::span<const T, extent>(const_array);
		CHECK_FALSE(span.empty());
		CHECK(span.data() == array);
		CHECK(span.size() == N);
		CHECK(span.size_bytes() == size_bytes);
	}


	SECTION("ctor_with_std_array")
	{
		auto span = pal::span<T, extent>(std_array);
		CHECK_FALSE(span.empty());
		CHECK(span.data() == std_array.data());
		CHECK(span.size() == N);
		CHECK(span.size_bytes() == size_bytes);
	}


	SECTION("ctor_with_const_std_array")
	{
		auto span = pal::span<const T, extent>(const_std_array);
		CHECK_FALSE(span.empty());
		CHECK(span.data() == const_std_array.data());
		CHECK(span.size() == N);
		CHECK(span.size_bytes() == size_bytes);
	}


	SECTION("ctor_with_std_vector")
	{
		auto span = pal::span<T, extent>(std_vector);
		CHECK_FALSE(span.empty());
		CHECK(span.data() == std_vector.data());
		CHECK(span.size() == N);
		CHECK(span.size_bytes() == size_bytes);
	}


	SECTION("ctor_with_const_std_vector")
	{
		auto span = pal::span<const T, extent>(const_std_vector);
		CHECK_FALSE(span.empty());
		CHECK(span.data() == const_std_vector.data());
		CHECK(span.size() == N);
		CHECK(span.size_bytes() == size_bytes);
	}


	SECTION("ctor_with_same_span")
	{
		auto other = pal::span<T, extent>(array);
		pal::span<const T, extent> span = other;
		CHECK(span.extent == extent);
		CHECK_FALSE(span.empty());
		CHECK(span.data() == array);
		CHECK(span.size() == N);
		CHECK(span.size_bytes() == size_bytes);
	}


	SECTION("ctor_with_different_span")
	{
		constexpr auto different_extent = extent == pal::dynamic_extent
			? N
			: pal::dynamic_extent
		;
		auto other = pal::span<T, different_extent>(array);
		pal::span<const T, extent> span = other;
		CHECK(span.extent == extent);
		CHECK_FALSE(span.empty());
		CHECK(span.data() == array);
		CHECK(span.size() == N);
		CHECK(span.size_bytes() == size_bytes);
	}


	SECTION("index")
	{
		std::vector<T> data;
		auto span = pal::span<T, extent>(std_vector);
		for (auto index = 0U;  index < span.size();  ++index)
		{
			data.emplace_back(span[index]);
		}
		CHECK(std_vector == data);
	}


	SECTION("index_out_of_range")
	{
		if constexpr (!pal::expect_noexcept)
		{
			auto span = pal::span<T, extent>(std_vector);
			CHECK_THROWS_AS(span[-1], std::logic_error);
			CHECK_THROWS_AS(span[N], std::logic_error);
		}
	}


	SECTION("iterator")
	{
		std::vector<T> data;
		auto span = pal::span<T, extent>(std_vector);
		for (auto it = span.begin();  it != span.end();  ++it)
		{
			data.emplace_back(*it);
		}
		CHECK(std_vector == data);
	}


	SECTION("reverse_iterator")
	{
		std::vector<T> data;
		auto span = pal::span<T, extent>(std_vector);
		for (auto it = span.rbegin();  it != span.rend();  ++it)
		{
			data.emplace_back(*it);
		}
		auto reverse_data = std_vector;
		std::reverse(reverse_data.begin(), reverse_data.end());
		CHECK(reverse_data == data);
	}


	SECTION("first_dynamic")
	{
		auto span = pal::span<T, extent>(array);
		auto first_half = span.first(N / 2);
		CHECK(first_half.size() == N / 2);

		std::vector<T>
			expected(array, array + N / 2),
			data(first_half.begin(), first_half.end());
		CHECK(data == expected);
	}


	SECTION("first_static")
	{
		auto span = pal::span<T, extent>(array);
		auto first_half = span.template first<N / 2>();
		CHECK(first_half.size() == N / 2);

		std::vector<T>
			expected(array, array + N / 2),
			data(first_half.begin(), first_half.end());
		CHECK(data == expected);
	}


	SECTION("first_count_out_of_range")
	{
		if constexpr (!pal::expect_noexcept)
		{
			auto span = pal::span<T, extent>(array);
			CHECK_THROWS_AS(span.first(-1), std::logic_error);
			CHECK_THROWS_AS(span.first(span.size() + 1), std::logic_error);
		}
	}


	SECTION("last_dynamic")
	{
		auto span = pal::span<T, extent>(array);
		auto last_half = span.last(N / 2);
		CHECK(last_half.size() == N / 2);

		std::vector<T>
			expected(array + N / 2, array + N),
			data(last_half.begin(), last_half.end());
		CHECK(data == expected);
	}


	SECTION("last_static")
	{
		auto span = pal::span<T, extent>(array);
		auto last_half = span.template last<N / 2>();
		CHECK(last_half.size() == N / 2);

		std::vector<T>
			expected(array + N / 2, array + N),
			data(last_half.begin(), last_half.end());
		CHECK(data == expected);
	}


	SECTION("last_count_out_of_range")
	{
		if constexpr (!pal::expect_noexcept)
		{
			auto span = pal::span<T, extent>(array);
			CHECK_THROWS_AS(span.last(-1), std::logic_error);
			CHECK_THROWS_AS(span.last(span.size() + 1), std::logic_error);
		}
	}


	SECTION("subspan_dynamic")
	{
		auto span = pal::span<T, extent>(array);
		auto middle = span.subspan(1, N - 2);
		CHECK(middle.size() == N - 2);

		std::vector<T>
			expected(array + 1, array + N - 1),
			data(middle.begin(), middle.end());
		CHECK(data == expected);
	}


	SECTION("subspan_static")
	{
		auto span = pal::span<T, extent>(array);
		auto middle = span.template subspan<1, N - 2>();
		CHECK(middle.size() == N - 2);

		std::vector<T>
			expected(array + 1, array + N - 1),
			data(middle.begin(), middle.end());
		CHECK(data == expected);
	}


	SECTION("subspan_until_end_dynamic")
	{
		auto span = pal::span<T, extent>(array);
		auto middle = span.subspan(1);
		CHECK(middle.size() == N - 1);

		std::vector<T>
			expected(array + 1, array + N),
			data(middle.begin(), middle.end());
		CHECK(data == expected);
	}


	SECTION("subspan_until_end_static")
	{
		auto span = pal::span<T, extent>(array);
		auto middle = span.template subspan<1>();
		CHECK(middle.size() == N - 1);

		std::vector<T>
			expected(array + 1, array + N),
			data(middle.begin(), middle.end());
		CHECK(data == expected);
	}


	SECTION("subspan_offset_out_of_range")
	{
		if constexpr (!pal::expect_noexcept)
		{
			auto span = pal::span<T, extent>(array);
			CHECK_THROWS_AS(span.subspan(-1), std::logic_error);
			CHECK_THROWS_AS(span.subspan(span.size() + 1), std::logic_error);
		}
	}


	SECTION("subspan_count_out_of_range")
	{
		if constexpr (!pal::expect_noexcept)
		{
			auto span = pal::span<T, extent>(array);
			CHECK_THROWS_AS(span.subspan(0, -2), std::logic_error);
			CHECK_THROWS_AS(span.subspan(0, span.size() + 1), std::logic_error);
		}
	}


	SECTION("as_bytes")
	{
		auto span = pal::as_bytes(pal::span<T, extent>(array));
		CHECK(std::is_const_v<typename decltype(span)::element_type>);
		CHECK(span.data() == reinterpret_cast<const std::byte *>(array));
	}


	SECTION("as_writable_bytes")
	{
		auto span = pal::as_writable_bytes(pal::span<T, extent>(array));
		CHECK_FALSE(std::is_const_v<typename decltype(span)::element_type>);
		CHECK(span.data() == reinterpret_cast<std::byte *>(array));
	}
}


#endif


} // namespace
