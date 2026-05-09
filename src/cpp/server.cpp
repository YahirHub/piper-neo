#include "server.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cerrno>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <spdlog/spdlog.h>

#include "json.hpp"

using json = nlohmann::json;

namespace piper_server {
namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle INVALID_SOCKET_HANDLE = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;
#endif

void closeSocket(SocketHandle socketHandle) {
#ifdef _WIN32
  closesocket(socketHandle);
#else
  close(socketHandle);
#endif
}

std::string nowIso8601() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

std::string lowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string trimCopy(const std::string &value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }

  return value.substr(begin, end - begin);
}

bool isSafeFileName(const std::string &fileName) {
  if (fileName.empty() || fileName == "." || fileName == "..") {
    return false;
  }

  for (char c : fileName) {
    const auto value = static_cast<unsigned char>(c);
    if (!(std::isalnum(value) || c == '_' || c == '-' || c == '.')) {
      return false;
    }
  }

  return (fileName.find("..") == std::string::npos) &&
         (fileName.find('/') == std::string::npos) &&
         (fileName.find('\\') == std::string::npos);
}

std::string randomSuffix() {
  static std::mutex randomMutex;
  static std::mt19937_64 rng{std::random_device{}()};
  std::lock_guard<std::mutex> lock(randomMutex);
  std::uniform_int_distribution<unsigned long long> dist;
  std::ostringstream out;
  out << std::hex << dist(rng);
  return out.str().substr(0, 10);
}

std::string makeOutputFileName() {
  const auto now = std::chrono::system_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch())
                      .count();
  std::ostringstream name;
  name << "tts_" << ms << "_" << randomSuffix() << ".wav";
  return name.str();
}

json successResponse(const std::string &message, const json &data = json::object()) {
  json response;
  response["success"] = true;
  if (!message.empty()) {
    response["message"] = message;
  }
  response["data"] = data;
  return response;
}

json errorResponse(const std::string &error, const std::string &message) {
  json response;
  response["success"] = false;
  response["error"] = error;
  response["message"] = message;
  return response;
}

struct HttpRequest {
  std::string method;
  std::string path;
  std::string body;
  std::map<std::string, std::string> headers;
};

std::optional<std::string> getHeader(const HttpRequest &request,
                                     const std::string &headerName) {
  auto it = request.headers.find(lowerCopy(headerName));
  if (it == request.headers.end()) {
    return std::nullopt;
  }

  return it->second;
}

std::optional<std::string> extractBearerToken(const std::string &authorization) {
  const std::string prefix = "Bearer ";
  if (authorization.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }

  auto token = trimCopy(authorization.substr(prefix.size()));
  if (token.empty()) {
    return std::nullopt;
  }

  return token;
}

bool requestIsAuthorized(const HttpRequest &request, const ServerOptions &options) {
  if (options.apiToken.empty()) {
    return true;
  }

  if (auto authorization = getHeader(request, "authorization")) {
    if (auto bearer = extractBearerToken(*authorization)) {
      if (*bearer == options.apiToken) {
        return true;
      }
    }
  }

  if (auto token = getHeader(request, "x-api-token")) {
    if (trimCopy(*token) == options.apiToken) {
      return true;
    }
  }

  return false;
}

bool clientDisconnected(SocketHandle socketHandle) {
  fd_set readSet;
  FD_ZERO(&readSet);
  FD_SET(socketHandle, &readSet);

  timeval timeout{};
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;

#ifdef _WIN32
  int ready = select(0, &readSet, nullptr, nullptr, &timeout);
#else
  int ready = select(socketHandle + 1, &readSet, nullptr, nullptr, &timeout);
#endif

  if (ready <= 0 || !FD_ISSET(socketHandle, &readSet)) {
    return false;
  }

  char probe = 0;
#ifdef _WIN32
  int received = recv(socketHandle, &probe, 1, MSG_PEEK);
  if (received == 0) {
    return true;
  }
  if (received < 0) {
    int err = WSAGetLastError();
    return (err != WSAEWOULDBLOCK) && (err != WSAEINTR);
  }
#else
  ssize_t received = recv(socketHandle, &probe, 1, MSG_PEEK);
  if (received == 0) {
    return true;
  }
  if (received < 0) {
    return (errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR);
  }
#endif

  // The peer may have sent extra bytes on a keep-alive request. This server
  // closes every response, so extra data is ignored but does not mean closed.
  return false;
}

