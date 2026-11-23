#include <cstdint>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include <fastgltf/core.hpp>
#include "gltf_path.hpp"

TEST_CASE("Parse glTF 2.1 files array", "[gltf-2.1]") {
	constexpr std::string_view json = R"({
		"asset": {"version": "2.1"},
		"files": [
			{"mimeType": "model/gltf-binary", "uri": "data:model/gltf-binary;base64,SGVsbG8gd29ybGQ="},
			{"mimeType": "application/octet-stream", "uri": "external.bin"},
			{"mimeType": "audio/ogg", "uri": "sound.ogg"}
		]
	})";
	auto jsonData = fastgltf::GltfDataBuffer::FromBytes(
			reinterpret_cast<const std::byte*>(json.data()), json.size());
	REQUIRE(jsonData.error() == fastgltf::Error::None);

	fastgltf::Parser parser;
	auto asset = parser.loadGltfJson(jsonData.get(), path);
	REQUIRE(asset.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::None);

	REQUIRE(fastgltf::hasBit(asset->availableCategories, fastgltf::Category::Files));
	REQUIRE(asset->files.size() == 3);

	// Data URIs are decoded during parsing, and the declared mimeType is stamped onto the source.
	REQUIRE(asset->files[0].mimeType == fastgltf::internal::mimeTypeGltfBinary);
	REQUIRE(std::holds_alternative<fastgltf::sources::Array>(asset->files[0].data));
	auto& array = std::get<fastgltf::sources::Array>(asset->files[0].data);
	REQUIRE(array.bytes.size() == 11); // "Hello world"
	REQUIRE(array.mimeType == fastgltf::MimeType::GltfBinary);

	// External URIs stay unresolved without Options::LoadExternalFiles.
	REQUIRE(std::holds_alternative<fastgltf::sources::URI>(asset->files[1].data));
	REQUIRE(std::get<fastgltf::sources::URI>(asset->files[1].data).uri->string() == "external.bin");

	// Arbitrary MIME types are preserved verbatim even though no MimeType enum value exists.
	REQUIRE(asset->files[2].mimeType == "audio/ogg");
	REQUIRE(std::holds_alternative<fastgltf::sources::URI>(asset->files[2].data));
	REQUIRE(std::get<fastgltf::sources::URI>(asset->files[2].data).mimeType == fastgltf::MimeType::None);
}

TEST_CASE("glTF 2.1 files entries require mimeType and uri", "[gltf-2.1]") {
	fastgltf::Parser parser;

	SECTION("Missing mimeType") {
		constexpr std::string_view json = R"({
			"asset": {"version": "2.1"},
			"files": [{"uri": "external.bin"}]
		})";
		auto jsonData = fastgltf::GltfDataBuffer::FromBytes(
				reinterpret_cast<const std::byte*>(json.data()), json.size());
		REQUIRE(jsonData.error() == fastgltf::Error::None);
		auto asset = parser.loadGltfJson(jsonData.get(), path);
		REQUIRE(asset.error() == fastgltf::Error::InvalidGltf);
	}

	SECTION("Missing uri") {
		constexpr std::string_view json = R"({
			"asset": {"version": "2.1"},
			"files": [{"mimeType": "application/octet-stream"}]
		})";
		auto jsonData = fastgltf::GltfDataBuffer::FromBytes(
				reinterpret_cast<const std::byte*>(json.data()), json.size());
		REQUIRE(jsonData.error() == fastgltf::Error::None);
		auto asset = parser.loadGltfJson(jsonData.get(), path);
		REQUIRE(asset.error() == fastgltf::Error::InvalidGltf);
	}
}

