#include "server.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <deque>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cerrno>
#include <limits>
#include <cstdlib>
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
#include <regex>
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
#include "neo_model.hpp"

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


struct TtsTextSanitizeResult {
  bool ok = true;
  std::string speakText;
  std::vector<std::string> warnings;
  double riskScore = 0.0;
  std::size_t rawBytes = 0;
  std::size_t speakBytes = 0;
  std::size_t rawChars = 0;
  std::size_t speakChars = 0;
  std::size_t urls = 0;
  std::size_t emails = 0;
  std::size_t codeBlocks = 0;
  std::size_t emojis = 0;
};

void addTtsWarning(TtsTextSanitizeResult &result, const std::string &warning) {
  if (std::find(result.warnings.begin(), result.warnings.end(), warning) == result.warnings.end()) {
    result.warnings.push_back(warning);
  }
}

bool isUtf8Continuation(unsigned char byte) { return (byte & 0xC0) == 0x80; }

bool decodeUtf8At(const std::string &input, std::size_t index, std::uint32_t &cp,
                  std::size_t &width) {
  if (index >= input.size()) return false;
  const auto b0 = static_cast<unsigned char>(input[index]);
  if (b0 <= 0x7F) {
    cp = b0;
    width = 1;
    return true;
  }

  if (b0 >= 0xC2 && b0 <= 0xDF) {
    if ((index + 1) >= input.size()) return false;
    const auto b1 = static_cast<unsigned char>(input[index + 1]);
    if (!isUtf8Continuation(b1)) return false;
    cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
    width = 2;
    return true;
  }

  if (b0 >= 0xE0 && b0 <= 0xEF) {
    if ((index + 2) >= input.size()) return false;
    const auto b1 = static_cast<unsigned char>(input[index + 1]);
    const auto b2 = static_cast<unsigned char>(input[index + 2]);
    if (!isUtf8Continuation(b1) || !isUtf8Continuation(b2)) return false;
    if (b0 == 0xE0 && b1 < 0xA0) return false;
    if (b0 == 0xED && b1 >= 0xA0) return false; // surrogate range
    cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
    width = 3;
    return true;
  }

  if (b0 >= 0xF0 && b0 <= 0xF4) {
    if ((index + 3) >= input.size()) return false;
    const auto b1 = static_cast<unsigned char>(input[index + 1]);
    const auto b2 = static_cast<unsigned char>(input[index + 2]);
    const auto b3 = static_cast<unsigned char>(input[index + 3]);
    if (!isUtf8Continuation(b1) || !isUtf8Continuation(b2) || !isUtf8Continuation(b3)) return false;
    if (b0 == 0xF0 && b1 < 0x90) return false;
    if (b0 == 0xF4 && b1 > 0x8F) return false;
    cp = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
    width = 4;
    return true;
  }

  return false;
}

