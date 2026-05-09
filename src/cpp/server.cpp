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
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
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
  response << "Access-Control-Allow-Headers: Content-Type\r\n";
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
                  piper::Voice &voice, const ServerOptions &options,
                  std::mutex &synthesisMutex) {
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
                                    {"limits",
                                     json{{"max_input_bytes", options.maxInputBytes},
                                          {"max_text_chunk_bytes",
                                           options.maxTextChunkBytes}}}}));
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

      if (!synthesisMutex.try_lock()) {
        sendJson(clientSocket, 429,
                 errorResponse("server_busy",
                               "El servidor está procesando otra síntesis. Intenta nuevamente."));
        closeSocket(clientSocket);
        return;
      }

      std::unique_lock<std::mutex> synthesisLock(synthesisMutex, std::adopt_lock);
      const auto previousSpeakerId = voice.synthesisConfig.speakerId;
      if (input.contains("speaker_id") && input["speaker_id"].is_number_integer()) {
        voice.synthesisConfig.speakerId = input["speaker_id"].get<piper::SpeakerId>();
      }

      std::filesystem::create_directories(options.outputDir);
      const auto outputPath = options.outputDir / fileName;
      piper::SynthesisResult result;

      try {
        std::ofstream audioFile(outputPath, std::ios::binary);
        if (!audioFile.good()) {
          throw std::runtime_error("Could not open output file");
        }

        piper::textToWavFile(piperConfig, voice, text, audioFile, result,
                             options.maxTextChunkBytes);
      } catch (const std::exception &e) {
        voice.synthesisConfig.speakerId = previousSpeakerId;
        sendJson(clientSocket, 500,
                 errorResponse("synthesis_error", e.what()));
        closeSocket(clientSocket);
        return;
      }

      voice.synthesisConfig.speakerId = previousSpeakerId;
      synthesisLock.unlock();

      std::uintmax_t bytes = 0;
      if (std::filesystem::exists(outputPath)) {
        bytes = std::filesystem::file_size(outputPath);
      }

      sendJson(clientSocket, 201,
               successResponse("Audio generado exitosamente.",
                               json{{"file", fileName},
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

  std::mutex synthesisMutex;
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

    std::thread(handleClient, clientSocket, std::ref(piperConfig), std::ref(voice),
                options, std::ref(synthesisMutex))
        .detach();
  }

  closeSocket(serverSocket);
#ifdef _WIN32
  WSACleanup();
#endif
}

} // namespace piper_server