TEST_CASE("Parse glTF 2.1 external assets", "[gltf-2.1]") {
	auto fixtureDir = path / "gltf_2_1";
	fastgltf::GltfFileStream jsonData(fixtureDir / "external_asset.gltf");
	REQUIRE(jsonData.isOpen());

	fastgltf::Parser parser;
	auto asset = parser.loadGltfJson(jsonData, fixtureDir);
	REQUIRE(asset.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::None);

	REQUIRE(asset->assetInfo.version == fastgltf::Version { 2, 1 });
	REQUIRE(asset->assetInfo.minVersion == fastgltf::Version { 2, 1 });

	REQUIRE(fastgltf::hasBit(asset->availableCategories, fastgltf::Category::Files));
	REQUIRE(fastgltf::hasBit(asset->availableCategories, fastgltf::Category::ExternalAssets));

	REQUIRE(asset->files.size() == 1);
	REQUIRE(asset->files[0].mimeType == fastgltf::internal::mimeTypeGltfJson);

	REQUIRE(asset->externalAssets.size() == 1);
	REQUIRE(asset->externalAssets[0].fileIndex == 0);
	REQUIRE(asset->externalAssets[0].name == "sub_model");

	REQUIRE(asset->nodes.size() == 2);
	REQUIRE(asset->nodes[1].externalAssetIndex.has_value());
	REQUIRE(asset->nodes[1].externalAssetIndex.value() == 0);

	REQUIRE(asset->scenes.size() == 1);
}

TEST_CASE("Validate glTF 2.1 index bounds", "[gltf-2.1]") {
	auto fixtureDir = path / "gltf_2_1";
	fastgltf::GltfFileStream jsonData(fixtureDir / "external_asset.gltf");
	REQUIRE(jsonData.isOpen());

	fastgltf::Parser parser;
	auto asset = parser.loadGltfJson(jsonData, fixtureDir);
	REQUIRE(asset.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::None);

	SECTION("Invalid node externalAsset index") {
		asset->nodes[1].externalAssetIndex = 99;
		REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::InvalidGltf);
		asset->nodes[1].externalAssetIndex = 0;
		REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::None);
	}

	SECTION("Invalid externalAsset file index") {
		asset->externalAssets[0].fileIndex = 99;
		REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::InvalidGltf);
		asset->externalAssets[0].fileIndex = 0;
		REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::None);
	}

	SECTION("Empty file mimeType") {
		auto originalMimeType = asset->files[0].mimeType;
		asset->files[0].mimeType.clear();
		REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::InvalidGltf);
		asset->files[0].mimeType = originalMimeType;
		REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::None);
	}
}

TEST_CASE("Load glTF 2.1 external files", "[gltf-2.1]") {
	auto fixtureDir = path / "gltf_2_1";
	fastgltf::Parser parser;

	SECTION("Options::LoadExternalFiles loads the file bytes") {
		fastgltf::GltfFileStream jsonData(fixtureDir / "external_asset.gltf");
		REQUIRE(jsonData.isOpen());

		auto asset = parser.loadGltfJson(jsonData, fixtureDir, fastgltf::Options::LoadExternalFiles);
		REQUIRE(asset.error() == fastgltf::Error::None);
		REQUIRE(asset->files.size() == 1);

		REQUIRE(std::holds_alternative<fastgltf::sources::Array>(asset->files[0].data));
		auto& array = std::get<fastgltf::sources::Array>(asset->files[0].data);
		REQUIRE(array.bytes.size() == std::filesystem::file_size(fixtureDir / "sub_model.gltf"));
		REQUIRE(array.mimeType == fastgltf::MimeType::GltfJson);
	}

	SECTION("Missing external file returns an error") {
		fastgltf::GltfFileStream jsonData(fixtureDir / "missing_file.gltf");
		REQUIRE(jsonData.isOpen());

		auto asset = parser.loadGltfJson(jsonData, fixtureDir, fastgltf::Options::LoadExternalFiles);
		REQUIRE(asset.error() == fastgltf::Error::MissingExternalFile);
	}

	SECTION("Missing external file is fine without the option") {
		fastgltf::GltfFileStream jsonData(fixtureDir / "missing_file.gltf");
		REQUIRE(jsonData.isOpen());

		auto asset = parser.loadGltfJson(jsonData, fixtureDir);
		REQUIRE(asset.error() == fastgltf::Error::None);
		REQUIRE(std::holds_alternative<fastgltf::sources::URI>(asset->files[0].data));
	}
}

