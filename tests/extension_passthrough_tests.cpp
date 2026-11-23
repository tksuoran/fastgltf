#include <map>
#include <string>
#include <tuple>

#include <catch2/catch_test_macros.hpp>

#include <fastgltf/core.hpp>
#include "gltf_path.hpp"

namespace {
	using ExtensionKey = std::tuple<fastgltf::Category, std::size_t, std::size_t>;

	struct CapturedExtensions {
		std::map<ExtensionKey, std::string> json;
	};

	void captureExtensions(std::string_view extensions, std::size_t objectIndex, std::size_t subObjectIndex,
			fastgltf::Category objectType, void* userPointer) {
		auto* captured = static_cast<CapturedExtensions*>(userPointer);
		captured->json.emplace(ExtensionKey { objectType, objectIndex, subObjectIndex }, std::string(extensions));
	}

	std::optional<std::string> writeExtensions(std::size_t objectIndex, std::size_t subObjectIndex,
			fastgltf::Category objectType, void* userPointer) {
		auto* captured = static_cast<CapturedExtensions*>(userPointer);
		auto it = captured->json.find(ExtensionKey { objectType, objectIndex, subObjectIndex });
		if (it == captured->json.end())
			return std::nullopt;
		// The callback returns the members only: strip the braces of the captured object
		// and the typed extensions which fastgltf writes itself.
		std::string members = it->second.substr(1, it->second.size() - 2);
		return members;
	}

	constexpr std::string_view passthroughJson = R"({
		"asset": {"version": "2.0"},
		"extensionsUsed": ["KHR_lights_punctual", "KHR_materials_variants", "VENDOR_ext"],
		"extensions": {
			"VENDOR_ext": {"root": true},
			"KHR_lights_punctual": {"lights": [{"type": "point"}]},
			"KHR_materials_variants": {"variants": [{"name": "a"}]}
		},
		"scene": 0,
		"scenes": [{"nodes": [0, 1], "extensions": {"VENDOR_ext": {"scene": 0}}}],
		"nodes": [
			{"name": "plain"},
			{"camera": 0, "mesh": 0, "extensions": {"VENDOR_ext": {"node": 1}, "KHR_lights_punctual": {"light": 0}}}
		],
		"cameras": [{"type": "perspective", "perspective": {"yfov": 1, "znear": 0.1}, "extensions": {"VENDOR_ext": {"camera": [1, 2]}}}],
		"materials": [{}, {"emissiveFactor": [1, 1, 1], "extensions": {"VENDOR_ext": {"material": "x"}}}],
		"meshes": [{
			"primitives": [
				{"attributes": {"POSITION": 0}},
				{"attributes": {"POSITION": 0}, "extensions": {
					"VENDOR_ext": {"primitive": 1},
					"KHR_materials_variants": {"mappings": [{"material": 1, "variants": [0]}]}
				}}
			],
			"extensions": {"VENDOR_ext": {"mesh": 0}}
		}],
		"accessors": [{"componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 1]}]
	})";
} // namespace