bool recvAppend(SocketHandle socketHandle, std::string &buffer) {
  char chunk[8192];
#ifdef _WIN32
  int received = recv(socketHandle, chunk, static_cast<int>(sizeof(chunk)), 0);
#else
  ssize_t received = recv(socketHandle, chunk, sizeof(chunk), 0);
#endif
  if (received <= 0) {
    return false;
  }

  buffer.append(chunk, static_cast<std::size_t>(received));
  return true;
}

std::optional<HttpRequest> readHttpRequest(SocketHandle socketHandle,
                                           std::size_t maxBodyBytes) {
  std::string buffer;
  std::size_t headerEnd = std::string::npos;

  while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
    if (!recvAppend(socketHandle, buffer)) {
      return std::nullopt;
    }

    if (buffer.size() > 64 * 1024) {
      throw std::runtime_error("HTTP headers too large");
    }
  }

  std::string headerBlock = buffer.substr(0, headerEnd);
  std::istringstream headerStream(headerBlock);
  std::string requestLine;
  std::getline(headerStream, requestLine);
  if (!requestLine.empty() && requestLine.back() == '\r') {
    requestLine.pop_back();
  }

  std::istringstream requestLineStream(requestLine);
  HttpRequest request;
  std::string version;
  requestLineStream >> request.method >> request.path >> version;
  if (request.method.empty() || request.path.empty()) {
    throw std::runtime_error("Invalid HTTP request line");
  }

  std::string headerLine;
  while (std::getline(headerStream, headerLine)) {
    if (!headerLine.empty() && headerLine.back() == '\r') {
      headerLine.pop_back();
    }

    auto colon = headerLine.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    auto key = lowerCopy(trimCopy(headerLine.substr(0, colon)));
    auto value = trimCopy(headerLine.substr(colon + 1));
    request.headers[key] = value;
  }

  std::size_t contentLength = 0;
  if (auto header = getHeader(request, "content-length")) {
    contentLength = static_cast<std::size_t>(std::stoull(*header));
  }

  if (contentLength > maxBodyBytes) {
    throw std::runtime_error("payload_too_large");
  }

  const std::size_t bodyStart = headerEnd + 4;
  while ((buffer.size() - bodyStart) < contentLength) {
    if (!recvAppend(socketHandle, buffer)) {
      throw std::runtime_error("Unexpected end of HTTP body");
    }

    if ((buffer.size() - bodyStart) > maxBodyBytes) {
      throw std::runtime_error("payload_too_large");
    }
  }

  request.body = buffer.substr(bodyStart, contentLength);
  return request;
}

void sendRaw(SocketHandle socketHandle, const std::string &data) {
  const char *ptr = data.data();
  std::size_t remaining = data.size();
  while (remaining > 0) {
#ifdef _WIN32
    int sent = send(socketHandle, ptr, static_cast<int>(remaining), 0);
#else
    ssize_t sent = send(socketHandle, ptr, remaining, 0);
#endif
    if (sent <= 0) {
      return;
    }
    ptr += sent;
    remaining -= static_cast<std::size_t>(sent);
  }
}

std::string reasonPhrase(int statusCode) {
  switch (statusCode) {
  case 200:
    return "OK";
  case 201:
    return "Created";
  case 400:
    return "Bad Request";
  case 401:
    return "Unauthorized";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 413:
    return "Payload Too Large";
  case 429:
    return "Too Many Requests";
  case 500:
    return "Internal Server Error";
  default:
    return "OK";
  }
}