TEST_CASE("Rewrite glTF 2.1 asset", "[gltf-2.1]") {
	auto fixtureDir = path / "gltf_2_1";
	fastgltf::GltfFileStream jsonData(fixtureDir / "external_asset.gltf");
	REQUIRE(jsonData.isOpen());

	fastgltf::Parser parser;
	auto asset = parser.loadGltfJson(jsonData, fixtureDir);
	REQUIRE(asset.error() == fastgltf::Error::None);

	fastgltf::Exporter exporter;
	auto expected = exporter.writeGltfJson(asset.get());
	REQUIRE(expected.error() == fastgltf::Error::None);

	auto exportedJsonData = fastgltf::GltfDataBuffer::FromBytes(
			reinterpret_cast<const std::byte*>(expected.get().output.data()), expected.get().output.size());
	REQUIRE(exportedJsonData.error() == fastgltf::Error::None);
	auto reparsed = parser.loadGltfJson(exportedJsonData.get(), fixtureDir);
	REQUIRE(reparsed.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(reparsed.get()) == fastgltf::Error::None);

	REQUIRE(reparsed->assetInfo.version == fastgltf::Version { 2, 1 });
	REQUIRE(reparsed->assetInfo.minVersion == fastgltf::Version { 2, 1 });

	REQUIRE(reparsed->files.size() == 1);
	REQUIRE(reparsed->files[0].mimeType == fastgltf::internal::mimeTypeGltfJson);
	REQUIRE(std::holds_alternative<fastgltf::sources::URI>(reparsed->files[0].data));
	REQUIRE(std::get<fastgltf::sources::URI>(reparsed->files[0].data).uri->string() == "sub_model.gltf");

	REQUIRE(reparsed->externalAssets.size() == 1);
	REQUIRE(reparsed->externalAssets[0].fileIndex == 0);
	REQUIRE(reparsed->externalAssets[0].name == "sub_model");

	REQUIRE(reparsed->nodes.size() == 2);
	REQUIRE(reparsed->nodes[1].externalAssetIndex.has_value());
	REQUIRE(reparsed->nodes[1].externalAssetIndex.value() == 0);

	REQUIRE(reparsed->scenes.size() == 1);
}

TEST_CASE("Multi-scene glTF 2.1 assets still parse", "[gltf-2.1]") {
	// Multiple scenes are deprecated with glTF 2.1, but importers are expected to keep
	// handling them exactly as they did for glTF 2.0.
	constexpr std::string_view json = R"({
		"asset": {"version": "2.1"},
		"nodes": [{"name": "A"}, {"name": "B"}],
		"scene": 0,
		"scenes": [{"nodes": [0]}, {"nodes": [1]}]
	})";
	auto jsonData = fastgltf::GltfDataBuffer::FromBytes(
			reinterpret_cast<const std::byte*>(json.data()), json.size());
	REQUIRE(jsonData.error() == fastgltf::Error::None);

	fastgltf::Parser parser;
	auto asset = parser.loadGltfJson(jsonData.get(), path);
	REQUIRE(asset.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(asset.get()) == fastgltf::Error::None);
	REQUIRE(asset->scenes.size() == 2);
	REQUIRE(asset->defaultScene.has_value());

	fastgltf::Exporter exporter;
	auto expected = exporter.writeGltfJson(asset.get());
	REQUIRE(expected.error() == fastgltf::Error::None);
}

TEST_CASE("Asset minVersion is checked", "[gltf-2.1]") {
	fastgltf::Parser parser;

	auto parseVersions = [&](std::string_view version, std::string_view minVersion) {
		std::string json = R"({"asset": {"version": ")";
		json += version;
		json += R"(", "minVersion": ")";
		json += minVersion;
		json += R"("}})";
		auto jsonData = fastgltf::GltfDataBuffer::FromBytes(
				reinterpret_cast<const std::byte*>(json.data()), json.size());
		REQUIRE(jsonData.error() == fastgltf::Error::None);
		return parser.loadGltfJson(jsonData.get(), path).error();
	};

	REQUIRE(parseVersions("2.0", "2.0") == fastgltf::Error::None);
	REQUIRE(parseVersions("2.1", "2.1") == fastgltf::Error::None);
	REQUIRE(parseVersions("2.2", "2.2") == fastgltf::Error::UnsupportedVersion);
	REQUIRE(parseVersions("2.0", "3.0") == fastgltf::Error::UnsupportedVersion);
	REQUIRE(parseVersions("2.0", "2.10") == fastgltf::Error::UnsupportedVersion);
	// Unparseable minVersion values are rejected.
	REQUIRE(parseVersions("2.0", "unknown") == fastgltf::Error::InvalidVersion);
}

