#ifndef PIPER_H_
#define PIPER_H_

#include <cstddef>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>
#include <piper-phonemize/phoneme_ids.hpp>
#include <piper-phonemize/phonemize.hpp>
#include <piper-phonemize/tashkeel.hpp>

#include "json.hpp"

using json = nlohmann::json;

namespace piper {

typedef int64_t SpeakerId;

struct eSpeakConfig {
  std::string voice = "en-us";
};

struct PiperConfig {
  std::string eSpeakDataPath;
  bool useESpeak = true;

  bool useTashkeel = false;
  std::optional<std::string> tashkeelModelPath;
  std::unique_ptr<tashkeel::State> tashkeelState;
};

enum PhonemeType { eSpeakPhonemes, TextPhonemes };

struct PhonemizeConfig {
  PhonemeType phonemeType = eSpeakPhonemes;
  std::optional<std::map<Phoneme, std::vector<Phoneme>>> phonemeMap;
  std::map<Phoneme, std::vector<PhonemeId>> phonemeIdMap;

  PhonemeId idPad = 0; // padding (optionally interspersed)
  PhonemeId idBos = 1; // beginning of sentence
  PhonemeId idEos = 2; // end of sentence
  bool interspersePad = true;

  eSpeakConfig eSpeak;
};

struct SynthesisConfig {
  // VITS inference settings
  float noiseScale = 0.667f;
  float lengthScale = 1.0f;
  float noiseW = 0.8f;

  // Audio settings
  int sampleRate = 22050;
  int sampleWidth = 2; // 16-bit
  int channels = 1;    // mono

  // Speaker id from 0 to numSpeakers - 1
  std::optional<SpeakerId> speakerId;

  // Extra silence
  float sentenceSilenceSeconds = 0.2f;
  std::optional<std::map<piper::Phoneme, float>> phonemeSilenceSeconds;
};

struct ModelConfig {
  int numSpeakers;

  // speaker name -> id
  std::optional<std::map<std::string, SpeakerId>> speakerIdMap;
};

struct ModelSession {
  Ort::Session onnx;
  Ort::AllocatorWithDefaultOptions allocator;
  Ort::SessionOptions options;
  Ort::Env env;

  ModelSession() : onnx(nullptr){};
};

struct SynthesisResult {
  double inferSeconds = 0.0;
  double audioSeconds = 0.0;
  double realTimeFactor = 0.0;
};

struct Voice {
  json configRoot;
  PhonemizeConfig phonemizeConfig;
  SynthesisConfig synthesisConfig;
  ModelConfig modelConfig;
  ModelSession session;
};

// True if the string is a single UTF-8 codepoint
bool isSingleCodepoint(std::string s);

// Get the first UTF-8 codepoint of a string
Phoneme getCodepoint(std::string s);

// Get version of Piper
std::string getVersion();

// Split UTF-8 text into smart chunks without breaking normal words or Spanish questions when possible.
std::vector<std::string> splitTextIntoChunks(const std::string &text,
                                             std::size_t maxChunkBytes);

// Must be called before using textTo* functions
void initialize(PiperConfig &config);

// Clean up
void terminate(PiperConfig &config);

// Load Onnx model and JSON config file
void loadVoice(PiperConfig &config, std::string modelPath,
               std::string modelConfigPath, Voice &voice,
               std::optional<SpeakerId> &speakerId, bool useCuda,
               std::optional<int> cpuThreads = std::nullopt);

// Phonemize text and synthesize audio
void textToAudio(PiperConfig &config, Voice &voice, std::string text,
                 std::vector<int16_t> &audioBuffer, SynthesisResult &result,
                 const std::function<void()> &audioCallback,
                 const std::function<bool()> &shouldCancel = nullptr);

// Phonemize text and synthesize audio to WAV file.
// Audio is written progressively so large inputs do not keep the whole WAV in RAM.
void textToWavFile(PiperConfig &config, Voice &voice, std::string text,
                   std::ostream &audioFile, SynthesisResult &result,
                   std::size_t maxChunkBytes = 0,
                   const std::function<bool()> &shouldCancel = nullptr);

// Read text from a stream, split it into bounded chunks, and synthesize one WAV.
// This is intended for very long stdin payloads.
void textToWavFileFromStream(PiperConfig &config, Voice &voice,
                             std::istream &textStream,
                             std::ostream &audioFile, SynthesisResult &result,
                             std::size_t maxChunkBytes,
                             const std::function<bool()> &shouldCancel = nullptr);

} // namespace piper

#endif // PIPER_H_