void sendResponse(SocketHandle socketHandle, int statusCode,
                  const std::string &contentType, const std::string &body,
                  const std::map<std::string, std::string> &extraHeaders = {}) {
  std::ostringstream response;
  response << "HTTP/1.1 " << statusCode << " " << reasonPhrase(statusCode) << "\r\n";
  response << "Content-Type: " << contentType << "\r\n";
  response << "Content-Length: " << body.size() << "\r\n";
  response << "Connection: close\r\n";
  response << "Access-Control-Allow-Origin: *\r\n";
  response << "Access-Control-Allow-Headers: Content-Type, Authorization, X-API-Token\r\n";
  response << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
  for (const auto &[key, value] : extraHeaders) {
    response << key << ": " << value << "\r\n";
  }
  response << "\r\n";
  response << body;
  sendRaw(socketHandle, response.str());
}

void sendJson(SocketHandle socketHandle, int statusCode, const json &body) {
  sendResponse(socketHandle, statusCode, "application/json; charset=utf-8",
               body.dump(2));
}

std::optional<std::string> routeFileName(const std::string &path) {
  const std::string prefix = "/api/v1/files/";
  if (path.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }

  return path.substr(prefix.size());
}

json modelInfoToJson(const ModelInfo &modelInfo) {
  return json{{"name", modelInfo.name},
              {"model_file", modelInfo.modelPath.string()},
              {"config_file", modelInfo.configPath.string()},
              {"has_config", modelInfo.hasConfig}};
}


std::string modelKey(const std::filesystem::path &modelPath) {
  std::error_code ignored;
  auto absolutePath = std::filesystem::absolute(modelPath, ignored);
  if (ignored) {
    return modelPath.lexically_normal().string();
  }

  auto canonicalPath = std::filesystem::weakly_canonical(absolutePath, ignored);
  if (ignored) {
    return absolutePath.lexically_normal().string();
  }

  return canonicalPath.string();
}

std::optional<ModelInfo> findModelByName(const ServerOptions &options,
                                         const std::string &requestedModel) {
  std::string modelName = trimCopy(requestedModel);
  if (modelName.empty()) {
    modelName = options.activeModelPath.filename().string();
  }

  if (!isSafeFileName(modelName)) {
    throw std::runtime_error("invalid_model");
  }

  std::vector<std::string> candidates{modelName};
  if (std::filesystem::path(modelName).extension().empty()) {
    candidates.push_back(modelName + ".onnx");
  }

  for (const auto &candidate : candidates) {
    if (candidate == options.activeModelPath.filename().string()) {
      const auto configPath = options.activeModelConfigPath.empty()
                                  ? std::filesystem::path(options.activeModelPath.string() + ".json")
                                  : options.activeModelConfigPath;
      return ModelInfo{candidate, options.activeModelPath, configPath,
                       std::filesystem::exists(configPath)};
    }
  }

  const auto models = scanModels(options.modelsDir);
  for (const auto &candidate : candidates) {
    for (const auto &model : models) {
      if (model.name == candidate) {
        return model;
      }
    }
  }

  return std::nullopt;
}

struct JobLimiter {
  explicit JobLimiter(std::size_t maxJobs) : maxJobs(std::max<std::size_t>(1, maxJobs)) {}

  bool tryAcquire() {
    std::lock_guard<std::mutex> lock(mutex);
    if (activeJobs >= maxJobs) {
      return false;
    }

    ++activeJobs;
    return true;
  }

  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    if (activeJobs > 0) {
      --activeJobs;
    }
  }

  std::size_t active() const {
    std::lock_guard<std::mutex> lock(mutex);
    return activeJobs;
  }

private:
  const std::size_t maxJobs;
  mutable std::mutex mutex;
  std::size_t activeJobs = 0;
};

struct JobLease {
  JobLimiter *limiter = nullptr;
  bool owns = false;

  JobLease() = default;
  explicit JobLease(JobLimiter &jobLimiter) : limiter(&jobLimiter), owns(jobLimiter.tryAcquire()) {}
  JobLease(const JobLease &) = delete;
  JobLease &operator=(const JobLease &) = delete;

  JobLease(JobLease &&other) noexcept : limiter(other.limiter), owns(other.owns) {
    other.limiter = nullptr;
    other.owns = false;
  }

