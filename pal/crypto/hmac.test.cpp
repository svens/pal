#include <pal/crypto/hmac>
#include <pal/crypto/test>
#include <unordered_map>


namespace {


static const std::string
	key = "key",
	empty = "",
	lazy_dog = "The quick brown fox jumps over the lazy dog",
	lazy_cog = "The quick brown fox jumps over the lazy cog";


struct md5: pal::crypto::md5_hmac
{
	static inline std::unordered_map<std::string, std::string> hmac =
	{
		{ empty, "74e6f7298a9c2d168935f58c001bad88" },
		{ lazy_dog, "ad262969c53bc16032f160081c4a07a0" },
		{ lazy_cog, "b80343a0feacb4887ea5c323737644bd" },
		{ lazy_dog + lazy_cog, "f7e44aae188dcba7057d1641b51afc47" },
	};

	static inline std::unordered_map<std::string, std::string> hmac_with_key =
	{
		{ empty, "63530468a04e386459855da0063b6596" },
		{ lazy_dog, "80070713463e7749b90c2dc24911e275" },
		{ lazy_cog, "f734cebb1ebaf1480795349e4a515799" },
		{ lazy_dog + lazy_cog, "d1edfe826f38af7c04ac1455611609ec" },
	};
};


struct sha1: pal::crypto::sha1_hmac
{
	static inline std::unordered_map<std::string, std::string> hmac =
	{
		{ empty, "fbdb1d1b18aa6c08324b7d64b71fb76370690e1d" },
		{ lazy_dog, "2ba7f707ad5f187c412de3106583c3111d668de8" },
		{ lazy_cog, "158725d9967a4cb4df85c0f500accb283236ad79" },
		{ lazy_dog + lazy_cog, "a6dc30edd58a3d3d928900511b4ba219f4003609" },
	};

	static inline std::unordered_map<std::string, std::string> hmac_with_key =
	{
		{ empty, "f42bb0eeb018ebbd4597ae7213711ec60760843f" },
		{ lazy_dog, "de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9" },
		{ lazy_cog, "ad8d3f85da865d37e37ae5d7ab8ee32c5681ebc1" },
		{ lazy_dog + lazy_cog, "3cef80fd41cf8c39116690cc24a14e8cfe286547" },
	};
};


struct sha256: pal::crypto::sha256_hmac
{
	static inline std::unordered_map<std::string, std::string> hmac =
	{
		{ empty, "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad" },
		{ lazy_dog, "fb011e6154a19b9a4c767373c305275a5a69e8b68b0b4c9200c383dced19a416" },
		{ lazy_cog, "06c9344e6e96903114656d2391fbc36af735bfe5078592f9f9c2af1581e0682c" },
		{ lazy_dog + lazy_cog, "5a3b14a310149fcc216b4c665674edf0ecc625c7b491c91782617c0359bb7539" },
	};

	static inline std::unordered_map<std::string, std::string> hmac_with_key =
	{
		{ empty, "5d5d139563c95b5967b9bd9a8c9b233a9dedb45072794cd232dc1b74832607d0" },
		{ lazy_dog, "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8" },
		{ lazy_cog, "3f7d9044432ff5c2a390eea7dbb3fcbdbb7b51bb0089fa7354d135500e0bca36" },
		{ lazy_dog + lazy_cog, "da9a338b329a975ba651ecb3286de8dd96c616d6df8b477738e822e3bc889915" },
	};
};


struct sha384: pal::crypto::sha384_hmac
{
	static inline std::unordered_map<std::string, std::string> hmac =
	{
		{ empty, "6c1f2ee938fad2e24bd91298474382ca218c75db3d83e114b3d4367776d14d3551289e75e8209cd4b792302840234adc" },
		{ lazy_dog, "0a3d8f99afb726f97d32cc513f3a5ad51246984fd3e916cefb82fc7967ee42eae547cd88aefd84493d2585e55906e1b0" },
		{ lazy_cog, "2238f8408bc68134d559b615879a029e409e60038421ff34bd40c8e4ee34ea1e152a6fa401c5f3336d66488e1c253e56" },
		{ lazy_dog + lazy_cog, "3c83611d0b4cc764d37e83329981a89072e5d028957d9ae2a4c0945c773ed724fdc1dca2b3466798f1b4a5481113ac09" },
	};

