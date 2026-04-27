// Benchmark results (2026-04-26, release builds, --benchmark-samples 200)
//
// benchmark       macOS/clang   Linux/GCC   Windows/MSVC
// v4                  4.5 ns      8.2 ns       16.7 ns
// v6/no_copy         12.0 ns     29.8 ns       13.9 ns
// v6/with_copy       12.6 ns     29.5 ns       18.6 ns
//
// v6/with_copy retains the old approach (copy 16 bytes into address_v6, then hash)
// for comparison. v6/no_copy hashes sin6_addr.s6_addr directly — adopted as the
// implementation because MSVC does not optimize away the intermediate copy (~25%
// slower), while clang and GCC optimize both paths to equivalent code.

#include <pal/net/ip/tcp.hpp>
#include <pal/net/ip/udp.hpp>
#include <pal/hash.hpp>
#include <algorithm>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{

using namespace pal::net::ip;

TEST_CASE("net/ip/basic_endpoint/hash", "[!benchmark]")
{
	const tcp::endpoint v4{address_v4::loopback, port_type::https};
	const udp::endpoint v6{address_v6::loopback, port_type::https};

	BENCHMARK("v4")
	{
		return v4.hash();
	};

	BENCHMARK("v6/no_copy")
	{
		const auto *sa = static_cast<const ::sockaddr_in6 *>(v6.data());
		return pal::hash_128_to_64(
			static_cast<uint64_t>(AF_INET6),
			pal::hash_128_to_64(
				static_cast<uint64_t>(sa->sin6_port),
				pal::hash_128_to_64(
					pal::fnv_1a_64(sa->sin6_addr.s6_addr), static_cast<uint64_t>(sa->sin6_scope_id)
				)
			)
		);
	};

	BENCHMARK("v6/with_copy")
	{
		const auto *sa = static_cast<const ::sockaddr_in6 *>(v6.data());
		address_v6::bytes_type bytes{};
		std::ranges::copy(sa->sin6_addr.s6_addr, bytes.begin());
		return pal::hash_128_to_64(
			static_cast<uint64_t>(AF_INET6),
			pal::hash_128_to_64(
				static_cast<uint64_t>(sa->sin6_port),
				pal::hash_128_to_64(pal::fnv_1a_64(bytes), static_cast<uint64_t>(sa->sin6_scope_id))
			)
		);
	};
}

} // namespace