  JobLease &operator=(JobLease &&other) noexcept {
    if (this != &other) {
      reset();
      limiter = other.limiter;
      owns = other.owns;
      other.limiter = nullptr;
      other.owns = false;
    }
    return *this;
  }

  ~JobLease() { reset(); }

  explicit operator bool() const { return owns; }

  void reset() {
    if (owns && limiter != nullptr) {
      limiter->release();
    }
    owns = false;
    limiter = nullptr;
  }
};

struct VoiceSlot {
  std::unique_ptr<piper::Voice> ownedVoice;
  piper::Voice *voice = nullptr;
  bool inUse = false;
};

struct ModelRuntime {
  explicit ModelRuntime(ModelInfo modelInfo) : info(std::move(modelInfo)) {}

  ModelInfo info;
  std::mutex mutex;
  std::vector<std::unique_ptr<VoiceSlot>> slots;
  std::size_t loadingSlots = 0;
};

struct VoiceLease {
  std::shared_ptr<ModelRuntime> runtime;
  VoiceSlot *slot = nullptr;

  VoiceLease() = default;
  VoiceLease(std::shared_ptr<ModelRuntime> modelRuntime, VoiceSlot *voiceSlot)
      : runtime(std::move(modelRuntime)), slot(voiceSlot) {}
  VoiceLease(const VoiceLease &) = delete;
  VoiceLease &operator=(const VoiceLease &) = delete;

  VoiceLease(VoiceLease &&other) noexcept
      : runtime(std::move(other.runtime)), slot(other.slot) {
    other.slot = nullptr;
  }

  VoiceLease &operator=(VoiceLease &&other) noexcept {
    if (this != &other) {
      reset();
      runtime = std::move(other.runtime);
      slot = other.slot;
      other.slot = nullptr;
    }
    return *this;
  }

  ~VoiceLease() { reset(); }

  piper::Voice &get() const { return *slot->voice; }
  const ModelInfo &model() const { return runtime->info; }
  explicit operator bool() const { return slot != nullptr && slot->voice != nullptr; }

  void reset() {
    if (runtime && slot != nullptr) {
      std::lock_guard<std::mutex> lock(runtime->mutex);
      slot->inUse = false;
    }
    slot = nullptr;
    runtime.reset();
  }
};

class ModelCache {
public:
  ModelCache(piper::PiperConfig &piperConfig, piper::Voice &defaultVoice,
             const ServerOptions &options)
      : piperConfig(piperConfig), options(options),
        maxReplicas(std::max<std::size_t>(1, options.maxModelReplicas)) {
    const auto activeName = options.activeModelPath.filename().string();
    const auto activeConfig = options.activeModelConfigPath.empty()
                                  ? std::filesystem::path(options.activeModelPath.string() + ".json")
                                  : options.activeModelConfigPath;
    auto runtime = std::make_shared<ModelRuntime>(
        ModelInfo{activeName, options.activeModelPath, activeConfig,
                  std::filesystem::exists(activeConfig)});

    auto slot = std::make_unique<VoiceSlot>();
    slot->voice = &defaultVoice;
    runtime->slots.push_back(std::move(slot));

    cache[modelKey(options.activeModelPath)] = runtime;
  }