	static inline std::unordered_map<std::string, std::string> hmac_with_key =
	{
		{ empty, "99f44bb4e73c9d0ef26533596c8d8a32a5f8c10a9b997d30d89a7e35ba1ccf200b985f72431202b891fe350da410e43f" },
		{ lazy_dog, "d7f4727e2c0b39ae0f1e40cc96f60242d5b7801841cea6fc592c5d3e1ae50700582a96cf35e1e554995fe4e03381c237" },
		{ lazy_cog, "c550bf5a491af63f266daa271c2a449323d5adbc405080cbe437190ab60b49b63bd436c159259484331a40540bb0787b" },
		{ lazy_dog + lazy_cog, "47b406402e9b10b32a4d87809bc19c5d381c8dc67514d44e688557bb09cc6c6efcf0e8e4f27eea2403754015f81af0b9" },
	};
};


struct sha512: pal::crypto::sha512_hmac
{
	static inline std::unordered_map<std::string, std::string> hmac =
	{
		{ empty, "b936cee86c9f87aa5d3c6f2e84cb5a4239a5fe50480a6ec66b70ab5b1f4ac6730c6c515421b327ec1d69402e53dfb49ad7381eb067b338fd7b0cb22247225d47" },
		{ lazy_dog, "1de78322e11d7f8f1035c12740f2b902353f6f4ac4233ae455baccdf9f37791566e790d5c7682aad5d3ceca2feff4d3f3fdfd9a140c82a66324e9442b8af71b6" },
		{ lazy_cog, "8f8f4c709a00fd1b7b4873cc2b46f58d86aff52db18dbde9c3d3e8dbe9b4cfcb8bc4efbb8c07c4d1a14b3c33aa3577a987568df2ebd7357445eb570500fed3d6" },
		{ lazy_dog + lazy_cog, "a5769514a2ce9ce792e5040b8adfafde0dbcfc15545492affe2283bcf78fc76e356fba8c5ac23bb413da4f7a705237f66f72c0916e879851ae2dd01a5be656b1" },
	};