void appendUtf8(std::string &out, std::uint32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

bool isValidUtf8Strict(const std::string &input, std::size_t *charCount = nullptr) {
  std::size_t count = 0;
  for (std::size_t i = 0; i < input.size();) {
    std::uint32_t cp = 0;
    std::size_t width = 0;
    if (!decodeUtf8At(input, i, cp, width)) return false;
    i += width;
    ++count;
  }
  if (charCount != nullptr) *charCount = count;
  return true;
}

bool isDefaultIgnorableForTts(std::uint32_t cp) {
  return cp == 0x00AD || cp == 0x034F || cp == 0x061C || cp == 0x115F || cp == 0x1160 ||
         cp == 0x17B4 || cp == 0x17B5 || cp == 0x180E || cp == 0x200B || cp == 0x200C ||
         cp == 0x200D || cp == 0x200E || cp == 0x200F || cp == 0x202A || cp == 0x202B ||
         cp == 0x202C || cp == 0x202D || cp == 0x202E || cp == 0x2060 ||
         (cp >= 0x2061 && cp <= 0x206F) || cp == 0x3164 || cp == 0xFEFF || cp == 0xFFA0 ||
         (cp >= 0xFE00 && cp <= 0xFE0F);
}

bool isEmojiLike(std::uint32_t cp) {
  return (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF);
}

std::uint32_t compatibilityFoldCodepoint(std::uint32_t cp) {
  if (cp >= 0xFF01 && cp <= 0xFF5E) {
    return cp - 0xFEE0; // full-width ASCII
  }
  switch (cp) {
  case 0x00A0: return ' ';
  case 0x2018:
  case 0x2019:
  case 0x201B:
  case 0x2032:
    return '\'';
  case 0x201C:
  case 0x201D:
  case 0x2033:
    return '"';
  case 0x2010:
  case 0x2011:
  case 0x2012:
  case 0x2013:
  case 0x2014:
  case 0x2212:
    return '-';
  case 0x2026:
    return '.';
  case 0xFB00: return 0; // handled as multi-char below
  case 0xFB01: return 0;
  case 0xFB02: return 0;
  default:
    return cp;
  }
}

std::string normalizeUtf8ForTts(const std::string &input, TtsTextSanitizeResult &result) {
  std::string out;
  bool changed = false;
  bool controlsRemoved = false;
  bool invisiblesRemoved = false;
  bool emojiSeen = false;
  std::size_t consecutiveEmoji = 0;

  for (std::size_t i = 0; i < input.size();) {
    std::uint32_t cp = 0;
    std::size_t width = 0;
    if (!decodeUtf8At(input, i, cp, width)) {
      result.ok = false;
      addTtsWarning(result, "INVALID_UTF8");
      return {};
    }

    i += width;

    if (cp == 0xFEFF && out.empty()) {
      changed = true;
      invisiblesRemoved = true;
      continue;
    }

    if ((cp < 0x20 && cp != '\n' && cp != '\r' && cp != '\t') || (cp >= 0x7F && cp <= 0x9F)) {
      out.push_back(' ');
      changed = true;
      controlsRemoved = true;
      continue;
    }

    if (isDefaultIgnorableForTts(cp)) {
      changed = true;
      invisiblesRemoved = true;
      continue;
    }

    if (isEmojiLike(cp)) {
      emojiSeen = true;
      ++consecutiveEmoji;
      ++result.emojis;
      if (consecutiveEmoji <= 3) {
        out.append(" emoji ");
      }
      changed = true;
      continue;
    }
    consecutiveEmoji = 0;

    if (cp == 0xFB00) {
      out.append("ff");
      changed = true;
      continue;
    }
    if (cp == 0xFB01) {
      out.append("fi");
      changed = true;
      continue;
    }
    if (cp == 0xFB02) {
      out.append("fl");
      changed = true;
      continue;
    }

    auto folded = compatibilityFoldCodepoint(cp);
    if (folded != cp) changed = true;
    if (folded == '.') {
      out.append("...");
    } else {
      appendUtf8(out, folded);
    }
  }

  if (changed) addTtsWarning(result, "NORMALIZED_UNICODE");
  if (controlsRemoved) addTtsWarning(result, "CONTROL_REMOVED");
  if (invisiblesRemoved) addTtsWarning(result, "INVISIBLE_REMOVED");
  if (emojiSeen) addTtsWarning(result, "EMOJI_SUMMARIZED");
  if (result.emojis > 3) out.append(" emojis omitidos ");
  return out;
}

std::string regexReplaceWithWarning(const std::string &input, const std::regex &pattern,
                                    const std::string &replacement,
                                    TtsTextSanitizeResult &result,
                                    const std::string &warning) {
  if (!std::regex_search(input, pattern)) return input;
  addTtsWarning(result, warning);
  return std::regex_replace(input, pattern, replacement);
}

std::string summarizeUrlsForTts(const std::string &input, TtsTextSanitizeResult &result) {
  static const std::regex urlRegex(
      R"(\b((https?:\/\/|www\.)[^\s<>()\[\]{}\"']+|[A-Za-z0-9][A-Za-z0-9.-]{1,}\.(com|org|net|io|dev|mx|us|uk|ai|app|cloud|site|kg)(\/[^\s<>()\[\]{}\"']*)?))",
      std::regex::icase);

  std::string output;
  std::sregex_iterator it(input.begin(), input.end(), urlRegex);
  std::sregex_iterator end;
  std::size_t cursor = 0;
  for (; it != end; ++it) {
    const auto &match = *it;
    output.append(input.substr(cursor, static_cast<std::size_t>(match.position()) - cursor));
    std::string url = match.str(1);
    url = std::regex_replace(url, std::regex(R"(^https?:\/\/)", std::regex::icase), "");
    url = std::regex_replace(url, std::regex(R"(^www\.)", std::regex::icase), "");
    const auto stop = url.find_first_of("/?#");
    std::string host = stop == std::string::npos ? url : url.substr(0, stop);
    host = std::regex_replace(host, std::regex(R"(\.)"), " punto ");
    output.append(" enlace a ");
    output.append(host.empty() ? "sitio" : host);
    output.push_back(' ');
    cursor = static_cast<std::size_t>(match.position() + match.length());
    ++result.urls;
  }
  if (cursor == 0) return input;
  output.append(input.substr(cursor));
  addTtsWarning(result, "URL_SUMMARIZED");
  return output;
}

std::string collapseRepeatedCodepoints(const std::string &input, std::size_t maxRun,
                                       TtsTextSanitizeResult &result) {
  std::string out;
  std::uint32_t previous = 0;
  std::size_t run = 0;
  bool changed = false;

  for (std::size_t i = 0; i < input.size();) {
    std::uint32_t cp = 0;
    std::size_t width = 0;
    if (!decodeUtf8At(input, i, cp, width)) break;
    i += width;
    if (cp == previous) {
      ++run;
    } else {
      previous = cp;
      run = 1;
    }
    if (run <= maxRun) {
      appendUtf8(out, cp);
    } else {
      changed = true;
    }
  }

  if (changed) addTtsWarning(result, "REPETITIONS_COLLAPSED");
  return out;
}

std::string normalizeWhitespaceForTts(const std::string &input, TtsTextSanitizeResult &result) {
  std::string out;
  out.reserve(input.size());
  bool changed = false;
  bool lastSpace = false;
  std::size_t newlines = 0;

  for (char c : input) {
    if (c == '\r' || c == '\n') {
      if (newlines < 2) out.push_back('\n');
      ++newlines;
      lastSpace = false;
      changed = true;
      continue;
    }
    newlines = 0;
    if (c == '\t' || c == '\f' || c == '\v' || c == ' ') {
      if (!lastSpace) out.push_back(' ');
      lastSpace = true;
      changed = true;
      continue;
    }
    out.push_back(c);
    lastSpace = false;
  }

  auto trimmed = trimCopy(out);
  if (trimmed != input) changed = true;
  if (changed) addTtsWarning(result, "WHITESPACE_NORMALIZED");
  return trimmed;
}

bool hasHighEntropySpan(const std::string &input) {
  static const std::regex highEntropy(R"(\b[A-Za-z0-9+\/_=-]{80,}\b)");
  return std::regex_search(input, highEntropy);
}

bool looksLikeCodeForTts(const std::string &input) {
  static const std::regex codeSignal(
      R"((```[\s\S]*?```|~~~[\s\S]*?~~~|\b(function|const|let|var|class|return|import|export|SELECT|INSERT|UPDATE|DELETE|FROM|WHERE)\b|[{\[\];<>_=]{2,}|=>|::))",
      std::regex::icase);
  if (!std::regex_search(input, codeSignal)) return false;

  std::size_t structural = 0;
  for (char c : input) {
    if (std::string("{}[]();=<>_\\/|$#").find(c) != std::string::npos) ++structural;
  }
  return input.size() > 0 && (static_cast<double>(structural) / static_cast<double>(input.size())) > 0.06;
}

std::string sanitizeTtsTextForApi(const std::string &rawText, std::size_t maxChars,
                                  TtsTextSanitizeResult &result) {
  result.rawBytes = rawText.size();
  std::size_t rawChars = 0;
  if (!isValidUtf8Strict(rawText, &rawChars)) {
    result.ok = false;
    addTtsWarning(result, "INVALID_UTF8");
    result.riskScore = 1.0;
    return {};
  }
  result.rawChars = rawChars;

  std::string text = trimCopy(rawText);
  if (text != rawText) addTtsWarning(result, "TEXT_TRIMMED");

  text = normalizeUtf8ForTts(text, result);
  if (!result.ok) return {};

  static const std::regex brRegex(R"(<\s*br\s*\/?\s*>)", std::regex::icase);
  static const std::regex blockTagRegex(R"(<\s*\/?\s*(p|div|section|article|main|header|footer|li|ul|ol|h[1-6]|blockquote|tr|td|th|table)\b[^>]*>)", std::regex::icase);
  static const std::regex tagRegex(R"(<[^>]+>)");
  const auto beforeMarkup = text;
  text = std::regex_replace(text, brRegex, "\n");
  text = std::regex_replace(text, blockTagRegex, "\n");
  text = std::regex_replace(text, tagRegex, " ");
  if (text != beforeMarkup) addTtsWarning(result, "MARKUP_STRIPPED");

  static const std::regex bbcodeRegex(R"(\[(\/?)(b|i|u|s|url|img|quote|code|color|size|list|\*|center|left|right)(=[^\]]*)?\])", std::regex::icase);
  text = regexReplaceWithWarning(text, bbcodeRegex, " ", result, "BBCODE_STRIPPED");

  static const std::regex fenceRegex(R"((```[\s\S]*?```|~~~[\s\S]*?~~~))");
  if (std::regex_search(text, fenceRegex)) {
    text = std::regex_replace(text, fenceRegex, " bloque de codigo omitido ");
    addTtsWarning(result, "CODE_SUMMARIZED");
    ++result.codeBlocks;
  }

  static const std::regex markdownLinkRegex(R"(!\[([^\]]*)\]\([^)]*\)|\[([^\]]+)\]\(([^)]+)\))");
  text = std::regex_replace(text, markdownLinkRegex, "$1$2");

  static const std::regex markdownSyntaxRegex(R"((^|\n)\s{0,3}#{1,6}\s+|(^|\n)\s*[-*+]\s+|(^|\n)\s*\d+[.)]\s+|(^|\n)\s*>\s?|[\*_~#]{1,}|[|]{2,})");
  text = regexReplaceWithWarning(text, markdownSyntaxRegex, " ", result, "MARKDOWN_STRIPPED");

  if (looksLikeCodeForTts(text)) {
    static const std::regex codeLineRegex(
        R"((\b(function|const|let|var|class|return|import|export|SELECT|INSERT|UPDATE|DELETE|FROM|WHERE)\b[^\n]{0,240}|[{\[\];<>_=]{2,}))",
        std::regex::icase);
    text = std::regex_replace(text, codeLineRegex, " fragmento tecnico omitido ");
    addTtsWarning(result, "CODE_SUMMARIZED");
    ++result.codeBlocks;
  }

  text = summarizeUrlsForTts(text, result);

  static const std::regex emailRegex(R"(\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b)", std::regex::icase);
  if (std::regex_search(text, emailRegex)) {
    text = std::regex_replace(text, emailRegex, " correo electronico ");
    addTtsWarning(result, "EMAIL_SUMMARIZED");
    ++result.emails;
  }

  if (hasHighEntropySpan(text)) {
    static const std::regex highEntropy(R"(\b[A-Za-z0-9+\/_=-]{80,}\b)");
    text = std::regex_replace(text, highEntropy, " cadena tecnica omitida ");
    addTtsWarning(result, "HIGH_ENTROPY_SPAN");
  }

  text = collapseRepeatedCodepoints(text, 5, result);
  text = normalizeWhitespaceForTts(text, result);

  std::size_t speakChars = 0;
  isValidUtf8Strict(text, &speakChars);
  if (speakChars > maxChars) {
    std::string clipped;
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size() && count < maxChars;) {
      std::uint32_t cp = 0;
      std::size_t width = 0;
      if (!decodeUtf8At(text, i, cp, width)) break;
      clipped.append(text.substr(i, width));
      i += width;
      ++count;
    }
    text = trimCopy(clipped);
    addTtsWarning(result, "TEXT_TOO_LONG");
  }

  if (text.empty()) {
    addTtsWarning(result, "EMPTY_AFTER_SANITIZE");
    result.ok = false;
    result.riskScore = 1.0;
    return {};
  }

  result.speakText = text;
  result.speakBytes = text.size();
  isValidUtf8Strict(text, &result.speakChars);

  for (const auto &warning : result.warnings) {
    if (warning == "EMPTY_AFTER_SANITIZE" || warning == "TEXT_TOO_LONG" || warning == "INVALID_UTF8") {
      result.riskScore += 0.45;
    } else if (warning == "CODE_SUMMARIZED" || warning == "HIGH_ENTROPY_SPAN") {
      result.riskScore += 0.28;
    } else if (warning == "INVISIBLE_REMOVED" || warning == "URL_SUMMARIZED") {
      result.riskScore += 0.14;
    } else {
      result.riskScore += 0.06;
    }
  }
  result.riskScore = std::min(1.0, result.riskScore);
  return result.speakText;
}

