#include "neo_model.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef PIPER_NEO_HAS_ZSTD
#include <zstd.h>
#endif

namespace piper_neo {
namespace {

constexpr std::array<char, 8> MAGIC{{'P', 'I', 'P', 'E', 'R', 'N', 'E', 'O'}};
constexpr std::uint32_t FORMAT_VERSION = 1;
constexpr std::uint32_t COMPRESSION_NONE = 0;
constexpr std::uint32_t COMPRESSION_ZSTD = 1;

struct SectionEntry {
  std::string name;
  std::string contentType;
  std::uint32_t compression = COMPRESSION_NONE;
  std::uint64_t uncompressedSize = 0;
  std::uint64_t storedSize = 0;
  std::uint64_t offset = 0;
};

struct ParsedPackage {
  std::filesystem::path path;
  std::uint32_t version = 1;
  std::vector<SectionEntry> sections;
};

std::string lowerCopy(std::string value) {
  for (auto &c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

std::uint32_t readU32(std::istream &in) {
  unsigned char b[4]{};
  in.read(reinterpret_cast<char *>(b), 4);
  if (!in.good()) {
    throw std::runtime_error("invalid_neo");
  }
  return static_cast<std::uint32_t>(b[0]) |
         (static_cast<std::uint32_t>(b[1]) << 8) |
         (static_cast<std::uint32_t>(b[2]) << 16) |
         (static_cast<std::uint32_t>(b[3]) << 24);
}

std::uint64_t readU64(std::istream &in) {
  unsigned char b[8]{};
  in.read(reinterpret_cast<char *>(b), 8);
  if (!in.good()) {
    throw std::runtime_error("invalid_neo");
  }
  std::uint64_t value = 0;
  for (int i = 7; i >= 0; --i) {
    value = (value << 8) | b[i];
  }
  return value;
}

void writeU32(std::ostream &out, std::uint32_t value) {
  unsigned char b[4]{static_cast<unsigned char>(value & 0xff),
                     static_cast<unsigned char>((value >> 8) & 0xff),
                     static_cast<unsigned char>((value >> 16) & 0xff),
                     static_cast<unsigned char>((value >> 24) & 0xff)};
  out.write(reinterpret_cast<const char *>(b), 4);
}

void writeU64(std::ostream &out, std::uint64_t value) {
  unsigned char b[8]{};
  for (int i = 0; i < 8; ++i) {
    b[i] = static_cast<unsigned char>((value >> (8 * i)) & 0xff);
  }
  out.write(reinterpret_cast<const char *>(b), 8);
}

std::string readString(std::istream &in) {
  auto size = readU32(in);
  if (size > 64 * 1024 * 1024) {
    throw std::runtime_error("invalid_neo");
  }
  std::string value(size, '\0');
  if (size > 0) {
    in.read(value.data(), static_cast<std::streamsize>(size));
    if (!in.good()) {
      throw std::runtime_error("invalid_neo");
    }
  }
  return value;
}

void writeString(std::ostream &out, const std::string &value) {
  writeU32(out, static_cast<std::uint32_t>(value.size()));
  out.write(value.data(), static_cast<std::streamsize>(value.size()));
}

std::vector<char> readFileBytes(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.good()) {
    throw std::runtime_error("file_not_found");
  }
  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  if (size < 0) {
    throw std::runtime_error("file_read_error");
  }
  file.seekg(0, std::ios::beg);
  std::vector<char> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  return bytes;
}

void writeFileBytes(const std::filesystem::path &path, const std::vector<char> &bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  if (!file.good()) {
    throw std::runtime_error("file_write_error");
  }
  if (!bytes.empty()) {
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
}

std::string fnv1aHex(const std::string &input) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (unsigned char c : input) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << std::setw(16) << std::setfill('0') << hash;
  return out.str();
}

std::string contentTypeForImagePath(const std::filesystem::path &path) {
  const auto ext = lowerCopy(path.extension().string());
  if (ext == ".jpg" || ext == ".jpeg") {
    return "image/jpeg";
  }
  if (ext == ".png") {
    return "image/png";
  }
  if (ext == ".webp") {
    return "image/webp";
  }
  return "application/octet-stream";
}

std::pair<std::string, std::vector<char>> decodeDataImage(const std::string &dataUri) {
  static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const std::string prefix = "data:image/";
  if (dataUri.rfind(prefix, 0) != 0) {
    throw std::runtime_error("invalid_image");
  }
  const auto comma = dataUri.find(',');
  if (comma == std::string::npos) {
    throw std::runtime_error("invalid_image");
  }
  auto meta = lowerCopy(dataUri.substr(0, comma));
  if (meta.find(";base64") == std::string::npos) {
    throw std::runtime_error("invalid_image");
  }

  std::string contentType;
  if (meta.rfind("data:image/jpeg", 0) == 0 || meta.rfind("data:image/jpg", 0) == 0) {
    contentType = "image/jpeg";
  } else if (meta.rfind("data:image/png", 0) == 0) {
    contentType = "image/png";
  } else if (meta.rfind("data:image/webp", 0) == 0) {
    contentType = "image/webp";
  } else {
    throw std::runtime_error("invalid_image");
  }

  std::array<int, 256> table{};
  table.fill(-1);
  for (int i = 0; i < static_cast<int>(chars.size()); ++i) {
    table[static_cast<unsigned char>(chars[i])] = i;
  }

  std::vector<char> decoded;
  int val = 0;
  int valb = -8;
  for (std::size_t i = comma + 1; i < dataUri.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(dataUri[i]);
    if (std::isspace(c)) {
      continue;
    }
    if (c == '=') {
      break;
    }
    if (table[c] == -1) {
      throw std::runtime_error("invalid_image");
    }
    val = (val << 6) + table[c];
    valb += 6;
    if (valb >= 0) {
      decoded.push_back(static_cast<char>((val >> valb) & 0xff));
      valb -= 8;
    }
  }
  return {contentType, decoded};
}

std::vector<char> compressZstd(const std::vector<char> &input, int level) {
#ifdef PIPER_NEO_HAS_ZSTD
  if (input.empty()) {
    return {};
  }
  if (level <= 0) {
    level = 10;
  }
  const auto bound = ZSTD_compressBound(input.size());
  std::vector<char> output(bound);
  const auto size = ZSTD_compress(output.data(), output.size(), input.data(), input.size(), level);
  if (ZSTD_isError(size)) {
    throw std::runtime_error(std::string("zstd_compress_error: ") + ZSTD_getErrorName(size));
  }
  output.resize(size);
  return output;
#else
  (void)input;
  (void)level;
  throw std::runtime_error("zstd_not_available");
#endif
}

std::vector<char> decompressZstd(const std::vector<char> &input, std::uint64_t expectedSize) {
#ifdef PIPER_NEO_HAS_ZSTD
  std::vector<char> output(static_cast<std::size_t>(expectedSize));
  const auto size = ZSTD_decompress(output.data(), output.size(), input.data(), input.size());
  if (ZSTD_isError(size)) {
    throw std::runtime_error(std::string("zstd_decompress_error: ") + ZSTD_getErrorName(size));
  }
  if (size != expectedSize) {
    throw std::runtime_error("invalid_neo_size");
  }
  return output;
#else
  (void)input;
  (void)expectedSize;
  throw std::runtime_error("zstd_not_available");
#endif
}

std::vector<char> maybeDecompress(const std::vector<char> &stored, const SectionEntry &entry) {
  if (entry.compression == COMPRESSION_NONE) {
    if (stored.size() != entry.uncompressedSize) {
      throw std::runtime_error("invalid_neo_size");
    }
    return stored;
  }
  if (entry.compression == COMPRESSION_ZSTD) {
    return decompressZstd(stored, entry.uncompressedSize);
  }
  throw std::runtime_error("unsupported_neo_compression");
}

std::vector<char> readSectionBytes(const ParsedPackage &package, const SectionEntry &entry) {
  std::ifstream file(package.path, std::ios::binary);
  if (!file.good()) {
    throw std::runtime_error("neo_not_found");
  }
  file.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
  std::vector<char> stored(static_cast<std::size_t>(entry.storedSize));
  if (!stored.empty()) {
    file.read(stored.data(), static_cast<std::streamsize>(stored.size()));
    if (!file.good()) {
      throw std::runtime_error("invalid_neo");
    }
  }
  return maybeDecompress(stored, entry);
}

const SectionEntry *findSection(const ParsedPackage &package, const std::string &name) {
  for (const auto &entry : package.sections) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

ParsedPackage parsePackage(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.good()) {
    throw std::runtime_error("neo_not_found");
  }

  std::array<char, 8> magic{};
  file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  if (magic != MAGIC) {
    throw std::runtime_error("invalid_neo_magic");
  }

  ParsedPackage package;
  package.path = path;
  package.version = readU32(file);
  if (package.version != FORMAT_VERSION) {
    throw std::runtime_error("unsupported_neo_version");
  }

  const auto sectionCount = readU32(file);
  if (sectionCount == 0 || sectionCount > 128) {
    throw std::runtime_error("invalid_neo");
  }

  package.sections.reserve(sectionCount);
  for (std::uint32_t i = 0; i < sectionCount; ++i) {
    SectionEntry entry;
    entry.name = readString(file);
    entry.contentType = readString(file);
    entry.compression = readU32(file);
    entry.uncompressedSize = readU64(file);
    entry.storedSize = readU64(file);
    entry.offset = readU64(file);
    if (entry.name.empty()) {
      throw std::runtime_error("invalid_neo");
    }
    package.sections.push_back(entry);
  }

  return package;
}

void addSection(std::vector<SectionEntry> &entries, std::vector<std::vector<char>> &payloads,
                const std::string &name, const std::string &contentType,
                const std::vector<char> &raw, bool useZstd, int compressionLevel) {
  SectionEntry entry;
  entry.name = name;
  entry.contentType = contentType;
  entry.uncompressedSize = raw.size();
  if (useZstd) {
    entry.compression = COMPRESSION_ZSTD;
    payloads.push_back(compressZstd(raw, compressionLevel));
  } else {
    entry.compression = COMPRESSION_NONE;
    payloads.push_back(raw);
  }
  entry.storedSize = payloads.back().size();
  entries.push_back(entry);
}

std::string compressionName(std::uint32_t compression) {
  if (compression == COMPRESSION_NONE) {
    return "none";
  }
  if (compression == COMPRESSION_ZSTD) {
    return "zstd";
  }
  return "unknown";
}

} // namespace

bool isNeoFile(const std::filesystem::path &path) {
  return lowerCopy(path.extension().string()) == ".neo";
}

NeoPackageInfo inspectPackage(const std::filesystem::path &neoPath) {
  auto package = parsePackage(neoPath);
  auto metadataEntry = findSection(package, "metadata.json");
  auto modelEntry = findSection(package, "model.onnx");
  if (metadataEntry == nullptr || modelEntry == nullptr) {
    throw std::runtime_error("invalid_neo");
  }

  const auto metadataBytes = readSectionBytes(package, *metadataEntry);
  const std::string metadataText(metadataBytes.begin(), metadataBytes.end());

  NeoPackageInfo info;
  info.path = neoPath;
  info.version = package.version;
  info.metadata = nlohmann::json::parse(metadataText);
  info.modelBytes = modelEntry->uncompressedSize;
  info.storedModelBytes = modelEntry->storedSize;
  info.modelCompression = compressionName(modelEntry->compression);

  if (auto image = findSection(package, "image")) {
    info.hasImage = true;
    info.imageContentType = image->contentType;
  }

  return info;
}

ExtractedNeoModel extractPackage(const std::filesystem::path &neoPath,
                                 const std::filesystem::path &cacheRoot) {
  auto package = parsePackage(neoPath);
  auto metadataEntry = findSection(package, "metadata.json");
  auto modelEntry = findSection(package, "model.onnx");
  if (metadataEntry == nullptr || modelEntry == nullptr) {
    throw std::runtime_error("invalid_neo");
  }

  auto info = inspectPackage(neoPath);
  auto cacheKey = fnv1aHex(std::filesystem::absolute(neoPath).string());
  auto outDir = cacheRoot / cacheKey;
  auto outModel = outDir / (neoPath.stem().string() + ".onnx");
  auto outConfig = std::filesystem::path(outModel.string() + ".json");

  std::filesystem::create_directories(outDir);
  if (!std::filesystem::exists(outModel) || !std::filesystem::exists(outConfig)) {
    auto modelBytes = readSectionBytes(package, *modelEntry);
    auto metadataBytes = readSectionBytes(package, *metadataEntry);
    writeFileBytes(outModel, modelBytes);
    writeFileBytes(outConfig, metadataBytes);
  }

  ExtractedNeoModel extracted;
  extracted.directory = outDir;
  extracted.modelPath = outModel;
  extracted.configPath = outConfig;
  extracted.info = std::move(info);
  return extracted;
}

std::pair<std::string, std::string> readImageSection(const std::filesystem::path &neoPath) {
  auto package = parsePackage(neoPath);
  auto imageEntry = findSection(package, "image");
  if (imageEntry == nullptr) {
    throw std::runtime_error("not_found");
  }
  auto imageBytes = readSectionBytes(package, *imageEntry);
  return {imageEntry->contentType, std::string(imageBytes.begin(), imageBytes.end())};
}

void writePackageFromOnnx(const std::filesystem::path &onnxPath,
                          const std::filesystem::path &configPath,
                          const std::filesystem::path &outputNeoPath,
                          const std::optional<std::filesystem::path> &imagePath,
                          int compressionLevel) {
  auto modelBytes = readFileBytes(onnxPath);
  auto configBytes = readFileBytes(configPath);
  nlohmann::json metadata = nlohmann::json::parse(std::string(configBytes.begin(), configBytes.end()));

  std::optional<std::vector<char>> imageBytes;
  std::string imageContentType;
  if (imagePath) {
    imageBytes = readFileBytes(*imagePath);
    imageContentType = contentTypeForImagePath(*imagePath);
  } else if (metadata.contains("modelcard") && metadata["modelcard"].is_object() &&
             metadata["modelcard"].contains("image") && metadata["modelcard"]["image"].is_string()) {
    auto decoded = decodeDataImage(metadata["modelcard"]["image"].get<std::string>());
    imageContentType = decoded.first;
    imageBytes = std::move(decoded.second);
  }

  if (metadata.contains("modelcard") && metadata["modelcard"].is_object()) {
    metadata["modelcard"].erase("image");
  }

  metadata["piper_neo"] = nlohmann::json{{"format", "piper-neo"},
                                           {"format_version", FORMAT_VERSION},
                                           {"model_section", "model.onnx"},
                                           {"compression", "zstd"}};

  const auto metadataText = metadata.dump(2);
  std::vector<char> metadataPayload(metadataText.begin(), metadataText.end());

  std::vector<SectionEntry> entries;
  std::vector<std::vector<char>> payloads;
  addSection(entries, payloads, "metadata.json", "application/json", metadataPayload, true, compressionLevel);
  addSection(entries, payloads, "model.onnx", "application/onnx", modelBytes, true, compressionLevel);
  if (imageBytes) {
    addSection(entries, payloads, "image", imageContentType, *imageBytes, true, compressionLevel);
  }

  std::uint64_t directorySize = 8 + 4 + 4;
  for (const auto &entry : entries) {
    directorySize += 4 + entry.name.size();
    directorySize += 4 + entry.contentType.size();
    directorySize += 4 + 8 + 8 + 8;
  }

  std::uint64_t offset = directorySize;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    entries[i].offset = offset;
    offset += entries[i].storedSize;
  }

  std::filesystem::create_directories(outputNeoPath.parent_path().empty() ? std::filesystem::path(".") : outputNeoPath.parent_path());
  std::ofstream out(outputNeoPath, std::ios::binary);
  if (!out.good()) {
    throw std::runtime_error("file_write_error");
  }

  out.write(MAGIC.data(), static_cast<std::streamsize>(MAGIC.size()));
  writeU32(out, FORMAT_VERSION);
  writeU32(out, static_cast<std::uint32_t>(entries.size()));
  for (const auto &entry : entries) {
    writeString(out, entry.name);
    writeString(out, entry.contentType);
    writeU32(out, entry.compression);
    writeU64(out, entry.uncompressedSize);
    writeU64(out, entry.storedSize);
    writeU64(out, entry.offset);
  }
  for (const auto &payload : payloads) {
    if (!payload.empty()) {
      out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }
  }
}

} // namespace piper_neo
