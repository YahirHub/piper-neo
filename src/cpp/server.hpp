#ifndef PIPER_SERVER_H_
#define PIPER_SERVER_H_

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "piper.hpp"

namespace piper_server {

struct ModelInfo {
  std::string name;
  std::filesystem::path modelPath;
  std::filesystem::path configPath;
  bool hasConfig = false;
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
  std::size_t maxConcurrentJobs = 2;
  std::size_t maxModelReplicas = 2;
  std::string apiToken;
};

std::vector<ModelInfo> scanModels(const std::filesystem::path &modelsDir);
std::optional<ModelInfo> findFirstUsableModel(const std::filesystem::path &modelsDir);

void runServer(piper::PiperConfig &piperConfig, piper::Voice &voice,
               const ServerOptions &options);

} // namespace piper_server

#endif // PIPER_SERVER_H_
