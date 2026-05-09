#ifndef PIPER_NEO_MODEL_H_
#define PIPER_NEO_MODEL_H_

#include <filesystem>
#include <optional>
#include <string>

#include "json.hpp"

namespace piper_neo {

struct NeoSectionInfo {
  std::string name;
  std::string contentType;
  std::string compression;
  std::uint64_t uncompressedSize = 0;
  std::uint64_t storedSize = 0;
};

struct NeoPackageInfo {
  std::filesystem::path path;
  std::uint32_t version = 1;
  nlohmann::json metadata = nlohmann::json::object();
  bool hasImage = false;
  std::string imageContentType;
  std::uint64_t modelBytes = 0;
  std::uint64_t storedModelBytes = 0;
  std::string modelCompression;
};

struct ExtractedNeoModel {
  std::filesystem::path directory;
  std::filesystem::path modelPath;
  std::filesystem::path configPath;
  NeoPackageInfo info;
};

bool isNeoFile(const std::filesystem::path &path);

NeoPackageInfo inspectPackage(const std::filesystem::path &neoPath);

ExtractedNeoModel extractPackage(const std::filesystem::path &neoPath,
                                 const std::filesystem::path &cacheRoot);

void writePackageFromOnnx(const std::filesystem::path &onnxPath,
                          const std::filesystem::path &configPath,
                          const std::filesystem::path &outputNeoPath,
                          const std::optional<std::filesystem::path> &imagePath,
                          int compressionLevel);

std::pair<std::string, std::string> readImageSection(const std::filesystem::path &neoPath);

} // namespace piper_neo

#endif // PIPER_NEO_MODEL_H_