TEST_CASE("Extensions parse callback receives raw extension JSON", "[extensions]") {
	auto jsonData = fastgltf::GltfDataBuffer::FromBytes(
		reinterpret_cast<const std::byte*>(passthroughJson.data()), passthroughJson.size());
	REQUIRE(jsonData.error() == fastgltf::Error::None);

	CapturedExtensions captured;
	fastgltf::Parser parser(fastgltf::Extensions::KHR_lights_punctual | fastgltf::Extensions::KHR_materials_variants);
	parser.setUserPointer(&captured);
	parser.setExtensionsParseCallback(captureExtensions);

	auto asset = parser.loadGltfJson(jsonData.get(), path);
	REQUIRE(asset.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::None);

	using fastgltf::Category;
	using fastgltf::invalidSubObjectIndex;
	REQUIRE(captured.json.size() == 7);
	REQUIRE(captured.json.contains({ Category::Asset, 0, invalidSubObjectIndex }));
	REQUIRE(captured.json.at({ Category::Scenes, 0, invalidSubObjectIndex }) == R"({"VENDOR_ext": {"scene": 0}})");
	REQUIRE(captured.json.at({ Category::Nodes, 1, invalidSubObjectIndex }).starts_with(R"({"VENDOR_ext": {"node": 1})"));
	REQUIRE(captured.json.at({ Category::Cameras, 0, invalidSubObjectIndex }) == R"({"VENDOR_ext": {"camera": [1, 2]}})");
	REQUIRE(captured.json.at({ Category::Materials, 1, invalidSubObjectIndex }) == R"({"VENDOR_ext": {"material": "x"}})");
	REQUIRE(captured.json.at({ Category::Meshes, 0, invalidSubObjectIndex }) == R"({"VENDOR_ext": {"mesh": 0}})");
	REQUIRE(captured.json.contains({ Category::Meshes, 0, 1 }));

	// Typed extensions are still parsed after the object was passed to the callback.
	REQUIRE(asset->lights.size() == 1);
	REQUIRE(asset->materialVariants.size() == 1);
	REQUIRE(asset->nodes[1].lightIndex == 0U);
	REQUIRE(asset->nodes[1].cameraIndex == 0U);
	REQUIRE(asset->nodes[1].meshIndex == 0U);
	REQUIRE(asset->materials[1].emissiveFactor == fastgltf::math::nvec3(1));
	REQUIRE(asset->meshes[0].primitives.size() == 2);
	REQUIRE(asset->meshes[0].primitives[1].mappings.size() == 1);
	REQUIRE(asset->meshes[0].primitives[1].mappings[0].materialIndex == 1);
	REQUIRE(asset->scenes[0].nodeIndices.size() == 2);
}

TEST_CASE("Extensions write callback round-trips vendor extensions", "[extensions]") {
	auto jsonData = fastgltf::GltfDataBuffer::FromBytes(
		reinterpret_cast<const std::byte*>(passthroughJson.data()), passthroughJson.size());
	REQUIRE(jsonData.error() == fastgltf::Error::None);

	// Only keep the vendor extension: the typed ones are written by fastgltf itself.
	CapturedExtensions captured;
	fastgltf::Parser parser(fastgltf::Extensions::KHR_lights_punctual | fastgltf::Extensions::KHR_materials_variants);
	parser.setUserPointer(&captured);
	parser.setExtensionsParseCallback([](std::string_view extensions, std::size_t objectIndex,
			std::size_t subObjectIndex, fastgltf::Category objectType, void* userPointer) {
		const auto begin = extensions.find(R"("VENDOR_ext")");
		REQUIRE(begin != std::string_view::npos);
		const auto end = extensions.find('}', begin);
		const auto member = std::string(extensions.substr(begin, end + 1 - begin));
		static_cast<CapturedExtensions*>(userPointer)->json.emplace(
			ExtensionKey { objectType, objectIndex, subObjectIndex }, '{' + member + '}');
	});

	auto asset = parser.loadGltfJson(jsonData.get(), path);
	REQUIRE(asset.error() == fastgltf::Error::None);

	fastgltf::Exporter exporter;
	exporter.setUserPointer(&captured);
	exporter.setExtensionsWriteCallback(writeExtensions);
	auto exported = exporter.writeGltfJson(asset.get(), fastgltf::ExportOptions::ValidateAsset);
	REQUIRE(exported.error() == fastgltf::Error::None);

	auto exportedData = fastgltf::GltfDataBuffer::FromBytes(
		reinterpret_cast<const std::byte*>(exported->output.data()), exported->output.size());
	REQUIRE(exportedData.error() == fastgltf::Error::None);

	CapturedExtensions reparsed;
	parser.setUserPointer(&reparsed);
	parser.setExtensionsParseCallback(captureExtensions);
	auto reparsedAsset = parser.loadGltfJson(exportedData.get(), path);
	REQUIRE(reparsedAsset.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(reparsedAsset.get()) == fastgltf::Error::None);

	// The exporter always writes an extensions object for materials, which may be empty.
	std::erase_if(reparsed.json, [](const auto& entry) { return entry.second == "{}"; });
	REQUIRE(reparsed.json.size() == captured.json.size());
	for (const auto& [key, json] : captured.json) {
		INFO("category " << static_cast<std::uint32_t>(std::get<0>(key)) << " object " << std::get<1>(key));
		REQUIRE(reparsed.json.contains(key));
		const auto vendor = json.substr(1, json.size() - 2);
		// simdjson's string builder writes minified JSON, so compare without whitespace.
		auto strip = [](std::string s) { std::erase(s, ' '); return s; };
		REQUIRE(strip(reparsed.json.at(key)).find(strip(vendor)) != std::string::npos);
	}

	// The typed extensions survive next to the vendor ones.
	REQUIRE(reparsedAsset->lights.size() == 1);
	REQUIRE(reparsedAsset->nodes[1].lightIndex == 0U);
	REQUIRE(reparsedAsset->meshes[0].primitives[1].mappings.size() == 1);
}