TEST_CASE("Write glTF 2.1 asset with all external files", "[gltf-2.1]") {
	auto fixtureDir = path / "gltf_2_1";
	fastgltf::GltfFileStream jsonData(fixtureDir / "external_asset.gltf");
	REQUIRE(jsonData.isOpen());

	fastgltf::Parser parser;
	auto asset = parser.loadGltfJson(jsonData, fixtureDir, fastgltf::Options::LoadExternalFiles);
	REQUIRE(asset.error() == fastgltf::Error::None);

	// Destroy the directory to make sure that the FileExporter correctly creates directories.
	auto exportedFolder = path / "export_gltf_2_1";
	if (std::filesystem::is_directory(exportedFolder)) {
		std::error_code ec;
		std::filesystem::remove_all(exportedFolder, ec);
		REQUIRE(!ec);
	}

	fastgltf::FileExporter exporter;
	auto error = exporter.writeGltfJson(asset.get(), exportedFolder / "external_asset.gltf");
	REQUIRE(error == fastgltf::Error::None);
	// The file entry has no name, its mimeType maps to the .gltf extension.
	REQUIRE(std::filesystem::exists(exportedFolder / "file0.gltf"));

	fastgltf::GltfFileStream exportedJsonData(exportedFolder / "external_asset.gltf");
	REQUIRE(exportedJsonData.isOpen());
	auto reparsed = parser.loadGltfJson(exportedJsonData, exportedFolder, fastgltf::Options::LoadExternalFiles);
	REQUIRE(reparsed.error() == fastgltf::Error::None);
	REQUIRE(fastgltf::validate(reparsed.get()) == fastgltf::Error::None);
	REQUIRE(std::holds_alternative<fastgltf::sources::Array>(reparsed->files[0].data));
}

TEST_CASE("glTF MIME type strings", "[gltf-2.1]") {
	REQUIRE(fastgltf::internal::getMimeTypeString(fastgltf::MimeType::GltfJson) == fastgltf::internal::mimeTypeGltfJson);
	REQUIRE(fastgltf::internal::getMimeTypeString(fastgltf::MimeType::GltfBinary) == fastgltf::internal::mimeTypeGltfBinary);
}

TEST_CASE("glTF 2.1 category dependencies", "[gltf-2.1]") {
	auto fixtureDir = path / "gltf_2_1";
	fastgltf::Parser parser;

	SECTION("Category::Nodes pulls in external assets and files") {
		fastgltf::GltfFileStream jsonData(fixtureDir / "external_asset.gltf");
		REQUIRE(jsonData.isOpen());

		auto asset = parser.loadGltfJson(jsonData, fixtureDir, fastgltf::Options::None, fastgltf::Category::Nodes);
		REQUIRE(asset.error() == fastgltf::Error::None);
		REQUIRE(asset->externalAssets.size() == 1);
		REQUIRE(asset->files.size() == 1);
	}

	SECTION("Category::Buffers leaves them unparsed") {
		fastgltf::GltfFileStream jsonData(fixtureDir / "external_asset.gltf");
		REQUIRE(jsonData.isOpen());

		auto asset = parser.loadGltfJson(jsonData, fixtureDir, fastgltf::Options::None, fastgltf::Category::Buffers);
		REQUIRE(asset.error() == fastgltf::Error::None);
		REQUIRE(asset->externalAssets.empty());
		REQUIRE(asset->files.empty());
	}
}
