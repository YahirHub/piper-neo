#ifndef PIPER_SERVER_H_
#define PIPER_SERVER_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "piper.hpp"

namespace piper_server {

struct ModelInfo {
  std::string name;
  std::string format = "onnx";
  std::filesystem::path modelPath;
  std::filesystem::path configPath;
  bool hasConfig = false;
  bool isNeo = false;
};

struct ResourcePolicy {
  std::string profile = "auto";
  bool autoConfigured = true;
  unsigned int hardwareThreads = 1;
  std::uint64_t memoryBytes = 0;
  std::size_t cpuThreadsPerWorker = 1;
  std::size_t maxConcurrentJobs = 1;
  std::size_t chunkWorkers = 1;
  std::size_t maxModelReplicas = 1;
  std::size_t queueSize = 16;
  std::size_t queueTimeoutSeconds = 60;
  std::size_t maxTempBytes = 0;
};

struct ServerOptions {
  std::string host = "127.0.0.1";
  int port = 8080;
  std::filesystem::path modelsDir = "models";
  std::filesystem::path outputDir = "outputs";
  std::filesystem::path activeModelPath;
  std::filesystem::path activeModelConfigPath;
  std::optional<piper::SpeakerId> defaultSpeakerId;
  bool useCuda = false;
  std::optional<int> cpuThreads;
  std::size_t maxInputBytes = 10 * 1024 * 1024;
  std::size_t maxTextChunkBytes = 4096;
  std::size_t maxConcurrentJobs = 0;
  std::size_t maxModelReplicas = 0;
  std::size_t chunkWorkers = 0;
  std::size_t queueSize = 0;
  std::size_t queueTimeoutSeconds = 60;
  std::string cpuProfile = "auto";
  unsigned int detectedHardwareThreads = 0;
  std::uint64_t detectedMemoryBytes = 0;
  ResourcePolicy resourcePolicy;
  std::string apiToken;
  std::size_t maxTempBytes = 0;
  std::size_t outputRetentionSeconds = 3600;
  std::size_t modelsRefreshSeconds = 30;
};

std::vector<ModelInfo> scanModels(const std::filesystem::path &modelsDir);
std::optional<ModelInfo> findFirstUsableModel(const std::filesystem::path &modelsDir);

void runServer(piper::PiperConfig &piperConfig, piper::Voice &voice,
               ServerOptions options);

} // namespace piper_server

#endif // PIPER_SERVER_H_