TEST_CASE("Extensions parse callback does not change how sample assets parse", "[extensions]") {
	static constexpr auto allExtensions = ~static_cast<fastgltf::Extensions>(0);

	std::size_t testedAssets = 0, callbackInvocations = 0;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(sampleAssets / "Models")) {
		if (!entry.is_regular_file())
			continue;
		const auto& epath = entry.path();
		if (epath.extension() != ".gltf" && epath.extension() != ".glb")
			continue;
		INFO("Asset: " << epath.string());

		fastgltf::Parser plainParser(allExtensions);
		fastgltf::GltfFileStream plainData(epath);
		auto plain = plainParser.loadGltf(plainData, epath.parent_path());

		fastgltf::Parser callbackParser(allExtensions);
		callbackParser.setUserPointer(&callbackInvocations);
		callbackParser.setExtensionsParseCallback([](std::string_view extensions, std::size_t, std::size_t,
				fastgltf::Category, void* userPointer) {
			REQUIRE(extensions.starts_with('{'));
			REQUIRE(extensions.ends_with('}'));
			++*static_cast<std::size_t*>(userPointer);
		});
		fastgltf::GltfFileStream callbackData(epath);
		auto withCallback = callbackParser.loadGltf(callbackData, epath.parent_path());

		REQUIRE(plain.error() == withCallback.error());
		if (plain.error() != fastgltf::Error::None)
			continue;

		REQUIRE(plain->nodes.size() == withCallback->nodes.size());
		REQUIRE(plain->materials.size() == withCallback->materials.size());
		REQUIRE(plain->meshes.size() == withCallback->meshes.size());
		REQUIRE(plain->lights.size() == withCallback->lights.size());
		REQUIRE(plain->materialVariants == withCallback->materialVariants);
		for (std::size_t i = 0; i < plain->nodes.size(); ++i) {
			REQUIRE(plain->nodes[i].lightIndex == withCallback->nodes[i].lightIndex);
			REQUIRE(plain->nodes[i].instancingAttributes.size() == withCallback->nodes[i].instancingAttributes.size());
		}
		for (std::size_t i = 0; i < plain->materials.size(); ++i) {
			REQUIRE(plain->materials[i].emissiveStrength == withCallback->materials[i].emissiveStrength);
			REQUIRE(plain->materials[i].ior == withCallback->materials[i].ior);
			REQUIRE(static_cast<bool>(plain->materials[i].clearcoat) == static_cast<bool>(withCallback->materials[i].clearcoat));
		}
		for (std::size_t i = 0; i < plain->meshes.size(); ++i) {
			for (std::size_t p = 0; p < plain->meshes[i].primitives.size(); ++p) {
				REQUIRE(plain->meshes[i].primitives[p].mappings.size() == withCallback->meshes[i].primitives[p].mappings.size());
			}
		}
		++testedAssets;
	}
	REQUIRE(testedAssets > 0);
	REQUIRE(callbackInvocations > 0);
}