json ttsSanitizeResultToJson(const TtsTextSanitizeResult &result) {
  return json{{"speakText", result.speakText},
              {"warnings", result.warnings},
              {"riskScore", result.riskScore},
              {"stats", json{{"rawChars", result.rawChars},
                               {"speakChars", result.speakChars},
                               {"rawBytes", result.rawBytes},
                               {"speakBytes", result.speakBytes},
                               {"urls", result.urls},
                               {"emails", result.emails},
                               {"codeBlocks", result.codeBlocks},
                               {"emojis", result.emojis}}}};
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
  case 422:
    return "Unprocessable Content";
  case 429:
    return "Too Many Requests";
  case 500:
    return "Internal Server Error";
  case 507:
    return "Insufficient Storage";
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

struct ParsedTarget {
  std::string path;
  std::map<std::string, std::string> query;
};

std::string urlDecode(const std::string &value) {
  std::string decoded;
  decoded.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && (i + 2) < value.size()) {
      const auto hex = value.substr(i + 1, 2);
      char *end = nullptr;
      long code = std::strtol(hex.c_str(), &end, 16);
      if (end != nullptr && *end == '\0') {
        decoded.push_back(static_cast<char>(code));
        i += 2;
        continue;
      }
    }
    if (value[i] == '+') {
      decoded.push_back(' ');
    } else {
      decoded.push_back(value[i]);
    }
  }
  return decoded;
}

ParsedTarget parseTarget(const std::string &rawPath) {
  ParsedTarget target;
  const auto question = rawPath.find('?');
  target.path = question == std::string::npos ? rawPath : rawPath.substr(0, question);
  if (question == std::string::npos) {
    return target;
  }

  std::string queryString = rawPath.substr(question + 1);
  std::size_t offset = 0;
  while (offset <= queryString.size()) {
    const auto amp = queryString.find('&', offset);
    const auto part = queryString.substr(offset, amp == std::string::npos ? std::string::npos : amp - offset);
    if (!part.empty()) {
      const auto equals = part.find('=');
      const auto key = urlDecode(part.substr(0, equals));
      const auto value = equals == std::string::npos ? std::string() : urlDecode(part.substr(equals + 1));
      target.query[lowerCopy(key)] = value;
    }
    if (amp == std::string::npos) {
      break;
    }
    offset = amp + 1;
  }
  return target;
}

std::optional<std::string> queryValue(const ParsedTarget &target,
                                      const std::string &name) {
  auto it = target.query.find(lowerCopy(name));
  if (it == target.query.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::string> routeFileName(const std::string &path) {
  const std::string prefix = "/api/v1/files/";
  if (path.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }

  return urlDecode(path.substr(prefix.size()));
}

std::optional<std::string> routeModelImageName(const std::string &path) {
  const std::string prefix = "/api/v1/models/";
  const std::string suffix = "/image";
  if (path.rfind(prefix, 0) != 0 || path.size() <= (prefix.size() + suffix.size())) {
    return std::nullopt;
  }

  if (path.substr(path.size() - suffix.size()) != suffix) {
    return std::nullopt;
  }

  return urlDecode(path.substr(prefix.size(), path.size() - prefix.size() - suffix.size()));
}

json loadJsonFile(const std::filesystem::path &path) {
  std::ifstream file(path.string(), std::ios::binary);
  if (!file.good()) {
    throw std::runtime_error("not_found");
  }
  json root;
  file >> root;
  return root;
}

std::optional<json> tryLoadJsonFile(const std::filesystem::path &path,
                                    std::string &error) {
  try {
    return loadJsonFile(path);
  } catch (const json::exception &) {
    error = "invalid_json";
  } catch (const std::exception &e) {
    error = e.what();
  }
  return std::nullopt;
}

template <typename T>
void copyIfExists(json &to, const json &from, const std::string &key) {
  if (from.contains(key) && !from[key].is_null()) {
    try {
      to[key] = from[key].get<T>();
    } catch (const json::exception &) {
      // Ignore invalid metadata fields. A bad optional field should not hide a model.
    }
  }
}

bool modelJsonHasImage(const json &root) {
  return root.contains("modelcard") && root["modelcard"].is_object() &&
         root["modelcard"].contains("image") && root["modelcard"]["image"].is_string() &&
         !root["modelcard"]["image"].get<std::string>().empty();
}

json modelInfoToJson(const ModelInfo &modelInfo, const std::string &includeMode) {
  json out{{"file", modelInfo.name},
           {"name", modelInfo.name},
           {"format", modelInfo.format},
           {"config_file", modelInfo.isNeo ? "embedded" : modelInfo.configPath.filename().string()},
           {"available", std::filesystem::exists(modelInfo.modelPath)},
           {"has_config", modelInfo.hasConfig},
           {"config_valid", false}};

  if (!modelInfo.hasConfig) {
    return out;
  }

  json root;
  std::optional<piper_neo::NeoPackageInfo> neoInfo;
  if (modelInfo.isNeo) {
    try {
      neoInfo = piper_neo::inspectPackage(modelInfo.modelPath);
      root = neoInfo->metadata;
    } catch (const std::exception &e) {
      out["config_error"] = e.what();
      return out;
    }
  } else {
    std::string configError;
    auto maybeRoot = tryLoadJsonFile(modelInfo.configPath, configError);
    if (!maybeRoot) {
      out["config_error"] = configError.empty() ? "invalid_json" : configError;
      return out;
    }
    root = *maybeRoot;
  }

  out["config_valid"] = true;
  if (neoInfo) {
    out["neo"] = json{{"version", neoInfo->version},
                       {"model_compression", neoInfo->modelCompression},
                       {"model_bytes", neoInfo->modelBytes},
                       {"stored_model_bytes", neoInfo->storedModelBytes}};
  }

  json modelcard = json::object();
  if (root.contains("modelcard") && root["modelcard"].is_object()) {
    const auto &card = root["modelcard"];
    copyIfExists<std::string>(modelcard, card, "id");
    copyIfExists<std::string>(modelcard, card, "name");
    copyIfExists<std::string>(modelcard, card, "description");
    copyIfExists<std::string>(modelcard, card, "language");
    copyIfExists<std::string>(modelcard, card, "voiceprompt");
    copyIfExists<std::string>(modelcard, card, "sha256");
  }

  const bool hasImage = neoInfo ? neoInfo->hasImage : modelJsonHasImage(root);
  out["has_image"] = hasImage;
  if (hasImage) {
    out["image_url"] = "/api/v1/models/" + modelInfo.name + "/image";
  }

  if (!modelcard.empty()) {
    out["modelcard"] = modelcard;
    if (modelcard.contains("name")) {
      out["name"] = modelcard["name"];
    }
    if (modelcard.contains("language")) {
      out["language"] = modelcard["language"];
    }
  }

  if (includeMode == "basic") {
    return out;
  }

  if (root.contains("dataset")) {
    out["dataset"] = root["dataset"];
  }
  if (root.contains("audio") && root["audio"].is_object()) {
    json audio = json::object();
    copyIfExists<int>(audio, root["audio"], "sample_rate");
    copyIfExists<std::string>(audio, root["audio"], "quality");
    out["audio"] = audio;
  }
  if (root.contains("language") && root["language"].is_object()) {
    json language = json::object();
    copyIfExists<std::string>(language, root["language"], "code");
    out["language_info"] = language;
  }
  if (root.contains("espeak") && root["espeak"].is_object()) {
    json espeak = json::object();
    copyIfExists<std::string>(espeak, root["espeak"], "voice");
    out["espeak"] = espeak;
  }
  if (root.contains("inference") && root["inference"].is_object()) {
    json inference = json::object();
    copyIfExists<double>(inference, root["inference"], "noise_scale");
    copyIfExists<double>(inference, root["inference"], "length_scale");
    copyIfExists<double>(inference, root["inference"], "noise_w");
    out["inference"] = inference;
  }
  copyIfExists<int>(out, root, "num_speakers");
  copyIfExists<std::string>(out, root, "piper_version");

  if (includeMode == "technical") {
    copyIfExists<std::string>(out, root, "phoneme_type");
    copyIfExists<int>(out, root, "num_symbols");
    if (root.contains("speaker_id_map") && root["speaker_id_map"].is_object()) {
      out["speaker_id_map"] = root["speaker_id_map"];
    }
  }

  return out;
}

const std::string BASE64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string decodeBase64(const std::string &encoded) {
  std::array<int, 256> table{};
  table.fill(-1);
  for (int i = 0; i < static_cast<int>(BASE64_CHARS.size()); ++i) {
    table[static_cast<unsigned char>(BASE64_CHARS[i])] = i;
  }

  std::string decoded;
  int val = 0;
  int valb = -8;
  for (unsigned char c : encoded) {
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
      decoded.push_back(static_cast<char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return decoded;
}

std::pair<std::string, std::string> parseDataImage(const std::string &dataUri) {
  const std::string prefix = "data:image/";
  if (dataUri.rfind(prefix, 0) != 0) {
    throw std::runtime_error("invalid_image");
  }
  const auto comma = dataUri.find(',');
  if (comma == std::string::npos) {
    throw std::runtime_error("invalid_image");
  }
  const auto meta = dataUri.substr(0, comma);
  const auto lowerMeta = lowerCopy(meta);
  if (lowerMeta.find(";base64") == std::string::npos) {
    throw std::runtime_error("invalid_image");
  }

  std::string contentType;
  if (lowerMeta.rfind("data:image/jpeg", 0) == 0 || lowerMeta.rfind("data:image/jpg", 0) == 0) {
    contentType = "image/jpeg";
  } else if (lowerMeta.rfind("data:image/png", 0) == 0) {
    contentType = "image/png";
  } else if (lowerMeta.rfind("data:image/webp", 0) == 0) {
    contentType = "image/webp";
  } else {
    throw std::runtime_error("invalid_image");
  }

  return {contentType, decodeBase64(dataUri.substr(comma + 1))};
}



class ModelRegistry {
public:
  explicit ModelRegistry(const ServerOptions &options)
      : options(options), refreshSeconds(std::max<std::size_t>(1, options.modelsRefreshSeconds)) {}

  json list(const std::string &includeMode) {
    refreshIfNeeded();
    std::lock_guard<std::mutex> lock(mutex);
    json models = json::array();
    for (const auto &model : cachedModels) {
      models.push_back(modelInfoToJson(model, includeMode));
    }
    return models;
  }

  std::optional<ModelInfo> find(const std::string &modelName) {
    refreshIfNeeded();
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &model : cachedModels) {
      if (model.name == modelName) {
        return model;
      }
    }
    return std::nullopt;
  }

  void forceRefresh() {
    std::lock_guard<std::mutex> lock(mutex);
    refreshLocked();
  }

private:
  void refreshIfNeeded() {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex);
    if (!initialized || now >= nextRefresh) {
      refreshLocked();
    }
  }

  void refreshLocked() {
    cachedModels = scanModels(options.modelsDir);
    initialized = true;
    nextRefresh = std::chrono::steady_clock::now() + std::chrono::seconds(refreshSeconds);
  }

  const ServerOptions &options;
  const std::size_t refreshSeconds;
  std::mutex mutex;
  bool initialized = false;
  std::chrono::steady_clock::time_point nextRefresh{};
  std::vector<ModelInfo> cachedModels;
};

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
                                         const std::string &requestedModel,
                                         ModelRegistry *registry = nullptr) {
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
    candidates.push_back(modelName + ".neo");
  }

  for (const auto &candidate : candidates) {
    if (candidate == options.activeModelPath.filename().string()) {
      const auto configPath = options.activeModelConfigPath.empty()
                                  ? std::filesystem::path(options.activeModelPath.string() + ".json")
                                  : options.activeModelConfigPath;
      return ModelInfo{candidate, piper_neo::isNeoFile(options.activeModelPath) ? "neo" : "onnx",
                       options.activeModelPath, configPath,
                       piper_neo::isNeoFile(options.activeModelPath) || std::filesystem::exists(configPath),
                       piper_neo::isNeoFile(options.activeModelPath)};
    }
  }

  for (const auto &candidate : candidates) {
    if (registry != nullptr) {
      if (auto model = registry->find(candidate)) {
        return model;
      }
      continue;
    }

    const auto models = scanModels(options.modelsDir);
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
  std::condition_variable cv;
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
      runtime->cv.notify_one();
    }
    slot = nullptr;
    runtime.reset();
  }
};