  VoiceLease checkout(const std::optional<std::string> &requestedModel) {
    auto info = resolve(requestedModel);
    auto runtime = runtimeFor(info);

    std::unique_lock<std::mutex> lock(runtime->mutex);
    for (const auto &slot : runtime->slots) {
      if (!slot->inUse) {
        slot->inUse = true;
        return VoiceLease(runtime, slot.get());
      }
    }

    if ((runtime->slots.size() + runtime->loadingSlots) >= maxReplicas) {
      throw std::runtime_error("model_busy");
    }

    ++runtime->loadingSlots;
    auto newVoice = std::make_unique<piper::Voice>();
    auto speakerId = options.defaultSpeakerId;
    lock.unlock();

    try {
      spdlog::info("Loading model replica: {}", info.modelPath.string());
      piper::loadVoice(piperConfig, info.modelPath.string(), info.configPath.string(),
                       *newVoice, speakerId, options.useCuda, options.cpuThreads);
    } catch (...) {
      lock.lock();
      if (runtime->loadingSlots > 0) {
        --runtime->loadingSlots;
      }
      lock.unlock();
      throw;
    }

    lock.lock();
    if (runtime->loadingSlots > 0) {
      --runtime->loadingSlots;
    }
    auto slot = std::make_unique<VoiceSlot>();
    slot->voice = newVoice.get();
    slot->ownedVoice = std::move(newVoice);
    slot->inUse = true;
    auto *slotPtr = slot.get();
    runtime->slots.push_back(std::move(slot));
    return VoiceLease(runtime, slotPtr);
  }

private:
  ModelInfo resolve(const std::optional<std::string> &requestedModel) {
    const auto modelName = requestedModel ? trimCopy(*requestedModel) : std::string();
    auto info = findModelByName(options, modelName);
    if (!info) {
      throw std::runtime_error("model_not_found");
    }

    if (!info->hasConfig) {
      throw std::runtime_error("model_config_missing");
    }

    return *info;
  }

  std::shared_ptr<ModelRuntime> runtimeFor(const ModelInfo &info) {
    const auto key = modelKey(info.modelPath);
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto it = cache.find(key);
    if (it != cache.end()) {
      return it->second;
    }

    auto runtime = std::make_shared<ModelRuntime>(info);
    cache[key] = runtime;
    return runtime;
  }

  piper::PiperConfig &piperConfig;
  const ServerOptions &options;
  const std::size_t maxReplicas;
  std::mutex cacheMutex;
  std::map<std::string, std::shared_ptr<ModelRuntime>> cache;
};

int errorStatusForException(const std::string &error) {
  if (error == "invalid_model") {
    return 400;
  }
  if (error == "model_not_found" || error == "model_config_missing") {
    return 404;
  }
  if (error == "model_busy" || error == "server_busy") {
    return 429;
  }
  return 500;
}

json modelErrorResponse(const std::string &error) {
  if (error == "invalid_model") {
    return errorResponse("invalid_request",
                         "El nombre del modelo es inválido. Usa solo el nombre del archivo .onnx dentro de models/.");
  }
  if (error == "model_not_found") {
    return errorResponse("model_not_found", "Modelo no encontrado en la carpeta models/.");
  }
  if (error == "model_config_missing") {
    return errorResponse("model_config_missing",
                         "El modelo existe, pero falta su archivo .onnx.json.");
  }
  if (error == "model_busy") {
    return errorResponse("server_busy",
                         "Todas las réplicas de ese modelo están ocupadas. Intenta nuevamente.");
  }
  if (error == "server_busy") {
    return errorResponse("server_busy",
                         "El servidor alcanzó el límite de síntesis simultáneas. Intenta nuevamente.");
  }
  return errorResponse("server_error", error);
}

void sendFile(SocketHandle socketHandle, const std::filesystem::path &filePath) {
  std::ifstream file(filePath, std::ios::binary);
  if (!file.good()) {
    sendJson(socketHandle, 404,
             errorResponse("not_found", "Archivo no encontrado."));
    return;
  }

  const auto fileSize = std::filesystem::file_size(filePath);
  std::ostringstream headers;
  headers << "HTTP/1.1 200 OK\r\n";
  headers << "Content-Type: audio/wav\r\n";
  headers << "Content-Length: " << fileSize << "\r\n";
  headers << "Connection: close\r\n";
  headers << "Access-Control-Allow-Origin: *\r\n";
  headers << "Content-Disposition: attachment; filename=\""
          << filePath.filename().string() << "\"\r\n";
  headers << "\r\n";
  sendRaw(socketHandle, headers.str());

  std::array<char, 64 * 1024> buffer{};
  while (file.good()) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = file.gcount();
    if (count > 0) {
      sendRaw(socketHandle, std::string(buffer.data(), static_cast<std::size_t>(count)));
    }
  }
}