	static inline std::unordered_map<std::string, std::string> hmac_with_key =
	{
		{ empty, "84fa5aa0279bbc473267d05a53ea03310a987cecc4c1535ff29b6d76b8f1444a728df3aadb89d4a9a6709e1998f373566e8f824a8ca93b1821f0b69bc2a2f65e" },
		{ lazy_dog, "b42af09057bac1e2d41708e48a902e09b5ff7f12ab428a4fe86653c73dd248fb82f948a549f7b791a5b41915ee4d1ec3935357e4e2317250d0372afa2ebeeb3a" },
		{ lazy_cog, "f3e0fd665455729c1f1da82f7f72eb41d3a6b886f523a57f4c2e2bb1f081cc394c824de9371a1751d52ac496128efca5e6ac61a8536091eeb093c4f89ad9c5d6" },
		{ lazy_dog + lazy_cog, "ae06454efb6e7ae7dd3559c1e4f86d7e054717adec2ae1ae60d31927a6ee95d024f51da2999e8ec2277a2447a1f1c404a73025f3c0fd60d3058f00164f1314d7" },
	};
};


TEMPLATE_TEST_CASE("crypto/hmac", "",
	md5,
	sha1,
	sha256,
	sha384,
	sha512)
{
	using pal_test::to_hex;

	SECTION("no key")
	{
		TestType h;
		auto r = to_hex(h.update(std::span{lazy_dog}).finish());
		CHECK(r == TestType::hmac[lazy_dog]);
	}

	SECTION("with key")
	{
		TestType h{std::span{key}};
		auto r = to_hex(h.update(std::span{lazy_dog}).finish());
		CHECK(r == TestType::hmac_with_key[lazy_dog]);
	}

	SECTION("copy ctor")
	{
		TestType h1;
		h1.update(std::span{lazy_dog});
		auto h2 = h1;

		auto r1 = to_hex(h1.update(std::span{lazy_cog}).finish());
		auto r2 = to_hex(h2.update(std::span{lazy_cog}).finish());
		CHECK(r1 == r2);
		CHECK(r1 == TestType::hmac[lazy_dog + lazy_cog]);
	}

	SECTION("copy ctor with key")
	{
		TestType h1{std::span(key)};
		h1.update(std::span{lazy_dog});
		auto h2 = h1;

		auto r1 = to_hex(h1.update(std::span{lazy_cog}).finish());
		auto r2 = to_hex(h2.update(std::span{lazy_cog}).finish());
		CHECK(r1 == r2);
		CHECK(r1 == TestType::hmac_with_key[lazy_dog + lazy_cog]);
	}

	SECTION("copy assign")
	{
		TestType h1, h2;
		h1.update(std::span{lazy_dog});
		h2 = h1;

		auto r1 = to_hex(h1.update(std::span{lazy_cog}).finish());
		auto r2 = to_hex(h2.update(std::span{lazy_cog}).finish());
		CHECK(r1 == r2);
		CHECK(r1 == TestType::hmac[lazy_dog + lazy_cog]);
	}

	SECTION("copy assign with key")
	{
		TestType h1{std::span{key}}, h2;
		h1.update(std::span{lazy_dog});
		h2 = h1;

		auto r1 = to_hex(h1.update(std::span{lazy_cog}).finish());
		auto r2 = to_hex(h2.update(std::span{lazy_cog}).finish());
		CHECK(r1 == r2);
		CHECK(r1 == TestType::hmac_with_key[lazy_dog + lazy_cog]);
	}

	SECTION("move ctor")
	{
		TestType h1;
		h1.update(std::span{lazy_dog});
		auto h2{std::move(h1)};
		auto r = to_hex(h2.update(std::span{lazy_cog}).finish());
		CHECK(r == TestType::hmac[lazy_dog + lazy_cog]);
	}

	SECTION("move ctor with key")
	{
		TestType h1{std::span{key}};
		h1.update(std::span{lazy_dog});
		auto h2{std::move(h1)};
		auto r = to_hex(h2.update(std::span{lazy_cog}).finish());
		CHECK(r == TestType::hmac_with_key[lazy_dog + lazy_cog]);
	}

	SECTION("move assign")
	{
		TestType h1, h2;
		h1.update(std::span{lazy_dog});
		h2 = std::move(h1);
		auto r = to_hex(h2.update(std::span{lazy_cog}).finish());
		CHECK(r == TestType::hmac[lazy_dog + lazy_cog]);
	}

	SECTION("move assign with key")
	{
		TestType h1{std::span{key}}, h2;
		h1.update(std::span{lazy_dog});
		h2 = std::move(h1);
		auto r = to_hex(h2.update(std::span{lazy_cog}).finish());
		CHECK(r == TestType::hmac_with_key[lazy_dog + lazy_cog]);
	}

	SECTION("no update")
	{
		TestType h;
		auto r = to_hex(h.finish());
		CHECK(r == TestType::hmac[empty]);
	}

	SECTION("no update with key")
	{
		TestType h{std::span{key}};
		auto r = to_hex(h.finish());
		CHECK(r == TestType::hmac_with_key[empty]);
	}

	SECTION("finish")
	{
		TestType h;
		uint8_t data[TestType::digest_size];
		*reinterpret_cast<typename TestType::result_type *>(data) = h.finish();
		CHECK(to_hex(data) == TestType::hmac[empty]);
	}

	SECTION("finish with key")
	{
		TestType h{std::span{key}};
		uint8_t data[TestType::digest_size];
		*reinterpret_cast<typename TestType::result_type *>(data) = h.finish();
		CHECK(to_hex(data) == TestType::hmac_with_key[empty]);
	}

	SECTION("reuse object")
	{
		TestType h;
		auto r = to_hex(h.update(std::span{lazy_dog}).finish());
		CHECK(r == TestType::hmac[lazy_dog]);
		r = to_hex(h.update(std::span{lazy_cog}).finish());
		CHECK(r == TestType::hmac[lazy_cog]);
	}

	SECTION("reuse object with key")
	{
		TestType h{std::span{key}};
		auto r = to_hex(h.update(std::span{lazy_dog}).finish());
		CHECK(r == TestType::hmac_with_key[lazy_dog]);
		r = to_hex(h.update(std::span{lazy_cog}).finish());
		CHECK(r == TestType::hmac_with_key[lazy_cog]);
	}

	SECTION("multiple updates")
	{
		TestType h;
		auto r = to_hex(h
			.update(std::span{lazy_dog})
			.update(std::span{lazy_cog})
			.finish()
		);
		CHECK(r == TestType::hmac[lazy_dog + lazy_cog]);
	}

	SECTION("multiple updates with key")
	{
		TestType h{std::span{key}};
		auto r = to_hex(h
			.update(std::span{lazy_dog})
			.update(std::span{lazy_cog})
			.finish()
		);
		CHECK(r == TestType::hmac_with_key[lazy_dog + lazy_cog]);
	}

	SECTION("multiple spans")
	{
		TestType h;
		std::array spans =
		{
			std::span{lazy_dog},
			std::span{lazy_cog},
		};
		auto r = to_hex(h.update(spans).finish());
		CHECK(r == TestType::hmac[lazy_dog + lazy_cog]);
	}

	SECTION("multiple spans with key")
	{
		TestType h{std::span{key}};
		std::array spans =
		{
			std::span{lazy_dog},
			std::span{lazy_cog},
		};
		auto r = to_hex(h.update(spans).finish());
		CHECK(r == TestType::hmac_with_key[lazy_dog + lazy_cog]);
	}

	SECTION("one_shot")
	{
		auto r = to_hex(TestType::one_shot(std::span{lazy_dog}));
		CHECK(r == TestType::hmac[lazy_dog]);
	}

	SECTION("one_shot with key")
	{
		auto r = to_hex(
			TestType::one_shot(
				std::span{key},
				std::span{lazy_dog}
			)
		);
		CHECK(r == TestType::hmac_with_key[lazy_dog]);
	}

	SECTION("one_shot: multiple spans")
	{
		std::array spans =
		{
			std::span{lazy_dog},
			std::span{lazy_cog},
		};
		auto r = to_hex(TestType::one_shot(spans));
		CHECK(r == TestType::hmac[lazy_dog + lazy_cog]);
	}

	SECTION("one_shot: multiple spans with key")
	{
		std::array spans =
		{
			std::span{lazy_dog},
			std::span{lazy_cog},
		};
		auto r = to_hex(TestType::one_shot(std::span{key}, spans));
		CHECK(r == TestType::hmac_with_key[lazy_dog + lazy_cog]);
	}
}


} // namespace
