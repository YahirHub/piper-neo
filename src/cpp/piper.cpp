#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>

#include <espeak-ng/speak_lib.h>
#include <onnxruntime_cxx_api.h>
#include <spdlog/spdlog.h>

#include "json.hpp"
#include "piper.hpp"
#include "utf8.h"
#include "wavfile.hpp"

namespace piper {

#ifdef _PIPER_VERSION
// https://stackoverflow.com/questions/47346133/how-to-use-a-define-inside-a-format-string
#define _STR(x) #x
#define STR(x) _STR(x)
const std::string VERSION = STR(_PIPER_VERSION);
#else
const std::string VERSION = "";
#endif

// Maximum value for 16-bit signed WAV sample
const float MAX_WAV_VALUE = 32767.0f;

const std::string instanceName{"piper"};

constexpr std::size_t DEFAULT_TEXT_CHUNK_BYTES = 4096;
constexpr std::size_t TEXT_LOG_PREVIEW_BYTES = 256;

void throwIfSynthesisCancelled(const std::function<bool()> &shouldCancel) {
  if (shouldCancel && shouldCancel()) {
    throw std::runtime_error("synthesis_cancelled");
  }
}

std::string previewTextForLog(const std::string &text,
                              std::size_t maxBytes = TEXT_LOG_PREVIEW_BYTES) {
  if (text.size() <= maxBytes) {
    return text;
  }

  std::size_t end = maxBytes;
  while ((end > 0) && ((static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)) {
    --end;
  }

  if (end == 0) {
    end = maxBytes;
  }

  std::stringstream preview;
  preview << text.substr(0, end) << "... (" << text.size() << " byte(s))";
  return preview.str();
}

bool isUtf8ContinuationByte(char value) {
  return (static_cast<unsigned char>(value) & 0xC0) == 0x80;
}

std::size_t clampToUtf8Boundary(const std::string &text, std::size_t index) {
  if (index >= text.size()) {
    return text.size();
  }

  while ((index > 0) && isUtf8ContinuationByte(text[index])) {
    --index;
  }

  return index;
}

bool isAsciiWhitespace(char value) {
  return (value == ' ') || (value == '\n') || (value == '\r') ||
         (value == '\t') || (value == '\v') || (value == '\f');
}

bool startsWithBytes(const std::string &text, std::size_t index,
                     std::initializer_list<unsigned char> bytes) {
  if ((index + bytes.size()) > text.size()) {
    return false;
  }

  std::size_t offset = 0;
  for (auto expected : bytes) {
    if (static_cast<unsigned char>(text[index + offset]) != expected) {
      return false;
    }
    ++offset;
  }

  return true;
}

bool isClosingSpanishQuestion(const std::string &text, std::size_t index) {
  return text[index] == '?';
}

bool isClosingSpanishExclamation(const std::string &text, std::size_t index) {
  return text[index] == '!';
}

bool isOpeningSpanishQuestion(const std::string &text, std::size_t index) {
  // UTF-8: ¿ = C2 BF
  return startsWithBytes(text, index, {0xC2, 0xBF});
}

bool isOpeningSpanishExclamation(const std::string &text, std::size_t index) {
  // UTF-8: ¡ = C2 A1
  return startsWithBytes(text, index, {0xC2, 0xA1});
}

bool isUtf8Ellipsis(const std::string &text, std::size_t index) {
  // UTF-8: … = E2 80 A6
  return startsWithBytes(text, index, {0xE2, 0x80, 0xA6});
}

bool isClosingQuoteOrBracket(char value) {
  return (value == '"') || (value == '\'') || (value == ')') ||
         (value == ']') || (value == '}') || (value == '>');
}

std::size_t skipClosingMarksForward(const std::string &text,
                                    std::size_t index,
                                    std::size_t limit) {
  while (index < limit) {
    if (isClosingQuoteOrBracket(text[index])) {
      ++index;
      continue;
    }

    // UTF-8: ” = E2 80 9D, ’ = E2 80 99, » = C2 BB
    if (startsWithBytes(text, index, {0xE2, 0x80, 0x9D}) ||
        startsWithBytes(text, index, {0xE2, 0x80, 0x99})) {
      index += 3;
      continue;
    }

    if (startsWithBytes(text, index, {0xC2, 0xBB})) {
      index += 2;
      continue;
    }

    break;
  }

  return index;
}

bool hasBoundaryAfter(const std::string &text, std::size_t splitAt) {
  splitAt = skipClosingMarksForward(text, splitAt, text.size());
  return (splitAt >= text.size()) || isAsciiWhitespace(text[splitAt]);
}

bool isStrongSentenceBoundary(const std::string &text, std::size_t index,
                              std::size_t &splitAt) {
  char value = text[index];
  if ((value == '.') || (value == '!') || (value == '?')) {
    splitAt = skipClosingMarksForward(text, index + 1, text.size());
    return hasBoundaryAfter(text, index + 1);
  }

  if (isUtf8Ellipsis(text, index)) {
    splitAt = skipClosingMarksForward(text, index + 3, text.size());
    return hasBoundaryAfter(text, index + 3);
  }

  return false;
}

std::size_t findLastParagraphBoundary(const std::string &text,
                                      std::size_t begin,
                                      std::size_t candidate,
                                      std::size_t minBoundary) {
  std::size_t best = std::string::npos;
  for (std::size_t i = begin + 1; i < candidate; ++i) {
    bool paragraphBreak = ((text[i - 1] == '\n') && (text[i] == '\n')) ||
                          ((text[i - 1] == '\r') && (text[i] == '\n'));
    if (paragraphBreak && ((i + 1) >= minBoundary)) {
      best = i + 1;
    }
  }

  return best;
}

std::size_t findLastStrongSentenceBoundary(const std::string &text,
                                           std::size_t begin,
                                           std::size_t candidate,
                                           std::size_t minBoundary) {
  std::size_t best = std::string::npos;
  for (std::size_t i = begin; i < candidate; ++i) {
    std::size_t splitAt = std::string::npos;
    if (isStrongSentenceBoundary(text, i, splitAt) &&
        (splitAt >= minBoundary) && (splitAt <= candidate)) {
      best = splitAt;
    }
  }

  return best;
}

std::size_t findLastSoftBoundary(const std::string &text, std::size_t begin,
                                 std::size_t candidate,
                                 std::size_t minBoundary) {
  std::size_t best = std::string::npos;
  for (std::size_t i = begin; i < candidate; ++i) {
    char value = text[i];
    if ((value == ';') || (value == ':') || (value == ',') ||
        (value == '\n') || (value == '\r') || (value == '\t') ||
        (value == ' ')) {
      std::size_t splitAt = i + 1;
      if (splitAt >= minBoundary) {
        best = splitAt;
      }
    }
  }

  return best;
}

std::size_t findLastWhitespaceBoundary(const std::string &text,
                                       std::size_t begin,
                                       std::size_t candidate,
                                       std::size_t minBoundary) {
  std::size_t best = std::string::npos;
  for (std::size_t i = begin; i < candidate; ++i) {
    if (isAsciiWhitespace(text[i]) && ((i + 1) >= minBoundary)) {
      best = i + 1;
    }
  }

  return best;
}

std::size_t findForwardWhitespaceBoundary(const std::string &text,
                                          std::size_t candidate,
                                          std::size_t hardLimit) {
  for (std::size_t i = candidate; i < hardLimit; ++i) {
    if (isAsciiWhitespace(text[i])) {
      return i + 1;
    }
  }

  return std::string::npos;
}

std::size_t findUnclosedSpanishPairEnd(const std::string &text,
                                       std::size_t begin,
                                       std::size_t candidate,
                                       std::size_t hardLimit) {
  std::size_t questionOpen = std::string::npos;
  std::size_t exclamationOpen = std::string::npos;

  for (std::size_t i = begin; i < candidate; ++i) {
    if (isOpeningSpanishQuestion(text, i)) {
      questionOpen = i;
      ++i;
      continue;
    }

    if (isOpeningSpanishExclamation(text, i)) {
      exclamationOpen = i;
      ++i;
      continue;
    }

    if (isClosingSpanishQuestion(text, i)) {
      questionOpen = std::string::npos;
    }

    if (isClosingSpanishExclamation(text, i)) {
      exclamationOpen = std::string::npos;
    }
  }

  if (questionOpen != std::string::npos) {
    for (std::size_t i = candidate; i < hardLimit; ++i) {
      if (isClosingSpanishQuestion(text, i)) {
        return skipClosingMarksForward(text, i + 1, hardLimit);
      }
    }
  }

  if (exclamationOpen != std::string::npos) {
    for (std::size_t i = candidate; i < hardLimit; ++i) {
      if (isClosingSpanishExclamation(text, i)) {
        return skipClosingMarksForward(text, i + 1, hardLimit);
      }
    }
  }

  return std::string::npos;
}

std::size_t chooseSmartSplitPoint(const std::string &text, std::size_t offset,
                                  std::size_t preferredBytes) {
  const std::size_t remaining = text.size() - offset;
  if (remaining <= preferredBytes) {
    return text.size();
  }

  const std::size_t candidate =
      clampToUtf8Boundary(text, std::min(offset + preferredBytes, text.size()));
  const std::size_t hardLimit = clampToUtf8Boundary(
      text, std::min(offset + (preferredBytes * 2), text.size()));
  const std::size_t minBoundary = offset + std::max<std::size_t>(1, preferredBytes / 3);

  // Spanish questions/exclamations should be kept intact when the closing mark
  // is reasonably close. This avoids chunks like "¿Cuánto cuesta" + "...?".
  auto spanishPairEnd =
      findUnclosedSpanishPairEnd(text, offset, candidate, hardLimit);
  if (spanishPairEnd != std::string::npos && spanishPairEnd > offset) {
    return spanishPairEnd;
  }

  auto paragraph =
      findLastParagraphBoundary(text, offset, candidate, minBoundary);
  if (paragraph != std::string::npos) {
    return paragraph;
  }

  auto sentence =
      findLastStrongSentenceBoundary(text, offset, candidate, minBoundary);
  if (sentence != std::string::npos) {
    return sentence;
  }

  auto soft = findLastSoftBoundary(text, offset, candidate, minBoundary);
  if (soft != std::string::npos) {
    return soft;
  }

  auto whitespace =
      findLastWhitespaceBoundary(text, offset, candidate, minBoundary);
  if (whitespace != std::string::npos) {
    return whitespace;
  }

  // If a normal word crosses the preferred limit, allow a little overrun and
  // cut at the next whitespace. If no whitespace exists, cut at a UTF-8 safe
  // hard boundary to protect RAM/CPU.
  auto forwardWhitespace = findForwardWhitespaceBoundary(text, candidate, hardLimit);
  if (forwardWhitespace != std::string::npos) {
    return forwardWhitespace;
  }

  return hardLimit > offset ? hardLimit : candidate;
}

void trimLeadingAsciiWhitespace(const std::string &text, std::size_t &offset) {
  while ((offset < text.size()) && isAsciiWhitespace(text[offset])) {
    ++offset;
  }
}

std::vector<std::string> splitTextIntoChunks(const std::string &text,
                                             std::size_t maxChunkBytes) {
  if ((maxChunkBytes == 0) || (text.size() <= maxChunkBytes)) {
    return {text};
  }

  std::vector<std::string> chunks;
  std::size_t offset = 0;

  while (offset < text.size()) {
    trimLeadingAsciiWhitespace(text, offset);
    if (offset >= text.size()) {
      break;
    }

    std::size_t splitAt = chooseSmartSplitPoint(text, offset, maxChunkBytes);
    splitAt = clampToUtf8Boundary(text, splitAt);

    if (splitAt <= offset) {
      splitAt = clampToUtf8Boundary(
          text, std::min(offset + maxChunkBytes, text.size()));
    }

    chunks.emplace_back(text.substr(offset, splitAt - offset));
    offset = splitAt;
  }

  return chunks;
}

std::string getVersion() { return VERSION; }

// True if the string is a single UTF-8 codepoint
bool isSingleCodepoint(std::string s) {
  return utf8::distance(s.begin(), s.end()) == 1;
}

// Get the first UTF-8 codepoint of a string
Phoneme getCodepoint(std::string s) {
  utf8::iterator character_iter(s.begin(), s.begin(), s.end());
  return *character_iter;
}

// Load JSON config information for phonemization
void parsePhonemizeConfig(json &configRoot, PhonemizeConfig &phonemizeConfig) {
  // {
  //     "espeak": {
  //         "voice": "<language code>"
  //     },
  //     "phoneme_type": "<espeak or text>",
  //     "phoneme_map": {
  //         "<from phoneme>": ["<to phoneme 1>", "<to phoneme 2>", ...]
  //     },
  //     "phoneme_id_map": {
  //         "<phoneme>": [<id1>, <id2>, ...]
  //     }
  // }

  if (configRoot.contains("espeak")) {
    auto espeakValue = configRoot["espeak"];
    if (espeakValue.contains("voice")) {
      phonemizeConfig.eSpeak.voice = espeakValue["voice"].get<std::string>();
    }
  }

  if (configRoot.contains("phoneme_type")) {
    auto phonemeTypeStr = configRoot["phoneme_type"].get<std::string>();
    if (phonemeTypeStr == "text") {
      phonemizeConfig.phonemeType = TextPhonemes;
    }
  }

  // phoneme to [id] map
  // Maps phonemes to one or more phoneme ids (required).
  if (configRoot.contains("phoneme_id_map")) {
    auto phonemeIdMapValue = configRoot["phoneme_id_map"];
    for (auto &fromPhonemeItem : phonemeIdMapValue.items()) {
      std::string fromPhoneme = fromPhonemeItem.key();
      if (!isSingleCodepoint(fromPhoneme)) {
        std::stringstream idsStr;
        for (auto &toIdValue : fromPhonemeItem.value()) {
          PhonemeId toId = toIdValue.get<PhonemeId>();
          idsStr << toId << ",";
        }

        spdlog::error("\"{}\" is not a single codepoint (ids={})", fromPhoneme,
                      idsStr.str());
        throw std::runtime_error(
            "Phonemes must be one codepoint (phoneme id map)");
      }

      auto fromCodepoint = getCodepoint(fromPhoneme);
      for (auto &toIdValue : fromPhonemeItem.value()) {
        PhonemeId toId = toIdValue.get<PhonemeId>();
        phonemizeConfig.phonemeIdMap[fromCodepoint].push_back(toId);
      }
    }
  }

  // phoneme to [phoneme] map
  // Maps phonemes to one or more other phonemes (not normally used).
  if (configRoot.contains("phoneme_map")) {
    if (!phonemizeConfig.phonemeMap) {
      phonemizeConfig.phonemeMap.emplace();
    }

    auto phonemeMapValue = configRoot["phoneme_map"];
    for (auto &fromPhonemeItem : phonemeMapValue.items()) {
      std::string fromPhoneme = fromPhonemeItem.key();
      if (!isSingleCodepoint(fromPhoneme)) {
        spdlog::error("\"{}\" is not a single codepoint", fromPhoneme);
        throw std::runtime_error(
            "Phonemes must be one codepoint (phoneme map)");
      }

      auto fromCodepoint = getCodepoint(fromPhoneme);
      for (auto &toPhonemeValue : fromPhonemeItem.value()) {
        std::string toPhoneme = toPhonemeValue.get<std::string>();
        if (!isSingleCodepoint(toPhoneme)) {
          throw std::runtime_error(
              "Phonemes must be one codepoint (phoneme map)");
        }

        auto toCodepoint = getCodepoint(toPhoneme);
        (*phonemizeConfig.phonemeMap)[fromCodepoint].push_back(toCodepoint);
      }
    }
  }

} /* parsePhonemizeConfig */

// Load JSON config for audio synthesis
void parseSynthesisConfig(json &configRoot, SynthesisConfig &synthesisConfig) {
  // {
  //     "audio": {
  //         "sample_rate": 22050
  //     },
  //     "inference": {
  //         "noise_scale": 0.667,
  //         "length_scale": 1,
  //         "noise_w": 0.8,
  //         "phoneme_silence": {
  //           "<phoneme>": <seconds of silence>,
  //           ...
  //         }
  //     }
  // }

  if (configRoot.contains("audio")) {
    auto audioValue = configRoot["audio"];
    if (audioValue.contains("sample_rate")) {
      // Default sample rate is 22050 Hz
      synthesisConfig.sampleRate = audioValue.value("sample_rate", 22050);
    }
  }

  if (configRoot.contains("inference")) {
    // Overrides default inference settings
    auto inferenceValue = configRoot["inference"];
    if (inferenceValue.contains("noise_scale")) {
      synthesisConfig.noiseScale = inferenceValue.value("noise_scale", 0.667f);
    }

    if (inferenceValue.contains("length_scale")) {
      synthesisConfig.lengthScale = inferenceValue.value("length_scale", 1.0f);
    }

    if (inferenceValue.contains("noise_w")) {
      synthesisConfig.noiseW = inferenceValue.value("noise_w", 0.8f);
    }

    if (inferenceValue.contains("phoneme_silence")) {
      // phoneme -> seconds of silence to add after
      synthesisConfig.phonemeSilenceSeconds.emplace();
      auto phonemeSilenceValue = inferenceValue["phoneme_silence"];
      for (auto &phonemeItem : phonemeSilenceValue.items()) {
        std::string phonemeStr = phonemeItem.key();
        if (!isSingleCodepoint(phonemeStr)) {
          spdlog::error("\"{}\" is not a single codepoint", phonemeStr);
          throw std::runtime_error(
              "Phonemes must be one codepoint (phoneme silence)");
        }

        auto phoneme = getCodepoint(phonemeStr);
        (*synthesisConfig.phonemeSilenceSeconds)[phoneme] =
            phonemeItem.value().get<float>();
      }

    } // if phoneme_silence

  } // if inference

} /* parseSynthesisConfig */

void parseModelConfig(json &configRoot, ModelConfig &modelConfig) {

  modelConfig.numSpeakers = configRoot["num_speakers"].get<SpeakerId>();

  if (configRoot.contains("speaker_id_map")) {
    if (!modelConfig.speakerIdMap) {
      modelConfig.speakerIdMap.emplace();
    }

    auto speakerIdMapValue = configRoot["speaker_id_map"];
    for (auto &speakerItem : speakerIdMapValue.items()) {
      std::string speakerName = speakerItem.key();
      (*modelConfig.speakerIdMap)[speakerName] =
          speakerItem.value().get<SpeakerId>();
    }
  }

} /* parseModelConfig */

void initialize(PiperConfig &config) {
  if (config.useESpeak) {
    // Set up espeak-ng for calling espeak_TextToPhonemesWithTerminator
    // See: https://github.com/rhasspy/espeak-ng
    spdlog::debug("Initializing eSpeak");
    int result = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS,
                                   /*buflength*/ 0,
                                   /*path*/ config.eSpeakDataPath.c_str(),
                                   /*options*/ 0);
    if (result < 0) {
      throw std::runtime_error("Failed to initialize eSpeak-ng");
    }

    spdlog::debug("Initialized eSpeak");
  }

  // Load onnx model for libtashkeel
  // https://github.com/mush42/libtashkeel/
  if (config.useTashkeel) {
    spdlog::debug("Using libtashkeel for diacritization");
    if (!config.tashkeelModelPath) {
      throw std::runtime_error("No path to libtashkeel model");
    }

    spdlog::debug("Loading libtashkeel model from {}",
                  config.tashkeelModelPath.value());
    config.tashkeelState = std::make_unique<tashkeel::State>();
    tashkeel::tashkeel_load(config.tashkeelModelPath.value(),
                            *config.tashkeelState);
    spdlog::debug("Initialized libtashkeel");
  }

  spdlog::info("Initialized piper");
}

void terminate(PiperConfig &config) {
  if (config.useESpeak) {
    // Clean up espeak-ng
    spdlog::debug("Terminating eSpeak");
    espeak_Terminate();
    spdlog::debug("Terminated eSpeak");
  }

  spdlog::info("Terminated piper");
}

void loadModel(std::string modelPath, ModelSession &session, bool useCuda,
               std::optional<int> cpuThreads) {
  spdlog::debug("Loading onnx model from {}", modelPath);
  session.env = Ort::Env(OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING,
                         instanceName.c_str());
  session.env.DisableTelemetryEvents();

  if (useCuda) {
    // Use CUDA provider
    OrtCUDAProviderOptions cuda_options{};
    cuda_options.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchHeuristic;
    session.options.AppendExecutionProvider_CUDA(cuda_options);
  }

  if (cpuThreads && (cpuThreads.value() > 0)) {
    session.options.SetIntraOpNumThreads(cpuThreads.value());
    session.options.SetInterOpNumThreads(1);
    spdlog::info("ONNX Runtime CPU threads limited to {}", cpuThreads.value());
  }

  // Slows down performance by ~2x
  // session.options.SetIntraOpNumThreads(1);

  // Roughly doubles load time for no visible inference benefit
  // session.options.SetGraphOptimizationLevel(
  //     GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

  session.options.SetGraphOptimizationLevel(
      GraphOptimizationLevel::ORT_DISABLE_ALL);

  // Slows down performance very slightly
  // session.options.SetExecutionMode(ExecutionMode::ORT_PARALLEL);

  session.options.DisableCpuMemArena();
  session.options.DisableMemPattern();
  session.options.DisableProfiling();

  auto startTime = std::chrono::steady_clock::now();

#ifdef _WIN32
  auto modelPathW = std::wstring(modelPath.begin(), modelPath.end());
  auto modelPathStr = modelPathW.c_str();
#else
  auto modelPathStr = modelPath.c_str();
#endif

  session.onnx = Ort::Session(session.env, modelPathStr, session.options);

  auto endTime = std::chrono::steady_clock::now();
  spdlog::debug("Loaded onnx model in {} second(s)",
                std::chrono::duration<double>(endTime - startTime).count());
}

// Load Onnx model and JSON config file
void loadVoice(PiperConfig &config, std::string modelPath,
               std::string modelConfigPath, Voice &voice,
               std::optional<SpeakerId> &speakerId, bool useCuda,
               std::optional<int> cpuThreads) {
  spdlog::debug("Parsing voice config at {}", modelConfigPath);
  std::ifstream modelConfigFile(modelConfigPath);
  voice.configRoot = json::parse(modelConfigFile);

  parsePhonemizeConfig(voice.configRoot, voice.phonemizeConfig);
  parseSynthesisConfig(voice.configRoot, voice.synthesisConfig);
  parseModelConfig(voice.configRoot, voice.modelConfig);

  if (voice.modelConfig.numSpeakers > 1) {
    // Multi-speaker model
    if (speakerId) {
      voice.synthesisConfig.speakerId = speakerId;
    } else {
      // Default speaker
      voice.synthesisConfig.speakerId = 0;
    }
  }

  spdlog::debug("Voice contains {} speaker(s)", voice.modelConfig.numSpeakers);

  loadModel(modelPath, voice.session, useCuda, cpuThreads);

} /* loadVoice */

// Phoneme ids to WAV audio
void synthesize(std::vector<PhonemeId> &phonemeIds,
                SynthesisConfig &synthesisConfig, ModelSession &session,
                std::vector<int16_t> &audioBuffer, SynthesisResult &result) {
  spdlog::debug("Synthesizing audio for {} phoneme id(s)", phonemeIds.size());

  auto memoryInfo = Ort::MemoryInfo::CreateCpu(
      OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);

  // Allocate
  std::vector<int64_t> phonemeIdLengths{(int64_t)phonemeIds.size()};
  std::vector<float> scales{synthesisConfig.noiseScale,
                            synthesisConfig.lengthScale,
                            synthesisConfig.noiseW};

  std::vector<Ort::Value> inputTensors;
  std::vector<int64_t> phonemeIdsShape{1, (int64_t)phonemeIds.size()};
  inputTensors.push_back(Ort::Value::CreateTensor<int64_t>(
      memoryInfo, phonemeIds.data(), phonemeIds.size(), phonemeIdsShape.data(),
      phonemeIdsShape.size()));

  std::vector<int64_t> phomemeIdLengthsShape{(int64_t)phonemeIdLengths.size()};
  inputTensors.push_back(Ort::Value::CreateTensor<int64_t>(
      memoryInfo, phonemeIdLengths.data(), phonemeIdLengths.size(),
      phomemeIdLengthsShape.data(), phomemeIdLengthsShape.size()));

  std::vector<int64_t> scalesShape{(int64_t)scales.size()};
  inputTensors.push_back(
      Ort::Value::CreateTensor<float>(memoryInfo, scales.data(), scales.size(),
                                      scalesShape.data(), scalesShape.size()));

  // Add speaker id.
  // NOTE: These must be kept outside the "if" below to avoid being deallocated.
  std::vector<int64_t> speakerId{
      (int64_t)synthesisConfig.speakerId.value_or(0)};
  std::vector<int64_t> speakerIdShape{(int64_t)speakerId.size()};

  if (synthesisConfig.speakerId) {
    inputTensors.push_back(Ort::Value::CreateTensor<int64_t>(
        memoryInfo, speakerId.data(), speakerId.size(), speakerIdShape.data(),
        speakerIdShape.size()));
  }

  // From export_onnx.py
  std::array<const char *, 4> inputNames = {"input", "input_lengths", "scales",
                                            "sid"};
  std::array<const char *, 1> outputNames = {"output"};

  // Infer
  auto startTime = std::chrono::steady_clock::now();
  auto outputTensors = session.onnx.Run(
      Ort::RunOptions{nullptr}, inputNames.data(), inputTensors.data(),
      inputTensors.size(), outputNames.data(), outputNames.size());
  auto endTime = std::chrono::steady_clock::now();

  if ((outputTensors.size() != 1) || (!outputTensors.front().IsTensor())) {
    throw std::runtime_error("Invalid output tensors");
  }
  auto inferDuration = std::chrono::duration<double>(endTime - startTime);
  result.inferSeconds = inferDuration.count();

  const float *audio = outputTensors.front().GetTensorData<float>();
  auto audioShape =
      outputTensors.front().GetTensorTypeAndShapeInfo().GetShape();
  int64_t audioCount = audioShape[audioShape.size() - 1];

  result.audioSeconds = (double)audioCount / (double)synthesisConfig.sampleRate;
  result.realTimeFactor = 0.0;
  if (result.audioSeconds > 0) {
    result.realTimeFactor = result.inferSeconds / result.audioSeconds;
  }
  spdlog::debug("Synthesized {} second(s) of audio in {} second(s)",
                result.audioSeconds, result.inferSeconds);

  // Get max audio value for scaling
  float maxAudioValue = 0.01f;
  for (int64_t i = 0; i < audioCount; i++) {
    float audioValue = abs(audio[i]);
    if (audioValue > maxAudioValue) {
      maxAudioValue = audioValue;
    }
  }

  // We know the size up front
  audioBuffer.reserve(audioBuffer.size() + audioCount);

  // Scale audio to fill range and convert to int16
  float audioScale = (MAX_WAV_VALUE / std::max(0.01f, maxAudioValue));
  for (int64_t i = 0; i < audioCount; i++) {
    int16_t intAudioValue = static_cast<int16_t>(
        std::clamp(audio[i] * audioScale,
                   static_cast<float>(std::numeric_limits<int16_t>::min()),
                   static_cast<float>(std::numeric_limits<int16_t>::max())));

    audioBuffer.push_back(intAudioValue);
  }

  // Clean up
  for (std::size_t i = 0; i < outputTensors.size(); i++) {
    Ort::detail::OrtRelease(outputTensors[i].release());
  }

  for (std::size_t i = 0; i < inputTensors.size(); i++) {
    Ort::detail::OrtRelease(inputTensors[i].release());
  }
}

// ----------------------------------------------------------------------------

// Phonemize text and synthesize audio
void textToAudio(PiperConfig &config, Voice &voice, std::string text,
                 std::vector<int16_t> &audioBuffer, SynthesisResult &result,
                 const std::function<void()> &audioCallback,
                 const std::function<bool()> &shouldCancel) {

  throwIfSynthesisCancelled(shouldCancel);

  std::size_t sentenceSilenceSamples = 0;
  if (voice.synthesisConfig.sentenceSilenceSeconds > 0) {
    sentenceSilenceSamples = (std::size_t)(
        voice.synthesisConfig.sentenceSilenceSeconds *
        voice.synthesisConfig.sampleRate * voice.synthesisConfig.channels);
  }

  if (config.useTashkeel) {
    if (!config.tashkeelState) {
      throw std::runtime_error("Tashkeel model is not loaded");
    }

    spdlog::debug("Diacritizing text with libtashkeel: {}", previewTextForLog(text));
    text = tashkeel::tashkeel_run(text, *config.tashkeelState);
  }

  throwIfSynthesisCancelled(shouldCancel);

  // Phonemes for each sentence
  spdlog::debug("Phonemizing text: {}", previewTextForLog(text));
  std::vector<std::vector<Phoneme>> phonemes;

  if (voice.phonemizeConfig.phonemeType == eSpeakPhonemes) {
    // Use espeak-ng for phonemization
    eSpeakPhonemeConfig eSpeakConfig;
    eSpeakConfig.voice = voice.phonemizeConfig.eSpeak.voice;
    phonemize_eSpeak(text, eSpeakConfig, phonemes);
  } else {
    // Use UTF-8 codepoints as "phonemes"
    CodepointsPhonemeConfig codepointsConfig;
    phonemize_codepoints(text, codepointsConfig, phonemes);
  }

  // Synthesize each sentence independently.
  std::vector<PhonemeId> phonemeIds;
  std::map<Phoneme, std::size_t> missingPhonemes;
  for (auto phonemesIter = phonemes.begin(); phonemesIter != phonemes.end();
       ++phonemesIter) {
    throwIfSynthesisCancelled(shouldCancel);
    std::vector<Phoneme> &sentencePhonemes = *phonemesIter;

    if (spdlog::should_log(spdlog::level::debug)) {
      // DEBUG log for phonemes
      std::string phonemesStr;
      for (auto phoneme : sentencePhonemes) {
        utf8::append(phoneme, std::back_inserter(phonemesStr));
      }

      spdlog::debug("Converting {} phoneme(s) to ids: {}",
                    sentencePhonemes.size(), phonemesStr);
    }

    std::vector<std::shared_ptr<std::vector<Phoneme>>> phrasePhonemes;
    std::vector<SynthesisResult> phraseResults;
    std::vector<size_t> phraseSilenceSamples;

    // Use phoneme/id map from config
    PhonemeIdConfig idConfig;
    idConfig.phonemeIdMap =
        std::make_shared<PhonemeIdMap>(voice.phonemizeConfig.phonemeIdMap);

    if (voice.synthesisConfig.phonemeSilenceSeconds) {
      // Split into phrases
      std::map<Phoneme, float> &phonemeSilenceSeconds =
          *voice.synthesisConfig.phonemeSilenceSeconds;

      auto currentPhrasePhonemes = std::make_shared<std::vector<Phoneme>>();
      phrasePhonemes.push_back(currentPhrasePhonemes);

      for (auto sentencePhonemesIter = sentencePhonemes.begin();
           sentencePhonemesIter != sentencePhonemes.end();
           sentencePhonemesIter++) {
        Phoneme &currentPhoneme = *sentencePhonemesIter;
        currentPhrasePhonemes->push_back(currentPhoneme);

        if (phonemeSilenceSeconds.count(currentPhoneme) > 0) {
          // Split at phrase boundary
          phraseSilenceSamples.push_back(
              (std::size_t)(phonemeSilenceSeconds[currentPhoneme] *
                            voice.synthesisConfig.sampleRate *
                            voice.synthesisConfig.channels));

          currentPhrasePhonemes = std::make_shared<std::vector<Phoneme>>();
          phrasePhonemes.push_back(currentPhrasePhonemes);
        }
      }
    } else {
      // Use all phonemes
      phrasePhonemes.push_back(
          std::make_shared<std::vector<Phoneme>>(sentencePhonemes));
    }

    // Ensure results/samples are the same size
    while (phraseResults.size() < phrasePhonemes.size()) {
      phraseResults.emplace_back();
    }

    while (phraseSilenceSamples.size() < phrasePhonemes.size()) {
      phraseSilenceSamples.push_back(0);
    }

    // phonemes -> ids -> audio
    for (size_t phraseIdx = 0; phraseIdx < phrasePhonemes.size(); phraseIdx++) {
      throwIfSynthesisCancelled(shouldCancel);
      if (phrasePhonemes[phraseIdx]->size() <= 0) {
        continue;
      }

      // phonemes -> ids
      phonemes_to_ids(*(phrasePhonemes[phraseIdx]), idConfig, phonemeIds,
                      missingPhonemes);
      if (spdlog::should_log(spdlog::level::debug)) {
        // DEBUG log for phoneme ids
        std::stringstream phonemeIdsStr;
        for (auto phonemeId : phonemeIds) {
          phonemeIdsStr << phonemeId << ", ";
        }

        spdlog::debug("Converted {} phoneme(s) to {} phoneme id(s): {}",
                      phrasePhonemes[phraseIdx]->size(), phonemeIds.size(),
                      phonemeIdsStr.str());
      }

      // ids -> audio
      synthesize(phonemeIds, voice.synthesisConfig, voice.session, audioBuffer,
                 phraseResults[phraseIdx]);
      throwIfSynthesisCancelled(shouldCancel);

      // Add end of phrase silence
      for (std::size_t i = 0; i < phraseSilenceSamples[phraseIdx]; i++) {
        audioBuffer.push_back(0);
      }

      result.audioSeconds += phraseResults[phraseIdx].audioSeconds;
      result.inferSeconds += phraseResults[phraseIdx].inferSeconds;

      phonemeIds.clear();
    }

    // Add end of sentence silence
    if (sentenceSilenceSamples > 0) {
      for (std::size_t i = 0; i < sentenceSilenceSamples; i++) {
        audioBuffer.push_back(0);
      }
    }

    throwIfSynthesisCancelled(shouldCancel);

    if (audioCallback) {
      // Call back must copy audio since it is cleared afterwards.
      audioCallback();
      audioBuffer.clear();
    }

    phonemeIds.clear();
  }

  if (missingPhonemes.size() > 0) {
    spdlog::warn("Missing {} phoneme(s) from phoneme/id map!",
                 missingPhonemes.size());

    for (auto phonemeCount : missingPhonemes) {
      std::string phonemeStr;
      utf8::append(phonemeCount.first, std::back_inserter(phonemeStr));
      spdlog::warn("Missing \"{}\" (\\u{:04X}): {} time(s)", phonemeStr,
                   (uint32_t)phonemeCount.first, phonemeCount.second);
    }
  }

  if (result.audioSeconds > 0) {
    result.realTimeFactor = result.inferSeconds / result.audioSeconds;
  }

} /* textToAudio */

void writeStreamingWav(PiperConfig &config, Voice &voice,
                       const std::vector<std::string> &textChunks,
                       std::ostream &audioFile, SynthesisResult &result,
                       const std::function<bool()> &shouldCancel) {
  auto synthesisConfig = voice.synthesisConfig;
  auto headerPosition = audioFile.tellp();
  const bool isSeekable = (headerPosition != std::streampos(-1));
  const std::uint32_t placeholderDataSize =
      isSeekable ? 0
                 : (std::numeric_limits<std::uint32_t>::max() -
                    static_cast<std::uint32_t>(sizeof(WavHeader)) + 8);

  // Placeholder header. It is patched with the final size for seekable streams.
  // For stdout/pipes we use a maximum RIFF size and recommend --output_raw.
  writeWavHeaderBytes(synthesisConfig.sampleRate, synthesisConfig.sampleWidth,
                      synthesisConfig.channels, placeholderDataSize, audioFile);

  std::uint64_t totalAudioBytes = 0;

  for (const auto &textChunk : textChunks) {
    throwIfSynthesisCancelled(shouldCancel);
    if (textChunk.empty()) {
      continue;
    }

    std::vector<int16_t> audioBuffer;
    auto audioCallback = [&audioFile, &audioBuffer, &totalAudioBytes, &shouldCancel]() {
      throwIfSynthesisCancelled(shouldCancel);
      if (audioBuffer.empty()) {
        return;
      }

      const std::size_t audioBytes = sizeof(int16_t) * audioBuffer.size();
      audioFile.write(reinterpret_cast<const char *>(audioBuffer.data()),
                      audioBytes);
      totalAudioBytes += audioBytes;
    };

    textToAudio(config, voice, textChunk, audioBuffer, result, audioCallback,
                shouldCancel);
  }

  const auto endPosition = audioFile.tellp();
  if ((headerPosition != std::streampos(-1)) &&
      (endPosition != std::streampos(-1))) {
    const std::uint64_t maxWavBytes = std::numeric_limits<std::uint32_t>::max();
    if (totalAudioBytes > maxWavBytes) {
      throw std::runtime_error(
          "Generated WAV exceeds 4 GiB. Use --output_raw for very large audio.");
    }

    audioFile.seekp(headerPosition);
    writeWavHeaderBytes(synthesisConfig.sampleRate, synthesisConfig.sampleWidth,
                        synthesisConfig.channels,
                        static_cast<std::uint32_t>(totalAudioBytes), audioFile);
    audioFile.seekp(endPosition);
  } else {
    spdlog::warn("WAV stream is not seekable; wrote a best-effort streaming WAV "
                 "header. Use --output_raw for unbounded streaming output.");
  }
}

// Phonemize text and synthesize audio to WAV file
void textToWavFile(PiperConfig &config, Voice &voice, std::string text,
                   std::ostream &audioFile, SynthesisResult &result,
                   std::size_t maxChunkBytes,
                   const std::function<bool()> &shouldCancel) {
  if (maxChunkBytes == 0) {
    maxChunkBytes = DEFAULT_TEXT_CHUNK_BYTES;
  }

  throwIfSynthesisCancelled(shouldCancel);
  auto chunks = splitTextIntoChunks(text, maxChunkBytes);
  writeStreamingWav(config, voice, chunks, audioFile, result, shouldCancel);

} /* textToWavFile */

void textToWavFileFromStream(PiperConfig &config, Voice &voice,
                             std::istream &textStream,
                             std::ostream &audioFile, SynthesisResult &result,
                             std::size_t maxChunkBytes,
                             const std::function<bool()> &shouldCancel) {
  if (maxChunkBytes == 0) {
    maxChunkBytes = DEFAULT_TEXT_CHUNK_BYTES;
  }

  auto synthesisConfig = voice.synthesisConfig;
  auto headerPosition = audioFile.tellp();
  const bool isSeekable = (headerPosition != std::streampos(-1));
  const std::uint32_t placeholderDataSize =
      isSeekable ? 0
                 : (std::numeric_limits<std::uint32_t>::max() -
                    static_cast<std::uint32_t>(sizeof(WavHeader)) + 8);

  writeWavHeaderBytes(synthesisConfig.sampleRate, synthesisConfig.sampleWidth,
                      synthesisConfig.channels, placeholderDataSize, audioFile);

  auto synthesizeChunk = [&config, &voice, &audioFile, &result, &shouldCancel](
                             const std::string &textChunk,
                             std::uint64_t &totalAudioBytes) {
    throwIfSynthesisCancelled(shouldCancel);
    if (textChunk.empty()) {
      return;
    }

    std::vector<int16_t> audioBuffer;
    auto audioCallback = [&audioFile, &audioBuffer, &totalAudioBytes, &shouldCancel]() {
      throwIfSynthesisCancelled(shouldCancel);
      if (audioBuffer.empty()) {
        return;
      }

      const std::size_t audioBytes = sizeof(int16_t) * audioBuffer.size();
      audioFile.write(reinterpret_cast<const char *>(audioBuffer.data()),
                      audioBytes);
      totalAudioBytes += audioBytes;
    };

    textToAudio(config, voice, textChunk, audioBuffer, result, audioCallback,
                shouldCancel);
  };

  std::uint64_t totalAudioBytes = 0;
  std::string pending;
  std::string line;
  bool firstLine = true;

  while (std::getline(textStream, line)) {
    throwIfSynthesisCancelled(shouldCancel);
    if (firstLine) {
      firstLine = false;
      if (startsWithBytes(line, 0, {0xEF, 0xBB, 0xBF})) {
        line.erase(0, 3);
      }
    }

    pending.append(line);
    pending.push_back('\n');

    while (pending.size() > (maxChunkBytes * 2)) {
      auto chunks = splitTextIntoChunks(pending, maxChunkBytes);
      if (chunks.empty()) {
        break;
      }

      // Keep the last chunk as carry because it may be an unfinished sentence
      // or question that continues on the next line.
      const std::size_t chunksToSynthesize =
          chunks.size() > 1 ? chunks.size() - 1 : 1;
      std::size_t consumedBytes = 0;

      for (std::size_t i = 0; i < chunksToSynthesize; ++i) {
        throwIfSynthesisCancelled(shouldCancel);
        synthesizeChunk(chunks[i], totalAudioBytes);
        consumedBytes += chunks[i].size();
      }

      if (consumedBytes == 0 || consumedBytes >= pending.size()) {
        pending.clear();
        break;
      }

      pending.erase(0, consumedBytes);
      std::size_t leadingWhitespace = 0;
      trimLeadingAsciiWhitespace(pending, leadingWhitespace);
      if (leadingWhitespace > 0) {
        pending.erase(0, leadingWhitespace);
      }
    }
  }

  if (!pending.empty()) {
    auto chunks = splitTextIntoChunks(pending, maxChunkBytes);
    for (const auto &textChunk : chunks) {
      synthesizeChunk(textChunk, totalAudioBytes);
    }
  }

  const auto endPosition = audioFile.tellp();
  if ((headerPosition != std::streampos(-1)) &&
      (endPosition != std::streampos(-1))) {
    const std::uint64_t maxWavBytes = std::numeric_limits<std::uint32_t>::max();
    if (totalAudioBytes > maxWavBytes) {
      throw std::runtime_error(
          "Generated WAV exceeds 4 GiB. Use --output_raw for very large audio.");
    }

    audioFile.seekp(headerPosition);
    writeWavHeaderBytes(synthesisConfig.sampleRate, synthesisConfig.sampleWidth,
                        synthesisConfig.channels,
                        static_cast<std::uint32_t>(totalAudioBytes), audioFile);
    audioFile.seekp(endPosition);
  } else {
    spdlog::warn("WAV stream is not seekable; wrote a best-effort streaming WAV "
                 "header. Use --output_raw for unbounded streaming output.");
  }
}

} // namespace piper