void handleClient(SocketHandle clientSocket, piper::PiperConfig &piperConfig,
                  const ServerOptions &options, ModelCache &modelCache,
                  JobLimiter &jobLimiter) {
  try {
    auto maybeRequest = readHttpRequest(clientSocket, options.maxInputBytes);
    if (!maybeRequest) {
      closeSocket(clientSocket);
      return;
    }

    const auto &request = *maybeRequest;

    if (request.method == "OPTIONS") {
      sendResponse(clientSocket, 200, "text/plain; charset=utf-8", "");
      closeSocket(clientSocket);
      return;
    }

    if (!requestIsAuthorized(request, options)) {
      sendJson(clientSocket, 401,
               errorResponse("invalid_token",
                             "Token inválido, ausente o expirado."));
      closeSocket(clientSocket);
      return;
    }

    if ((request.method == "GET") && (request.path == "/api/health")) {
      sendJson(clientSocket, 200,
               successResponse("Servidor Piper activo.",
                               json{{"status", "ok"},
                                    {"model_loaded", true},
                                    {"time", nowIso8601()}}));
      closeSocket(clientSocket);
      return;
    }

    if ((request.method == "GET") && (request.path == "/api/v1/status")) {
      sendJson(clientSocket, 200,
               successResponse("Estado obtenido correctamente.",
                               json{{"server", "piper"},
                                    {"model_loaded", true},
                                    {"active_model", options.activeModelPath.string()},
                                    {"models_dir", options.modelsDir.string()},
                                    {"output_dir", options.outputDir.string()},
                                    {"auth",
                                     json{{"enabled", !options.apiToken.empty()},
                                          {"header", "Authorization: Bearer <token>"}}},
                                    {"limits",
                                     json{{"max_input_bytes", options.maxInputBytes},
                                          {"max_text_chunk_bytes",
                                           options.maxTextChunkBytes},
                                          {"max_concurrent_jobs",
                                           options.maxConcurrentJobs},
                                          {"max_model_replicas",
                                           options.maxModelReplicas},
                                          {"active_jobs", jobLimiter.active()}}}}));
      closeSocket(clientSocket);
      return;
    }

    if ((request.method == "GET") && (request.path == "/api/v1/models")) {
      json models = json::array();
      for (const auto &model : scanModels(options.modelsDir)) {
        models.push_back(modelInfoToJson(model));
      }
      sendJson(clientSocket, 200,
               successResponse("Modelos listados correctamente.",
                               json{{"total", models.size()}, {"models", models}}));
      closeSocket(clientSocket);
      return;
    }

    if (auto fileName = routeFileName(request.path)) {
      if (request.method != "GET") {
        sendJson(clientSocket, 405,
                 errorResponse("method_not_allowed", "Método no permitido."));
        closeSocket(clientSocket);
        return;
      }

      if (!isSafeFileName(*fileName)) {
        sendJson(clientSocket, 400,
                 errorResponse("invalid_request", "Nombre de archivo inválido."));
        closeSocket(clientSocket);
        return;
      }

      auto filePath = options.outputDir / *fileName;
      sendFile(clientSocket, filePath);
      closeSocket(clientSocket);
      return;
    }

    if ((request.method == "POST") && (request.path == "/api/v1/tts")) {
      json input;
      try {
        input = json::parse(request.body);
      } catch (const std::exception &) {
        sendJson(clientSocket, 400,
                 errorResponse("invalid_json", "El body debe ser JSON válido."));
        closeSocket(clientSocket);
        return;
      }

      if (!input.contains("text") || !input["text"].is_string() ||
          input["text"].get<std::string>().empty()) {
        sendJson(clientSocket, 400,
                 errorResponse("missing_fields", "El campo text es obligatorio."));
        closeSocket(clientSocket);
        return;
      }

      const auto text = input["text"].get<std::string>();
      if (text.size() > options.maxInputBytes) {
        sendJson(clientSocket, 413,
                 errorResponse("payload_too_large",
                               "El texto excede el límite permitido."));
        closeSocket(clientSocket);
        return;
      }

      std::string fileName = makeOutputFileName();
      if (input.contains("output_file") && input["output_file"].is_string()) {
        fileName = input["output_file"].get<std::string>();
        if (!isSafeFileName(fileName) ||
            std::filesystem::path(fileName).extension() != ".wav") {
          sendJson(clientSocket, 400,
                   errorResponse("invalid_request",
                                 "output_file debe ser un nombre seguro con extensión .wav."));
          closeSocket(clientSocket);
          return;
        }
      }

      std::optional<std::string> requestedModel;
      if (input.contains("model")) {
        if (!input["model"].is_string()) {
          sendJson(clientSocket, 400,
                   errorResponse("invalid_request", "El campo model debe ser string."));
          closeSocket(clientSocket);
          return;
        }
        requestedModel = input["model"].get<std::string>();
      }

      JobLease jobLease(jobLimiter);
      if (!jobLease) {
        sendJson(clientSocket, 429, modelErrorResponse("server_busy"));
        closeSocket(clientSocket);
        return;
      }

      VoiceLease voiceLease;
      try {
        voiceLease = modelCache.checkout(requestedModel);
      } catch (const std::runtime_error &e) {
        const std::string message = e.what();
        sendJson(clientSocket, errorStatusForException(message),
                 modelErrorResponse(message));
        closeSocket(clientSocket);
        return;
      }

      auto &selectedVoice = voiceLease.get();
      const auto previousSpeakerId = selectedVoice.synthesisConfig.speakerId;
      if (input.contains("speaker_id") && input["speaker_id"].is_number_integer()) {
        selectedVoice.synthesisConfig.speakerId = input["speaker_id"].get<piper::SpeakerId>();
      }

      std::filesystem::create_directories(options.outputDir);
      const auto outputPath = options.outputDir / fileName;
      piper::SynthesisResult result;
      auto shouldCancel = [&clientSocket]() {
        return clientDisconnected(clientSocket);
      };

      try {
        if (shouldCancel()) {
          throw std::runtime_error("synthesis_cancelled");
        }

        std::ofstream audioFile(outputPath, std::ios::binary);
        if (!audioFile.good()) {
          throw std::runtime_error("Could not open output file");
        }

        piper::textToWavFile(piperConfig, selectedVoice, text, audioFile, result,
                             options.maxTextChunkBytes, shouldCancel);
      } catch (const std::exception &e) {
        selectedVoice.synthesisConfig.speakerId = previousSpeakerId;
        const std::string message = e.what();
        if (message == "synthesis_cancelled") {
          spdlog::warn("TTS request cancelled because client disconnected");
          std::error_code ignored;
          std::filesystem::remove(outputPath, ignored);
          closeSocket(clientSocket);
          return;
        }

        std::error_code ignored;
        std::filesystem::remove(outputPath, ignored);
        sendJson(clientSocket, 500,
                 errorResponse("synthesis_error", e.what()));
        closeSocket(clientSocket);
        return;
      }

      selectedVoice.synthesisConfig.speakerId = previousSpeakerId;

      std::uintmax_t bytes = 0;
      if (std::filesystem::exists(outputPath)) {
        bytes = std::filesystem::file_size(outputPath);
      }

      sendJson(clientSocket, 201,
               successResponse("Audio generado exitosamente.",
                               json{{"file", fileName},
                                    {"model", voiceLease.model().name},
                                    {"model_file", voiceLease.model().modelPath.string()},
                                    {"path", outputPath.string()},
                                    {"url", "/api/v1/files/" + fileName},
                                    {"format", "wav"},
                                    {"bytes", bytes},
                                    {"audio_seconds", result.audioSeconds},
                                    {"infer_seconds", result.inferSeconds},
                                    {"real_time_factor", result.realTimeFactor}}));
      closeSocket(clientSocket);
      return;
    }

    sendJson(clientSocket, 404,
             errorResponse("not_found", "Endpoint no encontrado."));
  } catch (const std::runtime_error &e) {
    const std::string message = e.what();
    if (message == "payload_too_large") {
      sendJson(clientSocket, 413,
               errorResponse("payload_too_large",
                             "El payload excede el límite permitido."));
    } else {
      sendJson(clientSocket, 500,
               errorResponse("server_error", message));
    }
  } catch (const std::exception &e) {
    sendJson(clientSocket, 500,
             errorResponse("server_error", e.what()));
  }

  closeSocket(clientSocket);
}

} // namespace

