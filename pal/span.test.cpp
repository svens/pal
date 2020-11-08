#include <pal/span>
#include <pal/test>


namespace {


TEMPLATE_TEST_CASE("span", "",
	uint8_t,
	const uint8_t,
	uint64_t,
	const uint64_t)
{
	TestType data_1[] = { 1 }, data_2[] = { 1, 2 };
	std::span span_1(data_1, 1);
	std::span span_2(data_2, 2);
	std::span<TestType> spans[] = { span_1, span_2 };

	SECTION("span_sequence: single")
	{
		CHECK(pal::span_sequence_begin(span_1) == &span_1);
		CHECK(pal::span_sequence_end(span_1) == &span_1 + 1);
	}

	SECTION("span_sequence: multiple")
	{
		CHECK(pal::span_sequence_begin(spans) == &spans[0]);
		CHECK(pal::span_sequence_end(spans) == &spans[2]);
	}

	SECTION("span_size_bytes")
	{
		CHECK(pal::span_size_bytes(span_1) == sizeof(data_1));
		CHECK(pal::span_size_bytes(span_2) == sizeof(data_2));
		CHECK(pal::span_size_bytes(spans) == sizeof(data_1) + sizeof(data_2));
	}

	SECTION("span_size_bytes: empty")
	{
		std::vector<std::remove_cv_t<TestType>> data;
		CHECK(pal::span_size_bytes(std::span{data}) == 0);
	}
}


} // namespace
