#include <pal/hash>
#include <pal/test>


namespace {


TEST_CASE("hash")
{
	char data[] = "0123";
	auto a = pal::fnv_1a_64(data);
	uint64_t b{};

	SECTION("fnv_1a_64")
	{
		data[3]++;
		b = pal::fnv_1a_64(data);
	}

	SECTION("hash_128_to_64")
	{
		b = pal::hash_128_to_64(1, a);
	}

	// expect differ more than last bit position
	CHECK((a ^ b) != 1);
}


} // namespace