std::vector<ModelInfo> scanModels(const std::filesystem::path &modelsDir) {
  std::vector<ModelInfo> models;
  if (!std::filesystem::exists(modelsDir)) {
    return models;
  }

  for (const auto &entry : std::filesystem::directory_iterator(modelsDir)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    auto path = entry.path();
    if (path.extension() != ".onnx") {
      continue;
    }

    auto configPath = std::filesystem::path(path.string() + ".json");
    models.push_back(ModelInfo{path.filename().string(), path, configPath,
                               std::filesystem::exists(configPath)});
  }

  std::sort(models.begin(), models.end(), [](const ModelInfo &a, const ModelInfo &b) {
    return a.name < b.name;
  });

  return models;
}

std::optional<ModelInfo> findFirstUsableModel(const std::filesystem::path &modelsDir) {
  for (const auto &model : scanModels(modelsDir)) {
    if (model.hasConfig) {
      return model;
    }
  }

  return std::nullopt;
}

void runServer(piper::PiperConfig &piperConfig, piper::Voice &voice,
               const ServerOptions &options) {
  std::filesystem::create_directories(options.modelsDir);
  std::filesystem::create_directories(options.outputDir);

#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    throw std::runtime_error("WSAStartup failed");
  }
#endif

  SocketHandle serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocket == INVALID_SOCKET_HANDLE) {
    throw std::runtime_error("Could not create server socket");
  }

  int reuse = 1;