class ModelCache {
public:
  ModelCache(piper::PiperConfig &piperConfig, piper::Voice &defaultVoice,
             const ServerOptions &options, ModelRegistry &registry)
      : piperConfig(piperConfig), options(options), registry(registry),
        maxReplicas(std::max<std::size_t>(1, options.maxModelReplicas)) {
    const auto activeName = options.activeModelPath.filename().string();
    const auto activeConfig = options.activeModelConfigPath.empty()
                                  ? std::filesystem::path(options.activeModelPath.string() + ".json")
                                  : options.activeModelConfigPath;
    const bool activeIsNeo = piper_neo::isNeoFile(options.activeModelPath);
    auto runtime = std::make_shared<ModelRuntime>(
        ModelInfo{activeName, activeIsNeo ? "neo" : "onnx", options.activeModelPath, activeConfig,
                  activeIsNeo || std::filesystem::exists(activeConfig), activeIsNeo});

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

    while ((runtime->slots.size() + runtime->loadingSlots) >= maxReplicas) {
      runtime->cv.wait(lock, [&runtime]() {
        for (const auto &slot : runtime->slots) {
          if (!slot->inUse) {
            return true;
          }
        }
        return false;
      });

      for (const auto &slot : runtime->slots) {
        if (!slot->inUse) {
          slot->inUse = true;
          return VoiceLease(runtime, slot.get());
        }
      }
    }

    ++runtime->loadingSlots;
    auto newVoice = std::make_unique<piper::Voice>();
    auto speakerId = options.defaultSpeakerId;
    lock.unlock();

    try {
      const auto loadStart = std::chrono::steady_clock::now();
      spdlog::info("{} Model load started: model={} format={}", nowIso8601(), info.name, info.format);
      auto loadModelPath = info.modelPath;
      auto loadConfigPath = info.configPath;
      if (info.isNeo) {
        auto extracted = piper_neo::extractPackage(info.modelPath, options.outputDir / "neo-cache");
        loadModelPath = extracted.modelPath;
        loadConfigPath = extracted.configPath;
      }
      piper::loadVoice(piperConfig, loadModelPath.string(), loadConfigPath.string(),
                       *newVoice, speakerId, options.useCuda, options.cpuThreads);
      const auto loadEnd = std::chrono::steady_clock::now();
      spdlog::info("{} Model load finished: model={} format={} duration_ms={}", nowIso8601(),
                   info.name, info.format,
                   std::chrono::duration_cast<std::chrono::milliseconds>(loadEnd - loadStart).count());
    } catch (...) {
      lock.lock();
      if (runtime->loadingSlots > 0) {
        --runtime->loadingSlots;
      }
      runtime->cv.notify_all();
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
    runtime->cv.notify_all();
    return VoiceLease(runtime, slotPtr);
  }

private:
  ModelInfo resolve(const std::optional<std::string> &requestedModel) {
    const auto modelName = requestedModel ? trimCopy(*requestedModel) : std::string();
    auto info = findModelByName(options, modelName, &registry);
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
  ModelRegistry &registry;
  const std::size_t maxReplicas;
  std::mutex cacheMutex;
  std::map<std::string, std::shared_ptr<ModelRuntime>> cache;
};

struct ServerMetrics {
  std::atomic<std::uint64_t> acceptedJobs{0};
  std::atomic<std::uint64_t> completedJobs{0};
  std::atomic<std::uint64_t> cancelledJobs{0};
  std::atomic<std::uint64_t> failedJobs{0};
  std::atomic<std::uint64_t> rejectedJobs{0};
  std::atomic<std::uint64_t> completedChunks{0};
  std::atomic<std::uint64_t> failedChunks{0};
  std::atomic<std::uint64_t> processingChunks{0};
  std::atomic<std::uint64_t> tempStorageBytes{0};
  std::atomic<std::uint64_t> sanitizedInputs{0};
  std::atomic<std::uint64_t> sanitizeWarnings{0};
  std::atomic<std::uint64_t> rejectedTextInputs{0};
};

struct TtsJobResult {
  std::string fileName;
  std::filesystem::path outputPath;
  std::string modelName;
  std::filesystem::path modelPath;
  std::size_t chunks = 0;
  std::uintmax_t bytes = 0;
  piper::SynthesisResult synthesis;
};

struct TtsJobRequest {
  std::string text;
  std::string fileName;
  std::filesystem::path outputPath;
  std::optional<std::string> requestedModel;
  std::optional<piper::SpeakerId> speakerId;
  std::function<bool()> shouldCancel;
};

struct PcmWavHeader {
  uint8_t RIFF[4] = {'R', 'I', 'F', 'F'};
  uint32_t chunkSize = 0;
  uint8_t WAVE[4] = {'W', 'A', 'V', 'E'};
  uint8_t fmt[4] = {'f', 'm', 't', ' '};
  uint32_t fmtSize = 16;
  uint16_t audioFormat = 1;
  uint16_t numChannels = 1;
  uint32_t sampleRate = 22050;
  uint32_t bytesPerSec = 44100;
  uint16_t blockAlign = 2;
  uint16_t bitsPerSample = 16;
  uint8_t data[4] = {'d', 'a', 't', 'a'};
  uint32_t dataSize = 0;
};

void writeServerWavHeader(int sampleRate, int sampleWidth, int channels,
                          std::uint32_t dataSizeBytes,
                          std::ostream &audioFile) {
  PcmWavHeader header;
  header.dataSize = dataSizeBytes;
  header.chunkSize = header.dataSize + sizeof(PcmWavHeader) - 8;
  header.sampleRate = static_cast<uint32_t>(sampleRate);
  header.numChannels = static_cast<uint16_t>(channels);
  header.bytesPerSec = static_cast<uint32_t>(sampleRate * sampleWidth * channels);
  header.blockAlign = static_cast<uint16_t>(sampleWidth * channels);
  header.bitsPerSample = static_cast<uint16_t>(sampleWidth * 8);
  audioFile.write(reinterpret_cast<const char *>(&header), sizeof(header));
}

class FairTtsScheduler {
public:
  FairTtsScheduler(piper::PiperConfig &piperConfig, ModelCache &modelCache,
                   const ServerOptions &options, ServerMetrics &metrics)
      : piperConfig(piperConfig), modelCache(modelCache), options(options),
        metrics(metrics), maxJobs(std::max<std::size_t>(1, options.maxConcurrentJobs)),
        queueSize(std::max<std::size_t>(maxJobs, options.queueSize)),
        workerCount(std::max<std::size_t>(1, options.chunkWorkers)) {
    for (std::size_t i = 0; i < workerCount; ++i) {
      workers.emplace_back([this]() { workerLoop(); });
    }
  }

  ~FairTtsScheduler() {
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      stopping = true;
      queueCv.notify_all();
    }
    for (auto &worker : workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  TtsJobResult synthesize(const TtsJobRequest &request) {
    if (request.shouldCancel && request.shouldCancel()) {
      metrics.cancelledJobs++;
      throw std::runtime_error("synthesis_cancelled");
    }

    const auto jobStart = std::chrono::steady_clock::now();
    auto job = std::make_shared<JobState>();
    job->id = makeOutputFileName();
    if (job->id.size() > 4 && job->id.substr(job->id.size() - 4) == ".wav") {
      job->id.erase(job->id.size() - 4);
    }
    job->textChunks = piper::splitTextIntoChunks(request.text, options.maxTextChunkBytes);
    job->fileName = request.fileName;
    job->outputPath = request.outputPath;
    job->requestedModel = request.requestedModel;
    job->speakerId = request.speakerId;
    job->shouldCancel = request.shouldCancel;
    job->pendingChunks = job->textChunks.size();
    job->chunkPaths.resize(job->textChunks.size());
    job->chunkBytes.resize(job->textChunks.size(), 0);
    job->tempDir = options.outputDir / "tmp" / job->id;

    if (job->textChunks.empty()) {
      throw std::runtime_error("missing_fields");
    }

    spdlog::info("{} TTS job queued: id={} model={} input_bytes={} chunks={}",
                 nowIso8601(), job->id,
                 request.requestedModel.value_or("<default>"), request.text.size(),
                 job->textChunks.size());

    std::filesystem::create_directories(job->tempDir);

    {
      std::lock_guard<std::mutex> lock(queueMutex);
      if ((activeJobs + waitingJobs) >= queueSize) {
        metrics.rejectedJobs++;
        throw std::runtime_error("server_busy");
      }

      metrics.acceptedJobs++;
      if (activeJobs < maxJobs) {
        job->activated = true;
        ++activeJobs;
        activeRoundRobin.push_back(job);
      } else {
        ++waitingJobs;
        pendingJobs.push_back(job);
      }
    }
    queueCv.notify_all();

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(std::max<std::size_t>(1, options.queueTimeoutSeconds));

    {
      std::unique_lock<std::mutex> lock(job->mutex);
      while (!job->done) {
        if (request.shouldCancel && request.shouldCancel()) {
          job->cancelled = true;
          job->pendingChunks = job->inFlightChunks;
          if (job->pendingChunks == 0) {
            job->done = true;
            job->cv.notify_all();
          }
          queueCv.notify_all();
        }

        if (std::chrono::steady_clock::now() > deadline && job->startedChunks == 0) {
          job->cancelled = true;
          job->failed = true;
          job->error = "server_busy";
          job->pendingChunks = 0;
          job->done = true;
          job->cv.notify_all();
          queueCv.notify_all();
        }

        job->cv.wait_for(lock, std::chrono::milliseconds(100));
      }
    }

    {
      std::lock_guard<std::mutex> lock(queueMutex);
      if (job->activated) {
        if (activeJobs > 0) {
          --activeJobs;
        }
      } else if (waitingJobs > 0) {
        --waitingJobs;
      }
      activatePendingJobsLocked();
    }
    queueCv.notify_all();

    if (job->cancelled && !job->failed) {
      cleanupJob(*job);
      metrics.cancelledJobs++;
      throw std::runtime_error("synthesis_cancelled");
    }

    if (job->failed) {
      cleanupJob(*job);
      metrics.failedJobs++;
      throw std::runtime_error(job->error.empty() ? "synthesis_error" : job->error);
    }

    assembleWav(*job);
    const auto jobEnd = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(jobEnd - jobStart).count();
    spdlog::info("{} TTS job finished: id={} model={} chunks={} audio_seconds={} infer_seconds={} duration_ms={} file={} bytes={}",
                 nowIso8601(), job->id, job->modelName, job->textChunks.size(),
                 job->synthesis.audioSeconds, job->synthesis.inferSeconds,
                 elapsedMs, job->fileName,
                 std::filesystem::exists(job->outputPath) ? std::filesystem::file_size(job->outputPath) : 0);
    cleanupJob(*job);
    metrics.completedJobs++;

    TtsJobResult result;
    result.fileName = job->fileName;
    result.outputPath = job->outputPath;
    result.modelName = job->modelName;
    result.modelPath = job->modelPath;
    result.chunks = job->textChunks.size();
    result.synthesis = job->synthesis;
    if (std::filesystem::exists(job->outputPath)) {
      result.bytes = std::filesystem::file_size(job->outputPath);
    }
    return result;
  }

  std::size_t activeJobCount() const {
    std::lock_guard<std::mutex> lock(queueMutex);
    return activeJobs;
  }

  std::size_t waitingJobCount() const {
    std::lock_guard<std::mutex> lock(queueMutex);
    return waitingJobs;
  }

  std::size_t workerTotal() const { return workerCount; }

private:
  struct JobState {
    std::string id;
    std::string fileName;
    std::filesystem::path outputPath;
    std::filesystem::path tempDir;
    std::optional<std::string> requestedModel;
    std::optional<piper::SpeakerId> speakerId;
    std::function<bool()> shouldCancel;
    std::vector<std::string> textChunks;
    std::vector<std::filesystem::path> chunkPaths;
    std::vector<std::uintmax_t> chunkBytes;
    std::atomic<std::uint64_t> allocatedTempBytes{0};
    std::size_t nextChunk = 0;
    std::size_t pendingChunks = 0;
    std::size_t startedChunks = 0;
    std::size_t inFlightChunks = 0;
    bool activated = false;
    bool cancelled = false;
    bool failed = false;
    bool done = false;
    std::string error;
    std::string modelName;
    std::filesystem::path modelPath;
    int sampleRate = 22050;
    int sampleWidth = 2;
    int channels = 1;
    piper::SynthesisResult synthesis;
    std::mutex mutex;
    std::condition_variable cv;
  };

  struct WorkItem {
    std::shared_ptr<JobState> job;
    std::size_t index = 0;
  };

  void activatePendingJobsLocked() {
    while (activeJobs < maxJobs && !pendingJobs.empty()) {
      auto pending = pendingJobs.front();
      pendingJobs.pop_front();
      if (waitingJobs > 0) {
        --waitingJobs;
      }

      std::lock_guard<std::mutex> jobLock(pending->mutex);
      if (pending->done || pending->cancelled || pending->failed) {
        continue;
      }

      pending->activated = true;
      ++activeJobs;
      activeRoundRobin.push_back(pending);
    }
  }

  std::optional<WorkItem> nextWork() {
    std::unique_lock<std::mutex> lock(queueMutex);
    queueCv.wait(lock, [this]() { return stopping || !activeRoundRobin.empty(); });
    if (stopping) {
      return std::nullopt;
    }

    while (!activeRoundRobin.empty()) {
      auto job = activeRoundRobin.front();
      activeRoundRobin.pop_front();

      {
        std::lock_guard<std::mutex> jobLock(job->mutex);
        if (job->cancelled || job->failed || job->nextChunk >= job->textChunks.size()) {
          continue;
        }

        const std::size_t index = job->nextChunk++;
        ++job->startedChunks;
        ++job->inFlightChunks;

        if (job->nextChunk < job->textChunks.size()) {
          activeRoundRobin.push_back(job);
        }
        return WorkItem{job, index};
      }
    }

    return std::nullopt;
  }

  void workerLoop() {
    while (true) {
      auto maybeWork = nextWork();
      if (!maybeWork) {
        if (stopping) {
          return;
        }
        continue;
      }

      metrics.processingChunks++;
      processChunk(maybeWork->job, maybeWork->index);
      metrics.processingChunks--;
    }
  }

  void processChunk(const std::shared_ptr<JobState> &job, std::size_t index) {
    try {
      {
        std::lock_guard<std::mutex> lock(job->mutex);
        if (job->cancelled || job->failed) {
          if (job->pendingChunks > job->inFlightChunks) {
            job->pendingChunks = job->inFlightChunks;
          }
          markChunkFinished(*job);
          return;
        }
      }

      auto shouldCancel = [job]() {
        std::lock_guard<std::mutex> lock(job->mutex);
        if (job->cancelled || job->failed) {
          return true;
        }
        return job->shouldCancel ? job->shouldCancel() : false;
      };

      if (shouldCancel()) {
        throw std::runtime_error("synthesis_cancelled");
      }

      VoiceLease voiceLease = modelCache.checkout(job->requestedModel);
      if (shouldCancel()) {
        throw std::runtime_error("synthesis_cancelled");
      }

      auto &selectedVoice = voiceLease.get();
      const auto previousSpeakerId = selectedVoice.synthesisConfig.speakerId;
      if (job->speakerId) {
        selectedVoice.synthesisConfig.speakerId = *job->speakerId;
      }

      const auto chunkPath = job->tempDir / ("chunk_" + std::to_string(index) + ".raw");
      if (shouldCancel()) {
        selectedVoice.synthesisConfig.speakerId = previousSpeakerId;
        throw std::runtime_error("synthesis_cancelled");
      }

      std::ofstream chunkFile(chunkPath, std::ios::binary);
      if (!chunkFile.good()) {
        selectedVoice.synthesisConfig.speakerId = previousSpeakerId;
        throw std::runtime_error("Could not open chunk temp file");
      }

      piper::SynthesisResult chunkResult;
      std::vector<int16_t> audioBuffer;
      std::uintmax_t bytes = 0;
      auto audioCallback = [&]() {
        if (audioBuffer.empty()) {
          return;
        }
        const std::size_t audioBytes = sizeof(int16_t) * audioBuffer.size();
        chunkFile.write(reinterpret_cast<const char *>(audioBuffer.data()), audioBytes);
        bytes += audioBytes;

        const auto newTempTotal = metrics.tempStorageBytes.fetch_add(audioBytes) + audioBytes;
        job->allocatedTempBytes.fetch_add(audioBytes);
        if (options.maxTempBytes > 0 && newTempTotal > options.maxTempBytes) {
          throw std::runtime_error("temp_storage_full");
        }
      };

      try {
        piper::textToAudio(piperConfig, selectedVoice, job->textChunks[index], audioBuffer,
                           chunkResult, audioCallback, shouldCancel);
      } catch (...) {
        selectedVoice.synthesisConfig.speakerId = previousSpeakerId;
        throw;
      }
      selectedVoice.synthesisConfig.speakerId = previousSpeakerId;
      chunkFile.close();

      {
        std::lock_guard<std::mutex> lock(job->mutex);
        job->chunkPaths[index] = chunkPath;
        job->chunkBytes[index] = bytes;
        job->sampleRate = selectedVoice.synthesisConfig.sampleRate;
        job->sampleWidth = selectedVoice.synthesisConfig.sampleWidth;
        job->channels = selectedVoice.synthesisConfig.channels;
        job->modelName = voiceLease.model().name;
        job->modelPath = voiceLease.model().modelPath;
        job->synthesis.audioSeconds += chunkResult.audioSeconds;
        job->synthesis.inferSeconds += chunkResult.inferSeconds;
        if (job->synthesis.audioSeconds > 0) {
          job->synthesis.realTimeFactor = job->synthesis.inferSeconds / job->synthesis.audioSeconds;
        }
        metrics.completedChunks++;
        markChunkFinished(*job);
      }
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(job->mutex);
      const std::string message = e.what();
      if (message == "synthesis_cancelled") {
        job->cancelled = true;
      } else {
        job->failed = true;
        job->error = message;
        metrics.failedChunks++;
      }
      if (job->pendingChunks > job->inFlightChunks) {
        job->pendingChunks = job->inFlightChunks;
      }
      markChunkFinished(*job);
    }
  }

  void markChunkFinished(JobState &job) {
    if (job.inFlightChunks > 0) {
      --job.inFlightChunks;
    }
    if (job.pendingChunks > 0) {
      --job.pendingChunks;
    }
    if (job.pendingChunks == 0 && job.inFlightChunks == 0) {
      job.done = true;
      job.cv.notify_all();
    }
  }

  void assembleWav(const JobState &job) {
    std::uint64_t totalBytes = 0;
    for (const auto bytes : job.chunkBytes) {
      totalBytes += bytes;
    }

    if (totalBytes > std::numeric_limits<std::uint32_t>::max()) {
      throw std::runtime_error("Generated WAV exceeds 4 GiB. Use smaller inputs or raw output.");
    }

    std::ofstream output(job.outputPath, std::ios::binary);
    if (!output.good()) {
      throw std::runtime_error("Could not open output file");
    }

    writeServerWavHeader(job.sampleRate, job.sampleWidth, job.channels,
                         static_cast<std::uint32_t>(totalBytes), output);

    std::array<char, 64 * 1024> buffer{};
    for (const auto &chunkPath : job.chunkPaths) {
      std::ifstream chunk(chunkPath, std::ios::binary);
      if (!chunk.good()) {
        throw std::runtime_error("Missing synthesized chunk");
      }
      while (chunk.good()) {
        chunk.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = chunk.gcount();
        if (count > 0) {
          output.write(buffer.data(), count);
        }
      }
    }
  }

  void cleanupJob(JobState &job) {
    const auto allocated = job.allocatedTempBytes.exchange(0);
    if (allocated > 0) {
      const auto current = metrics.tempStorageBytes.load();
      if (current >= allocated) {
        metrics.tempStorageBytes.fetch_sub(allocated);
      } else {
        metrics.tempStorageBytes.store(0);
      }
    }

    std::error_code ignored;
    std::filesystem::remove_all(job.tempDir, ignored);
  }

  piper::PiperConfig &piperConfig;
  ModelCache &modelCache;
  const ServerOptions &options;
  ServerMetrics &metrics;
  const std::size_t maxJobs;
  const std::size_t queueSize;
  const std::size_t workerCount;
  mutable std::mutex queueMutex;
  std::condition_variable queueCv;
  std::deque<std::shared_ptr<JobState>> activeRoundRobin;
  std::deque<std::shared_ptr<JobState>> pendingJobs;
  std::vector<std::thread> workers;
  bool stopping = false;
  std::size_t activeJobs = 0;
  std::size_t waitingJobs = 0;
};

json resourcePolicyJson(const ServerOptions &options, const FairTtsScheduler &scheduler) {
  return json{{"mode", "auto"},
              {"profile", options.cpuProfile},
              {"hardware_threads", options.resourcePolicy.hardwareThreads},
              {"memory_bytes", options.resourcePolicy.memoryBytes},
              {"cpu_threads_per_worker", options.cpuThreads.value_or(0)},
              {"max_concurrent_jobs", options.maxConcurrentJobs},
              {"chunk_workers", scheduler.workerTotal()},
              {"max_model_replicas", options.maxModelReplicas},
              {"queue_size", options.queueSize},
              {"queue_timeout_seconds", options.queueTimeoutSeconds},
              {"max_temp_bytes", options.maxTempBytes},
              {"active_jobs", scheduler.activeJobCount()},
              {"waiting_jobs", scheduler.waitingJobCount()}};
}

json metricsJson(const ServerMetrics &metrics, const FairTtsScheduler &scheduler,
                 const ServerOptions &options) {
  return json{{"jobs",
               json{{"active", scheduler.activeJobCount()},
                    {"waiting", scheduler.waitingJobCount()},
                    {"accepted", metrics.acceptedJobs.load()},
                    {"completed", metrics.completedJobs.load()},
                    {"cancelled", metrics.cancelledJobs.load()},
                    {"failed", metrics.failedJobs.load()},
                    {"rejected", metrics.rejectedJobs.load()}}},
              {"chunks",
               json{{"processing", metrics.processingChunks.load()},
                    {"completed", metrics.completedChunks.load()},
                    {"failed", metrics.failedChunks.load()}}},
              {"storage", json{{"temp_bytes", metrics.tempStorageBytes.load()},
                                  {"max_temp_bytes", options.maxTempBytes},
                                  {"output_retention_seconds", options.outputRetentionSeconds}}},
              {"resources", resourcePolicyJson(options, scheduler)},
              {"text_preprocessing",
               json{{"sanitized_inputs", metrics.sanitizedInputs.load()},
                    {"sanitize_warnings", metrics.sanitizeWarnings.load()},
                    {"rejected_inputs", metrics.rejectedTextInputs.load()}}}};
}


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
  if (error == "temp_storage_full") {
    return 507;
  }
  return 500;
}

json modelErrorResponse(const std::string &error) {
  if (error == "invalid_model") {
    return errorResponse("invalid_request",
                         "El nombre del modelo es inválido. Usa solo el nombre del archivo .onnx o .neo dentro de models/.");
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
  if (error == "temp_storage_full") {
    return errorResponse("temp_storage_full",
                         "El servidor alcanzó el límite de almacenamiento temporal para síntesis.");
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

bool isManagedWavFile(const std::filesystem::directory_entry &entry) {
  if (!entry.is_regular_file()) {
    return false;
  }
  return lowerCopy(entry.path().extension().string()) == ".wav";
}

void cleanupTempDirectory(const ServerOptions &options) {
  std::error_code ignored;
  const auto tempDir = options.outputDir / "tmp";
  if (std::filesystem::exists(tempDir, ignored)) {
    std::filesystem::remove_all(tempDir, ignored);
    spdlog::info("{} Temp cleanup completed: dir={}", nowIso8601(), tempDir.filename().string());
  }
  std::filesystem::create_directories(tempDir, ignored);
}

void cleanupExpiredOutputFiles(const ServerOptions &options) {
  if (options.outputRetentionSeconds == 0) {
    return;
  }

  std::error_code ignored;
  if (!std::filesystem::exists(options.outputDir, ignored)) {
    return;
  }

  const auto cutoff = std::filesystem::file_time_type::clock::now() -
                      std::chrono::seconds(options.outputRetentionSeconds);
  std::size_t removed = 0;
  for (const auto &entry : std::filesystem::directory_iterator(options.outputDir, ignored)) {
    if (ignored || !isManagedWavFile(entry)) {
      continue;
    }

    const auto modified = entry.last_write_time(ignored);
    if (ignored) {
      ignored.clear();
      continue;
    }

    if (modified < cutoff) {
      std::filesystem::remove(entry.path(), ignored);
      if (!ignored) {
        ++removed;
      } else {
        ignored.clear();
      }
    }
  }

  if (removed > 0) {
    spdlog::info("{} Output retention cleanup: removed={} retention_seconds={}",
                 nowIso8601(), removed, options.outputRetentionSeconds);
  }
}

void startOutputCleanupThread(ServerOptions options) {
  std::thread([options]() {
    const auto sleepSeconds = std::max<std::size_t>(30,
        std::min<std::size_t>(300, options.outputRetentionSeconds == 0 ? 300 : options.outputRetentionSeconds / 4));
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(sleepSeconds));
      cleanupExpiredOutputFiles(options);
    }
  }).detach();
}

void handleClient(SocketHandle clientSocket, piper::PiperConfig &piperConfig,
                  const ServerOptions &options, ModelCache &modelCache,
                  ModelRegistry &modelRegistry, FairTtsScheduler &scheduler,
                  ServerMetrics &metrics) {
  try {
    auto maybeRequest = readHttpRequest(clientSocket, options.maxInputBytes);
    if (!maybeRequest) {
      closeSocket(clientSocket);
      return;
    }

    const auto &request = *maybeRequest;
    const auto target = parseTarget(request.path);

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

    if ((request.method == "GET") && (target.path == "/api/health")) {
      sendJson(clientSocket, 200,
               successResponse("Servidor Piper activo.",
                               json{{"status", "ok"},
                                    {"model_loaded", true},
                                    {"time", nowIso8601()}}));
      closeSocket(clientSocket);
      return;
    }

    if ((request.method == "GET") && (target.path == "/api/v1/status")) {
      sendJson(clientSocket, 200,
               successResponse("Estado obtenido correctamente.",
                               json{{"server", "piper-neo"},
                                    {"model_loaded", true},
                                    {"active_model", options.activeModelPath.filename().string()},
                                    {"models_dir", options.modelsDir.filename().string()},
                                    {"output_dir", options.outputDir.filename().string()},
                                    {"auth",
                                     json{{"enabled", !options.apiToken.empty()},
                                          {"header", "Authorization: Bearer <token>"}}},
                                    {"limits",
                                     json{{"max_input_bytes", options.maxInputBytes},
                                          {"max_text_chunk_bytes", options.maxTextChunkBytes},
                                          {"text_sanitizer_enabled", true},
                                          {"max_sanitized_text_chars", std::max<std::size_t>(1000, std::min<std::size_t>(50000, options.maxInputBytes))},
                                          {"max_temp_bytes", options.maxTempBytes},
                                          {"output_retention_seconds", options.outputRetentionSeconds},
                                          {"models_refresh_seconds", options.modelsRefreshSeconds}}},
                                    {"resource_policy", resourcePolicyJson(options, scheduler)}}));
      closeSocket(clientSocket);
      return;
    }

    if ((request.method == "GET") && (target.path == "/api/v1/metrics")) {
      sendJson(clientSocket, 200,
               successResponse("Métricas obtenidas correctamente.",
                               metricsJson(metrics, scheduler, options)));
      closeSocket(clientSocket);
      return;
    }

    if ((request.method == "GET") && (target.path == "/api/v1/models")) {
      auto includeMode = queryValue(target, "include").value_or("basic");
      includeMode = lowerCopy(includeMode);
      if (includeMode != "basic" && includeMode != "metadata" && includeMode != "technical") {
        sendJson(clientSocket, 400,
                 errorResponse("invalid_request", "include debe ser basic, metadata o technical."));
        closeSocket(clientSocket);
        return;
      }

      json models = modelRegistry.list(includeMode);
      sendJson(clientSocket, 200,
               successResponse("Modelos listados correctamente.",
                               json{{"total", models.size()},
                                    {"include", includeMode},
                                    {"cached", true},
                                    {"refresh_seconds", options.modelsRefreshSeconds},
                                    {"models", models}}));
      closeSocket(clientSocket);
      return;
    }

    if (auto imageModelName = routeModelImageName(target.path)) {
      if (request.method != "GET") {
        sendJson(clientSocket, 405,
                 errorResponse("method_not_allowed", "Método no permitido."));
        closeSocket(clientSocket);
        return;
      }

      if (!isSafeFileName(*imageModelName)) {
        sendJson(clientSocket, 400,
                 errorResponse("invalid_request", "Nombre de modelo inválido."));
        closeSocket(clientSocket);
        return;
      }

      try {
        auto modelInfo = findModelByName(options, *imageModelName, &modelRegistry);
        if (!modelInfo || !modelInfo->hasConfig) {
          sendJson(clientSocket, 404,
                   errorResponse("not_found", "Modelo o configuración no encontrada."));
          closeSocket(clientSocket);
          return;
        }
        std::string contentType;
        std::string imageBytes;
        if (modelInfo->isNeo) {
          auto neoImage = piper_neo::readImageSection(modelInfo->modelPath);
          contentType = neoImage.first;
          imageBytes = neoImage.second;
        } else {
          auto root = loadJsonFile(modelInfo->configPath);
          if (!modelJsonHasImage(root)) {
            sendJson(clientSocket, 404,
                     errorResponse("not_found", "El modelo no tiene imagen."));
            closeSocket(clientSocket);
            return;
          }
          auto parsedImage = parseDataImage(root["modelcard"]["image"].get<std::string>());
          contentType = parsedImage.first;
          imageBytes = parsedImage.second;
        }
        sendResponse(clientSocket, 200, contentType, imageBytes,
                     {{"Cache-Control", "public, max-age=3600"}});
      } catch (const std::runtime_error &e) {
        const std::string message = e.what();
        if (message == "invalid_image") {
          sendJson(clientSocket, 400,
                   errorResponse("invalid_image", "La imagen del modelo no tiene un formato válido."));
        } else {
          sendJson(clientSocket, 500, errorResponse("server_error", message));
        }
      }
      closeSocket(clientSocket);
      return;
    }

    if (auto fileName = routeFileName(target.path)) {
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

    if ((request.method == "POST") && (target.path == "/api/v1/tts")) {
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

      TtsTextSanitizeResult sanitization;
      const auto maxSafeTextChars = std::max<std::size_t>(1000, std::min<std::size_t>(50000, options.maxInputBytes));
      const auto safeText = sanitizeTtsTextForApi(text, maxSafeTextChars, sanitization);
      if (!sanitization.ok || safeText.empty()) {
        metrics.rejectedTextInputs++;
        sendJson(clientSocket, sanitization.warnings.empty() ? 400 : 422,
                 errorResponse("text_not_pronounceable",
                               "El texto no contiene contenido pronunciable seguro después del filtrado."));
        closeSocket(clientSocket);
        return;
      }

      metrics.sanitizedInputs++;
      metrics.sanitizeWarnings.fetch_add(static_cast<std::uint64_t>(sanitization.warnings.size()));
      if (!sanitization.warnings.empty()) {
        spdlog::info("{} TTS text sanitized: raw_bytes={} speak_bytes={} warnings={} risk={}",
                     nowIso8601(), sanitization.rawBytes, sanitization.speakBytes,
                     sanitization.warnings.size(), sanitization.riskScore);
      }

      if (input.contains("output_file") || input.contains("outputFile")) {
        sendJson(clientSocket, 400,
                 errorResponse("invalid_request",
                               "output_file ya no está soportado. Piper Neo genera nombres seguros automáticamente."));
        closeSocket(clientSocket);
        return;
      }

      std::string fileName = makeOutputFileName();

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

      std::optional<piper::SpeakerId> speakerId;
      if (input.contains("speaker_id") && input["speaker_id"].is_number_integer()) {
        speakerId = input["speaker_id"].get<piper::SpeakerId>();
      }

      std::filesystem::create_directories(options.outputDir);
      const auto outputPath = options.outputDir / fileName;
      auto shouldCancel = [&clientSocket]() {
        return clientDisconnected(clientSocket);
      };

      try {
        TtsJobRequest jobRequest;
        jobRequest.text = safeText;
        jobRequest.fileName = fileName;
        jobRequest.outputPath = outputPath;
        jobRequest.requestedModel = requestedModel;
        jobRequest.speakerId = speakerId;
        jobRequest.shouldCancel = shouldCancel;

        auto result = scheduler.synthesize(jobRequest);

        sendJson(clientSocket, 201,
                 successResponse("Audio generado exitosamente.",
                                 json{{"file", fileName},
                                      {"model", result.modelName},
                                      {"url", "/api/v1/files/" + fileName},
                                      {"format", "wav"},
                                      {"chunks", result.chunks},
                                      {"bytes", result.bytes},
                                      {"audio_seconds", result.synthesis.audioSeconds},
                                      {"infer_seconds", result.synthesis.inferSeconds},
                                      {"real_time_factor", result.synthesis.realTimeFactor},
                                      {"text_preprocessing", ttsSanitizeResultToJson(sanitization)}}));
      } catch (const std::runtime_error &e) {
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
        sendJson(clientSocket, errorStatusForException(message),
                 modelErrorResponse(message));
      } catch (const std::exception &e) {
        std::error_code ignored;
        std::filesystem::remove(outputPath, ignored);
        sendJson(clientSocket, 500,
                 errorResponse("synthesis_error", e.what()));
      }

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
    const auto ext = lowerCopy(path.extension().string());
    if (ext == ".onnx") {
      auto configPath = std::filesystem::path(path.string() + ".json");
      models.push_back(ModelInfo{path.filename().string(), "onnx", path, configPath,
                                 std::filesystem::exists(configPath), false});
    } else if (ext == ".neo") {
      models.push_back(ModelInfo{path.filename().string(), "neo", path, {}, true, true});
    }
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
               ServerOptions options) {
  std::filesystem::create_directories(options.modelsDir);
  std::filesystem::create_directories(options.outputDir);
  cleanupTempDirectory(options);
  cleanupExpiredOutputFiles(options);
  startOutputCleanupThread(options);

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

  unsigned int hardwareThreads = options.detectedHardwareThreads;
  if (hardwareThreads == 0) {
    hardwareThreads = std::thread::hardware_concurrency();
  }
  if (hardwareThreads == 0) {
    hardwareThreads = 1;
  }
  options.resourcePolicy.profile = options.cpuProfile;
  options.resourcePolicy.autoConfigured = true;
  options.resourcePolicy.hardwareThreads = hardwareThreads;
  options.resourcePolicy.memoryBytes = options.detectedMemoryBytes;
  options.resourcePolicy.cpuThreadsPerWorker = static_cast<std::size_t>(options.cpuThreads.value_or(1));
  options.resourcePolicy.maxConcurrentJobs = options.maxConcurrentJobs;
  options.resourcePolicy.chunkWorkers = options.chunkWorkers;
  options.resourcePolicy.maxModelReplicas = options.maxModelReplicas;
  options.resourcePolicy.queueSize = options.queueSize;
  options.resourcePolicy.queueTimeoutSeconds = options.queueTimeoutSeconds;
  options.resourcePolicy.maxTempBytes = options.maxTempBytes;

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
  spdlog::info("Resource policy: profile={} detected_threads={} detected_memory_mb={} cpu_threads={} chunk_workers={} max_jobs={} queue_size={} replicas={} max_temp_bytes={}",
               options.cpuProfile, hardwareThreads,
               options.detectedMemoryBytes == 0 ? 0 : options.detectedMemoryBytes / (1024ULL * 1024ULL),
               options.cpuThreads.value_or(0), options.chunkWorkers,
               options.maxConcurrentJobs, options.queueSize, options.maxModelReplicas,
               options.maxTempBytes);

  ModelRegistry modelRegistry(options);
  modelRegistry.forceRefresh();
  ModelCache modelCache(piperConfig, voice, options, modelRegistry);
  ServerMetrics metrics;
  FairTtsScheduler scheduler(piperConfig, modelCache, options, metrics);

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
                std::ref(modelCache), std::ref(modelRegistry),
                std::ref(scheduler), std::ref(metrics))
        .detach();
  }

  closeSocket(serverSocket);
#ifdef _WIN32
  WSACleanup();
#endif
}

} // namespace piper_server