#ifdef _WIN32
  setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char *>(&reuse), sizeof(reuse));
#else
  setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<uint16_t>(options.port));
  if (inet_pton(AF_INET, options.host.c_str(), &address.sin_addr) <= 0) {
    closeSocket(serverSocket);
    throw std::runtime_error("Invalid --host value. Use an IPv4 address like 127.0.0.1 or 0.0.0.0");
  }

  if (bind(serverSocket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
    closeSocket(serverSocket);
    throw std::runtime_error("Could not bind server socket");
  }

  if (listen(serverSocket, 16) < 0) {
    closeSocket(serverSocket);
    throw std::runtime_error("Could not listen on server socket");
  }

  spdlog::info("Piper API server listening on http://{}:{}", options.host,
               options.port);
  spdlog::info("Models directory: {}", options.modelsDir.string());
  spdlog::info("Active model: {}", options.activeModelPath.string());
  spdlog::info("Output directory: {}", options.outputDir.string());
  if (options.apiToken.empty()) {
    spdlog::warn("API token is not configured; HTTP API is open on this bind address");
  } else {
    spdlog::info("API token authentication enabled");
  }
  spdlog::info("Concurrent jobs limit: {}", options.maxConcurrentJobs);
  spdlog::info("Model replicas per model: {}", options.maxModelReplicas);

  ModelCache modelCache(piperConfig, voice, options);
  JobLimiter jobLimiter(options.maxConcurrentJobs);

  while (true) {
    sockaddr_in clientAddress{};
#ifdef _WIN32
    int clientLength = sizeof(clientAddress);
#else
    socklen_t clientLength = sizeof(clientAddress);
#endif
    SocketHandle clientSocket =
        accept(serverSocket, reinterpret_cast<sockaddr *>(&clientAddress), &clientLength);
    if (clientSocket == INVALID_SOCKET_HANDLE) {
      continue;
    }

    std::thread(handleClient, clientSocket, std::ref(piperConfig), options,
                std::ref(modelCache), std::ref(jobLimiter))
        .detach();
  }

  closeSocket(serverSocket);
#ifdef _WIN32
  WSACleanup();
#endif
}

} // namespace piper_server
