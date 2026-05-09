#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <cstddef>
#include <vector>

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "json.hpp"
#include "piper.hpp"
#include "server.hpp"

using namespace std;
using json = nlohmann::json;

enum OutputType { OUTPUT_FILE, OUTPUT_DIRECTORY, OUTPUT_STDOUT, OUTPUT_RAW };

struct RunConfig {
  // Path to .onnx voice file
  filesystem::path modelPath;

  // Path to JSON voice config file
  filesystem::path modelConfigPath;

  // Type of output to produce.
  // Default is to write a WAV file in the current directory.
  OutputType outputType = OUTPUT_DIRECTORY;

  // Path for output
  optional<filesystem::path> outputPath = filesystem::path(".");

  // Numerical id of the default speaker (multi-speaker voices)
  optional<piper::SpeakerId> speakerId;

  // Amount of noise to add during audio generation
  optional<float> noiseScale;

  // Speed of speaking (1 = normal, < 1 is faster, > 1 is slower)
  optional<float> lengthScale;

  // Variation in phoneme lengths
  optional<float> noiseW;

  // Seconds of silence to add after each sentence
  optional<float> sentenceSilenceSeconds;

  // Path to espeak-ng data directory (default is next to piper executable)
  optional<filesystem::path> eSpeakDataPath;

  // Path to libtashkeel ort model
  // https://github.com/mush42/libtashkeel/
  optional<filesystem::path> tashkeelModelPath;

  // stdin input is lines of JSON instead of text with format:
  // {
  //   "text": str,               (required)
  //   "speaker_id": int,         (optional)
  //   "speaker": str,            (optional)
  //   "output_file": str,        (optional)
  // }
  bool jsonInput = false;

  // Seconds of extra silence to insert after a single phoneme
  optional<std::map<piper::Phoneme, float>> phonemeSilenceSeconds;

  // true to use CUDA execution provider
  bool useCuda = false;

  // 0 or nullopt lets ONNX Runtime choose its default thread count.
  optional<int> cpuThreads;
  bool cpuThreadsExplicit = false;

  // Maximum preferred text chunk size before phonemization/synthesis.
  // Long inputs are split intelligently so words/questions are preserved when
  // possible and the full WAV is not kept in memory.
  size_t maxTextChunkBytes = 4096;

  // Maximum HTTP/text payload accepted by API server and direct inputs.
  size_t maxInputBytes = 10 * 1024 * 1024;

  // Direct text/file inputs for one-shot synthesis.
  optional<string> inputText;
  optional<filesystem::path> inputFilePath;

  // API server mode.
  bool serverMode = false;
  string serverHost = "127.0.0.1";
  int serverPort = 8080;
  filesystem::path modelsDir = filesystem::path("models");
  optional<string> apiToken;
  size_t maxConcurrentJobs = 0;
  size_t maxModelReplicas = 0;
  size_t chunkWorkers = 0;
  size_t queueSize = 0;
  size_t queueTimeoutSeconds = 60;
  size_t maxTempBytes = 1024ULL * 1024ULL * 1024ULL;
  size_t outputRetentionSeconds = 3600;
  size_t modelsRefreshSeconds = 30;
  string cpuProfile = "balanced";
  bool outputDirExplicit = false;
};

void parseArgs(int argc, char *argv[], RunConfig &runConfig);
void applyAutoServerResourceConfig(RunConfig &runConfig);
optional<string> resolveApiToken(const optional<string> &explicitToken);
void rawOutputProc(vector<int16_t> &sharedAudioBuffer, mutex &mutAudio,
                   condition_variable &cvAudio, bool &audioReady,
                   bool &audioFinished);

// ----------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  spdlog::set_default_logger(spdlog::stderr_color_st("piper"));

  RunConfig runConfig;
  parseArgs(argc, argv, runConfig);

#ifdef _WIN32
  // Required on Windows to preserve UTF-8 text, IPA symbols and Spanish accents.
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  piper::PiperConfig piperConfig;
  piper::Voice voice;

  spdlog::debug("Loading voice from {} (config={})",
                runConfig.modelPath.string(),
                runConfig.modelConfigPath.string());

  const auto loadWallTime = chrono::system_clock::now();
  const auto loadWallT = chrono::system_clock::to_time_t(loadWallTime);
  std::tm loadWallTm{};
#ifdef _WIN32
  gmtime_s(&loadWallTm, &loadWallT);
#else
  gmtime_r(&loadWallT, &loadWallTm);
#endif
  std::ostringstream loadTimestamp;
  loadTimestamp << std::put_time(&loadWallTm, "%Y-%m-%dT%H:%M:%SZ");
  spdlog::info("{} Model load started: model={}", loadTimestamp.str(),
               runConfig.modelPath.filename().string());

  auto startTime = chrono::steady_clock::now();
  loadVoice(piperConfig, runConfig.modelPath.string(),
            runConfig.modelConfigPath.string(), voice, runConfig.speakerId,
            runConfig.useCuda, runConfig.cpuThreads);
  auto endTime = chrono::steady_clock::now();
  spdlog::info("{} Model load finished: model={} duration_ms={}",
               loadTimestamp.str(), runConfig.modelPath.filename().string(),
               chrono::duration_cast<chrono::milliseconds>(endTime - startTime).count());

  // Get the path to the piper executable so we can locate espeak-ng-data, etc.
  // next to it.
#ifdef _MSC_VER
  auto exePath = []() {
    wchar_t moduleFileName[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, moduleFileName, std::size(moduleFileName));
    return filesystem::path(moduleFileName);
  }();
#else
#ifdef __APPLE__
  auto exePath = []() {
    char moduleFileName[PATH_MAX] = {0};
    uint32_t moduleFileNameSize = std::size(moduleFileName);
    _NSGetExecutablePath(moduleFileName, &moduleFileNameSize);
    return filesystem::path(moduleFileName);
  }();
#else
  auto exePath = filesystem::canonical("/proc/self/exe");
#endif
#endif

  if (voice.phonemizeConfig.phonemeType == piper::eSpeakPhonemes) {
    spdlog::debug("Voice uses eSpeak phonemes ({})",
                  voice.phonemizeConfig.eSpeak.voice);

    if (runConfig.eSpeakDataPath) {
      // User provided path
      piperConfig.eSpeakDataPath = runConfig.eSpeakDataPath.value().string();
    } else {
      // Assume next to piper executable
      piperConfig.eSpeakDataPath =
          std::filesystem::absolute(
              exePath.parent_path().append("espeak-ng-data"))
              .string();

      spdlog::debug("espeak-ng-data directory is expected at {}",
                    piperConfig.eSpeakDataPath);
    }
  } else {
    // Not using eSpeak
    piperConfig.useESpeak = false;
  }

  // Enable libtashkeel for Arabic
  if (voice.phonemizeConfig.eSpeak.voice == "ar") {
    piperConfig.useTashkeel = true;
    if (runConfig.tashkeelModelPath) {
      // User provided path
      piperConfig.tashkeelModelPath =
          runConfig.tashkeelModelPath.value().string();
    } else {
      // Assume next to piper executable
      piperConfig.tashkeelModelPath =
          std::filesystem::absolute(
              exePath.parent_path().append("libtashkeel_model.ort"))
              .string();

      spdlog::debug("libtashkeel model is expected at {}",
                    piperConfig.tashkeelModelPath.value());
    }
  }

  piper::initialize(piperConfig);

  // Scales
  if (runConfig.noiseScale) {
    voice.synthesisConfig.noiseScale = runConfig.noiseScale.value();
  }

  if (runConfig.lengthScale) {
    voice.synthesisConfig.lengthScale = runConfig.lengthScale.value();
  }

  if (runConfig.noiseW) {
    voice.synthesisConfig.noiseW = runConfig.noiseW.value();
  }

  if (runConfig.sentenceSilenceSeconds) {
    voice.synthesisConfig.sentenceSilenceSeconds =
        runConfig.sentenceSilenceSeconds.value();
  }

  if (runConfig.phonemeSilenceSeconds) {
    if (!voice.synthesisConfig.phonemeSilenceSeconds) {
      // Overwrite
      voice.synthesisConfig.phonemeSilenceSeconds =
          runConfig.phonemeSilenceSeconds;
    } else {
      // Merge
      for (const auto &[phoneme, silenceSeconds] :
           *runConfig.phonemeSilenceSeconds) {
        voice.synthesisConfig.phonemeSilenceSeconds->try_emplace(
            phoneme, silenceSeconds);
      }
    }

  } // if phonemeSilenceSeconds

  if (runConfig.outputType == OUTPUT_DIRECTORY) {
    runConfig.outputPath = filesystem::absolute(runConfig.outputPath.value());
    spdlog::info("Output directory: {}", runConfig.outputPath.value().string());
  }

  auto timestampedOutputPath = [&runConfig]() {
    const auto now = chrono::system_clock::now();
    const auto timestamp =
        chrono::duration_cast<chrono::nanoseconds>(now.time_since_epoch())
            .count();
    stringstream outputName;
    outputName << timestamp << ".wav";
    filesystem::path outputPath = runConfig.outputPath.value_or(".");
    outputPath.append(outputName.str());
    return outputPath;
  };

  if (runConfig.serverMode) {
    piper_server::ServerOptions serverOptions;
    serverOptions.host = runConfig.serverHost;
    serverOptions.port = runConfig.serverPort;
    serverOptions.modelsDir = filesystem::absolute(runConfig.modelsDir);
    serverOptions.outputDir = filesystem::absolute(
        runConfig.outputDirExplicit ? runConfig.outputPath.value()
                                    : filesystem::path("outputs"));
    serverOptions.activeModelPath = filesystem::absolute(runConfig.modelPath);
    serverOptions.activeModelConfigPath = filesystem::absolute(runConfig.modelConfigPath);
    serverOptions.defaultSpeakerId = runConfig.speakerId;
    serverOptions.useCuda = runConfig.useCuda;
    serverOptions.cpuThreads = runConfig.cpuThreads;
    serverOptions.maxInputBytes = runConfig.maxInputBytes;
    serverOptions.maxTextChunkBytes = runConfig.maxTextChunkBytes;
    serverOptions.maxConcurrentJobs = runConfig.maxConcurrentJobs;
    serverOptions.maxModelReplicas = runConfig.maxModelReplicas;
    serverOptions.chunkWorkers = runConfig.chunkWorkers;
    serverOptions.queueSize = runConfig.queueSize;
    serverOptions.queueTimeoutSeconds = runConfig.queueTimeoutSeconds;
    serverOptions.maxTempBytes = runConfig.maxTempBytes;
    serverOptions.outputRetentionSeconds = runConfig.outputRetentionSeconds;
    serverOptions.modelsRefreshSeconds = runConfig.modelsRefreshSeconds;
    serverOptions.cpuProfile = runConfig.cpuProfile;
    serverOptions.apiToken = resolveApiToken(runConfig.apiToken).value_or("");

    piper_server::runServer(piperConfig, voice, serverOptions);
    piper::terminate(piperConfig);
    return EXIT_SUCCESS;
  }

  if (!runConfig.jsonInput && (runConfig.inputText || runConfig.inputFilePath ||
                               (runConfig.outputType == OUTPUT_FILE))) {
    filesystem::path outputPath;
    if (runConfig.outputType == OUTPUT_FILE) {
      if (!runConfig.outputPath || runConfig.outputPath->empty()) {
        throw runtime_error("No output path provided");
      }
      outputPath = runConfig.outputPath.value();
    } else if (runConfig.outputType == OUTPUT_DIRECTORY) {
      outputPath = timestampedOutputPath();
    } else if (runConfig.outputType == OUTPUT_STDOUT) {
      outputPath.clear();
    } else {
      throw runtime_error("--input-file/--text are not supported with --output_raw");
    }

    piper::SynthesisResult result;
    std::unique_ptr<std::istream> ownedInput;
    std::istream *inputStream = &cin;

    if (runConfig.inputText) {
      ownedInput = std::make_unique<std::istringstream>(*runConfig.inputText);
      inputStream = ownedInput.get();
    } else if (runConfig.inputFilePath) {
      auto fileInput = std::make_unique<std::ifstream>(
          runConfig.inputFilePath->string(), ios::binary);
      if (!fileInput->good()) {
        throw runtime_error("Input text file doesn't exist");
      }
      inputStream = fileInput.get();
      ownedInput = std::move(fileInput);
    }

    if (runConfig.outputType == OUTPUT_STDOUT) {
#ifdef _WIN32
      setmode(fileno(stdout), O_BINARY);
#endif
      piper::textToWavFileFromStream(piperConfig, voice, *inputStream, cout,
                                     result, runConfig.maxTextChunkBytes);
    } else {
      ofstream audioFile(outputPath.string(), ios::binary);
      piper::textToWavFileFromStream(piperConfig, voice, *inputStream, audioFile,
                                     result, runConfig.maxTextChunkBytes);
      cout << outputPath.string() << endl;
    }

    spdlog::info("Real-time factor: {} (infer={} sec, audio={} sec)",
                 result.realTimeFactor, result.inferSeconds,
                 result.audioSeconds);

    piper::terminate(piperConfig);
    return EXIT_SUCCESS;
  }

  string line;
  while (getline(cin, line)) {
    piper::SynthesisResult result;
    auto outputType = runConfig.outputType;
    auto speakerId = voice.synthesisConfig.speakerId;
    std::optional<filesystem::path> maybeOutputPath = runConfig.outputPath;

    if (runConfig.jsonInput) {
      // Each line is a JSON object
      json lineRoot = json::parse(line);

      // Text is required
      line = lineRoot["text"].get<std::string>();

      if (lineRoot.contains("output_file")) {
        // Override output WAV file path
        outputType = OUTPUT_FILE;
        maybeOutputPath =
            filesystem::path(lineRoot["output_file"].get<std::string>());
      }

      if (lineRoot.contains("speaker_id")) {
        // Override speaker id
        voice.synthesisConfig.speakerId =
            lineRoot["speaker_id"].get<piper::SpeakerId>();
      } else if (lineRoot.contains("speaker")) {
        // Resolve to id using speaker id map
        auto speakerName = lineRoot["speaker"].get<std::string>();
        if ((voice.modelConfig.speakerIdMap) &&
            (voice.modelConfig.speakerIdMap->count(speakerName) > 0)) {
          voice.synthesisConfig.speakerId =
              (*voice.modelConfig.speakerIdMap)[speakerName];
        } else {
          spdlog::warn("No speaker named: {}", speakerName);
        }
      }
    }

    // Timestamp is used for path to output WAV file
    const auto now = chrono::system_clock::now();
    const auto timestamp =
        chrono::duration_cast<chrono::nanoseconds>(now.time_since_epoch())
            .count();

    if (outputType == OUTPUT_DIRECTORY) {
      // Generate path using timestamp
      stringstream outputName;
      outputName << timestamp << ".wav";
      filesystem::path outputPath = runConfig.outputPath.value();
      outputPath.append(outputName.str());

      // Output audio to automatically-named WAV file in a directory
      ofstream audioFile(outputPath.string(), ios::binary);
      piper::textToWavFile(piperConfig, voice, line, audioFile, result,
                            runConfig.maxTextChunkBytes);
      cout << outputPath.string() << endl;
    } else if (outputType == OUTPUT_FILE) {
      if (!maybeOutputPath || maybeOutputPath->empty()) {
        throw runtime_error("No output path provided");
      }

      filesystem::path outputPath = maybeOutputPath.value();

      // Output audio to WAV file
      ofstream audioFile(outputPath.string(), ios::binary);
      piper::textToWavFile(piperConfig, voice, line, audioFile, result,
                            runConfig.maxTextChunkBytes);
      cout << outputPath.string() << endl;
    } else if (outputType == OUTPUT_STDOUT) {
      // Output WAV to stdout
      piper::textToWavFile(piperConfig, voice, line, cout, result,
                            runConfig.maxTextChunkBytes);
    } else if (outputType == OUTPUT_RAW) {
      // Raw output to stdout
      mutex mutAudio;
      condition_variable cvAudio;
      bool audioReady = false;
      bool audioFinished = false;
      vector<int16_t> audioBuffer;
      vector<int16_t> sharedAudioBuffer;

#ifdef _WIN32
      // Needed on Windows to avoid terminal conversions
      setmode(fileno(stdout), O_BINARY);
      setmode(fileno(stdin), O_BINARY);
#endif

      thread rawOutputThread(rawOutputProc, ref(sharedAudioBuffer),
                             ref(mutAudio), ref(cvAudio), ref(audioReady),
                             ref(audioFinished));
      auto audioCallback = [&audioBuffer, &sharedAudioBuffer, &mutAudio,
                            &cvAudio, &audioReady]() {
        // Signal thread that audio is ready
        {
          unique_lock lockAudio(mutAudio);
          copy(audioBuffer.begin(), audioBuffer.end(),
               back_inserter(sharedAudioBuffer));
          audioReady = true;
          cvAudio.notify_one();
        }
      };
      piper::textToAudio(piperConfig, voice, line, audioBuffer, result,
                         audioCallback);

      // Signal thread that there is no more audio
      {
        unique_lock lockAudio(mutAudio);
        audioReady = true;
        audioFinished = true;
        cvAudio.notify_one();
      }

      // Wait for audio output to finish
      spdlog::info("Waiting for audio to finish playing...");
      rawOutputThread.join();
    }

    spdlog::info("Real-time factor: {} (infer={} sec, audio={} sec)",
                 result.realTimeFactor, result.inferSeconds,
                 result.audioSeconds);

    // Restore config (--json-input)
    voice.synthesisConfig.speakerId = speakerId;

  } // for each line

  piper::terminate(piperConfig);

  return EXIT_SUCCESS;
}

// ----------------------------------------------------------------------------

size_t clampSize(size_t value, size_t minValue, size_t maxValue) {
  return std::max(minValue, std::min(value, maxValue));
}

void applyAutoServerResourceConfig(RunConfig &runConfig) {
  unsigned int hardwareThreads = std::thread::hardware_concurrency();
  if (hardwareThreads == 0) {
    hardwareThreads = 2;
  }

  size_t autoCpuThreads = 1;
  size_t autoMaxJobs = 1;
  size_t autoChunkWorkers = 1;
  size_t autoReplicas = 1;
  size_t autoQueueSize = 16;

  if (runConfig.cpuProfile == "eco") {
    autoCpuThreads = 1;
    autoMaxJobs = hardwareThreads >= 4 ? 2 : 1;
    autoChunkWorkers = autoMaxJobs;
    autoReplicas = 1;
    autoQueueSize = 16;
  } else if (runConfig.cpuProfile == "fast") {
    autoCpuThreads = hardwareThreads >= 8 ? 3 : (hardwareThreads >= 4 ? 2 : 1);
    autoMaxJobs = hardwareThreads >= 12 ? 5 : (hardwareThreads >= 8 ? 4 : 2);
    autoChunkWorkers = hardwareThreads >= 12 ? 5 : (hardwareThreads >= 8 ? 4 : 2);
    autoReplicas = hardwareThreads >= 12 ? 3 : 2;
    autoQueueSize = 48;
  } else if (runConfig.cpuProfile == "max") {
    autoCpuThreads = std::max<size_t>(1, hardwareThreads / 2);
    autoMaxJobs = hardwareThreads >= 16 ? 6 : (hardwareThreads >= 8 ? 4 : 2);
    autoChunkWorkers = autoMaxJobs;
    autoReplicas = hardwareThreads >= 16 ? 4 : (hardwareThreads >= 8 ? 3 : 2);
    autoQueueSize = 64;
  } else {
    // balanced
    autoCpuThreads = hardwareThreads >= 6 ? 2 : 1;
    autoMaxJobs = hardwareThreads >= 12 ? 4 : (hardwareThreads >= 6 ? 3 : (hardwareThreads >= 4 ? 2 : 1));
    autoChunkWorkers = autoMaxJobs;
    autoReplicas = hardwareThreads >= 12 ? 3 : (hardwareThreads >= 6 ? 2 : 1);
    autoQueueSize = 32;
  }

  if (!runConfig.cpuThreadsExplicit && !runConfig.cpuThreads) {
    runConfig.cpuThreads = static_cast<int>(autoCpuThreads);
  }

  if (runConfig.maxConcurrentJobs == 0) {
    runConfig.maxConcurrentJobs = autoMaxJobs;
  }

  if (runConfig.chunkWorkers == 0) {
    runConfig.chunkWorkers = autoChunkWorkers;
  }

  if (runConfig.maxModelReplicas == 0) {
    runConfig.maxModelReplicas = autoReplicas;
  }

  if (runConfig.queueSize == 0) {
    runConfig.queueSize = std::max<size_t>(autoQueueSize, runConfig.maxConcurrentJobs);
  }

  // Prevent accidental oversubscription when the user overrides only part of the policy.
  const size_t cpuThreads = static_cast<size_t>(runConfig.cpuThreads.value_or(1));
  const size_t safeWorkersByCpu = std::max<size_t>(1, hardwareThreads / std::max<size_t>(1, cpuThreads));
  runConfig.chunkWorkers = clampSize(runConfig.chunkWorkers, 1, safeWorkersByCpu);
  runConfig.maxConcurrentJobs = std::max<size_t>(1, runConfig.maxConcurrentJobs);
  runConfig.maxModelReplicas = std::max<size_t>(1, runConfig.maxModelReplicas);
  runConfig.queueSize = std::max(runConfig.queueSize, runConfig.maxConcurrentJobs);

  spdlog::info("Server resource policy: profile={} hardware_threads={} cpu_threads={} max_jobs={} chunk_workers={} max_model_replicas={} queue_size={}",
               runConfig.cpuProfile, hardwareThreads, runConfig.cpuThreads.value_or(0),
               runConfig.maxConcurrentJobs, runConfig.chunkWorkers,
               runConfig.maxModelReplicas, runConfig.queueSize);
}

// ----------------------------------------------------------------------------

string trimEnvValue(string value) {
  auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }

  if (value.size() >= 2) {
    const char first = value.front();
    const char last = value.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
      value = value.substr(1, value.size() - 2);
    }
  }

  return value;
}

optional<string> envValue(const string &key) {
  const char *rawValue = std::getenv(key.c_str());
  if (rawValue == nullptr) {
    return nullopt;
  }

  auto value = trimEnvValue(rawValue);
  if (value.empty()) {
    return nullopt;
  }

  return value;
}

optional<string> dotenvValue(const filesystem::path &envPath,
                             const vector<string> &keys) {
  ifstream envFile(envPath.string());
  if (!envFile.good()) {
    return nullopt;
  }

  string line;
  while (getline(envFile, line)) {
    line = trimEnvValue(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const auto equals = line.find('=');
    if (equals == string::npos) {
      continue;
    }

    auto key = trimEnvValue(line.substr(0, equals));
    auto value = trimEnvValue(line.substr(equals + 1));
    if (!value.empty() && value[0] == '#') {
      continue;
    }

    for (const auto &expectedKey : keys) {
      if (key == expectedKey && !value.empty()) {
        return value;
      }
    }
  }

  return nullopt;
}

optional<string> resolveApiToken(const optional<string> &explicitToken) {
  if (explicitToken && !explicitToken->empty()) {
    return explicitToken;
  }

  const vector<string> keys = {"PIPER_API_TOKEN", "PIPER_AUTH_TOKEN", "API_TOKEN"};
  for (const auto &key : keys) {
    if (auto value = envValue(key)) {
      return value;
    }
  }

  return dotenvValue(filesystem::path(".env"), keys);
}

// ----------------------------------------------------------------------------

void rawOutputProc(vector<int16_t> &sharedAudioBuffer, mutex &mutAudio,
                   condition_variable &cvAudio, bool &audioReady,
                   bool &audioFinished) {
  vector<int16_t> internalAudioBuffer;
  while (true) {
    {
      unique_lock lockAudio{mutAudio};
      cvAudio.wait(lockAudio, [&audioReady] { return audioReady; });

      if (sharedAudioBuffer.empty() && audioFinished) {
        break;
      }

      copy(sharedAudioBuffer.begin(), sharedAudioBuffer.end(),
           back_inserter(internalAudioBuffer));

      sharedAudioBuffer.clear();

      if (!audioFinished) {
        audioReady = false;
      }
    }

    cout.write((const char *)internalAudioBuffer.data(),
               sizeof(int16_t) * internalAudioBuffer.size());
    cout.flush();
    internalAudioBuffer.clear();
  }

} // rawOutputProc

// ----------------------------------------------------------------------------

void printUsage(char *argv[]) {
  cerr << endl;
  cerr << "usage: " << argv[0] << " [options]" << endl;
  cerr << endl;
  cerr << "options:" << endl;
  cerr << "   -h        --help              show this message and exit" << endl;
  cerr << "   -m  FILE  --model       FILE  path to onnx model file" << endl;
  cerr << "   -c  FILE  --config      FILE  path to model config file "
          "(default: model path + .json)"
       << endl;
  cerr << "   -f  FILE  --output_file FILE  path to output WAV file ('-' for "
          "stdout)"
       << endl;
  cerr << "   -d  DIR   --output_dir  DIR   path to output directory (default: "
          "cwd)"
       << endl;
  cerr << "   --output_raw                  output raw audio to stdout as it "
          "becomes available"
       << endl;
  cerr << "   -s  NUM   --speaker     NUM   id of speaker (default: 0)" << endl;
  cerr << "   --noise_scale           NUM   generator noise (default: 0.667)"
       << endl;
  cerr << "   --length_scale          NUM   phoneme length (default: 1.0)"
       << endl;
  cerr << "   --noise_w               NUM   phoneme width noise (default: 0.8)"
       << endl;
  cerr << "   --sentence_silence      NUM   seconds of silence after each "
          "sentence (default: 0.2)"
       << endl;
  cerr << "   --espeak_data           DIR   path to espeak-ng data directory"
       << endl;
  cerr << "   --tashkeel_model        FILE  path to libtashkeel onnx model "
          "(arabic)"
       << endl;
  cerr << "   --text                  TEXT  synthesize a direct text payload"
       << endl;
  cerr << "   --input_file            FILE  read text from a UTF-8 text file"
       << endl;
  cerr << "   --json-input                  stdin input is lines of JSON "
          "instead of plain text"
       << endl;
  cerr << "   --server                      start local HTTP API server" << endl;
  cerr << "   --host                  IP    server host (default: 127.0.0.1)"
       << endl;
  cerr << "   --port                  NUM   server port (default: 8080)"
       << endl;
  cerr << "   --models                DIR   models directory for server mode "
          "(default: models/)"
       << endl;
  cerr << "   --api-token             TOKEN protect API server with bearer token"
       << endl;
  cerr << "                                env/.env: PIPER_API_TOKEN" << endl;
  cerr << "   --cpu-profile           NAME  auto resource profile: eco, balanced, "
          "fast, max (server default: balanced)"
       << endl;
  cerr << "   --max-concurrent-jobs   NUM   max accepted simultaneous API jobs "
          "(default: auto)"
       << endl;
  cerr << "   --chunk-workers         NUM   fair chunk worker count "
          "(default: auto)"
       << endl;
  cerr << "   --max-model-replicas    NUM   max ONNX replicas per model "
          "(default: auto)"
       << endl;
  cerr << "   --queue-size            NUM   max waiting/active API jobs "
          "(default: auto)"
       << endl;
  cerr << "   --queue-timeout-seconds NUM   seconds a job may wait in queue "
          "(default: 60)"
       << endl;
  cerr << "   --max-temp-bytes       NUM   max temporary RAW audio bytes "
          "(default: 1073741824)"
       << endl;
  cerr << "   --output-retention-seconds NUM seconds generated API WAV files stay "
          "available (default: 3600)"
       << endl;
  cerr << "   --models-refresh-seconds NUM seconds to cache /models metadata "
          "(default: 30)"
       << endl;
  cerr << "   --max-input-bytes       NUM   max API/direct input bytes "
          "(default: 10485760)"
       << endl;
  cerr << "   --use-cuda                    use CUDA execution provider"
       << endl;
  cerr << "   --cpu-threads           NUM   limit ONNX Runtime CPU threads"
       << endl;
  cerr << "   --max-text-chunk-bytes  NUM   preferred smart chunk size before "
          "synthesis (default: 4096)"
       << endl;
  cerr << "   --debug                       print DEBUG messages to the console"
       << endl;
  cerr << "   -q       --quiet              disable logging" << endl;
  cerr << endl;
}

void ensureArg(int argc, char *argv[], int argi) {
  if ((argi + 1) >= argc) {
    printUsage(argv);
    exit(0);
  }
}

// Parse command-line arguments
void parseArgs(int argc, char *argv[], RunConfig &runConfig) {
  optional<filesystem::path> modelConfigPath;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "-m" || arg == "--model") {
      ensureArg(argc, argv, i);
      runConfig.modelPath = filesystem::path(argv[++i]);
    } else if (arg == "-c" || arg == "--config") {
      ensureArg(argc, argv, i);
      modelConfigPath = filesystem::path(argv[++i]);
    } else if (arg == "-f" || arg == "--output_file" ||
               arg == "--output-file") {
      ensureArg(argc, argv, i);
      std::string filePath = argv[++i];
      if (filePath == "-") {
        runConfig.outputType = OUTPUT_STDOUT;
        runConfig.outputPath = nullopt;
      } else {
        runConfig.outputType = OUTPUT_FILE;
        runConfig.outputPath = filesystem::path(filePath);
      }
    } else if (arg == "-d" || arg == "--output_dir" || arg == "--output-dir") {
      ensureArg(argc, argv, i);
      runConfig.outputType = OUTPUT_DIRECTORY;
      runConfig.outputPath = filesystem::path(argv[++i]);
      runConfig.outputDirExplicit = true;
    } else if (arg == "--output_raw" || arg == "--output-raw") {
      runConfig.outputType = OUTPUT_RAW;
    } else if (arg == "-s" || arg == "--speaker") {
      ensureArg(argc, argv, i);
      runConfig.speakerId = (piper::SpeakerId)stol(argv[++i]);
    } else if (arg == "--noise_scale" || arg == "--noise-scale") {
      ensureArg(argc, argv, i);
      runConfig.noiseScale = stof(argv[++i]);
    } else if (arg == "--length_scale" || arg == "--length-scale") {
      ensureArg(argc, argv, i);
      runConfig.lengthScale = stof(argv[++i]);
    } else if (arg == "--noise_w" || arg == "--noise-w") {
      ensureArg(argc, argv, i);
      runConfig.noiseW = stof(argv[++i]);
    } else if (arg == "--sentence_silence" || arg == "--sentence-silence") {
      ensureArg(argc, argv, i);
      runConfig.sentenceSilenceSeconds = stof(argv[++i]);
    } else if (arg == "--phoneme_silence" || arg == "--phoneme-silence") {
      ensureArg(argc, argv, i);
      ensureArg(argc, argv, i + 1);
      auto phonemeStr = std::string(argv[++i]);
      if (!piper::isSingleCodepoint(phonemeStr)) {
        std::cerr << "Phoneme '" << phonemeStr
                  << "' is not a single codepoint (--phoneme_silence)"
                  << std::endl;
        exit(1);
      }

      if (!runConfig.phonemeSilenceSeconds) {
        runConfig.phonemeSilenceSeconds.emplace();
      }

      auto phoneme = piper::getCodepoint(phonemeStr);
      (*runConfig.phonemeSilenceSeconds)[phoneme] = stof(argv[++i]);
    } else if (arg == "--espeak_data" || arg == "--espeak-data") {
      ensureArg(argc, argv, i);
      runConfig.eSpeakDataPath = filesystem::path(argv[++i]);
    } else if (arg == "--tashkeel_model" || arg == "--tashkeel-model") {
      ensureArg(argc, argv, i);
      runConfig.tashkeelModelPath = filesystem::path(argv[++i]);
    } else if (arg == "--text") {
      ensureArg(argc, argv, i);
      runConfig.inputText = string(argv[++i]);
    } else if (arg == "--input_file" || arg == "--input-file") {
      ensureArg(argc, argv, i);
      runConfig.inputFilePath = filesystem::path(argv[++i]);
    } else if (arg == "--json_input" || arg == "--json-input") {
      runConfig.jsonInput = true;
    } else if (arg == "--use_cuda" || arg == "--use-cuda") {
      runConfig.useCuda = true;
    } else if (arg == "--server") {
      runConfig.serverMode = true;
    } else if (arg == "--host") {
      ensureArg(argc, argv, i);
      runConfig.serverHost = string(argv[++i]);
    } else if (arg == "--port") {
      ensureArg(argc, argv, i);
      runConfig.serverPort = stoi(argv[++i]);
      if ((runConfig.serverPort < 1) || (runConfig.serverPort > 65535)) {
        throw runtime_error("--port must be between 1 and 65535");
      }
    } else if (arg == "--models") {
      ensureArg(argc, argv, i);
      runConfig.modelsDir = filesystem::path(argv[++i]);
    } else if (arg == "--api-token" || arg == "--api_token") {
      ensureArg(argc, argv, i);
      runConfig.apiToken = string(argv[++i]);
    } else if (arg == "--cpu-profile" || arg == "--cpu_profile") {
      ensureArg(argc, argv, i);
      runConfig.cpuProfile = string(argv[++i]);
      if (runConfig.cpuProfile != "eco" && runConfig.cpuProfile != "balanced" &&
          runConfig.cpuProfile != "fast" && runConfig.cpuProfile != "max") {
        throw runtime_error("--cpu-profile must be eco, balanced, fast or max");
      }
    } else if (arg == "--max-concurrent-jobs" || arg == "--max_concurrent_jobs") {
      ensureArg(argc, argv, i);
      long maxJobs = stol(argv[++i]);
      if (maxJobs < 1) {
        throw runtime_error("--max-concurrent-jobs must be >= 1");
      }
      runConfig.maxConcurrentJobs = static_cast<size_t>(maxJobs);
    } else if (arg == "--chunk-workers" || arg == "--chunk_workers") {
      ensureArg(argc, argv, i);
      long workers = stol(argv[++i]);
      if (workers < 1) {
        throw runtime_error("--chunk-workers must be >= 1");
      }
      runConfig.chunkWorkers = static_cast<size_t>(workers);
    } else if (arg == "--max-model-replicas" || arg == "--max_model_replicas") {
      ensureArg(argc, argv, i);
      long maxReplicas = stol(argv[++i]);
      if (maxReplicas < 1) {
        throw runtime_error("--max-model-replicas must be >= 1");
      }
      runConfig.maxModelReplicas = static_cast<size_t>(maxReplicas);
    } else if (arg == "--queue-size" || arg == "--queue_size") {
      ensureArg(argc, argv, i);
      long queueSize = stol(argv[++i]);
      if (queueSize < 1) {
        throw runtime_error("--queue-size must be >= 1");
      }
      runConfig.queueSize = static_cast<size_t>(queueSize);
    } else if (arg == "--queue-timeout-seconds" || arg == "--queue_timeout_seconds") {
      ensureArg(argc, argv, i);
      long timeoutSeconds = stol(argv[++i]);
      if (timeoutSeconds < 1) {
        throw runtime_error("--queue-timeout-seconds must be >= 1");
      }
      runConfig.queueTimeoutSeconds = static_cast<size_t>(timeoutSeconds);
    } else if (arg == "--max-temp-bytes" || arg == "--max_temp_bytes") {
      ensureArg(argc, argv, i);
      long long tempBytes = stoll(argv[++i]);
      if (tempBytes < 0) {
        throw runtime_error("--max-temp-bytes must be >= 0");
      }
      runConfig.maxTempBytes = static_cast<size_t>(tempBytes);
    } else if (arg == "--output-retention-seconds" || arg == "--output_retention_seconds") {
      ensureArg(argc, argv, i);
      long retentionSeconds = stol(argv[++i]);
      if (retentionSeconds < 0) {
        throw runtime_error("--output-retention-seconds must be >= 0");
      }
      runConfig.outputRetentionSeconds = static_cast<size_t>(retentionSeconds);
    } else if (arg == "--models-refresh-seconds" || arg == "--models_refresh_seconds") {
      ensureArg(argc, argv, i);
      long refreshSeconds = stol(argv[++i]);
      if (refreshSeconds < 1) {
        throw runtime_error("--models-refresh-seconds must be >= 1");
      }
      runConfig.modelsRefreshSeconds = static_cast<size_t>(refreshSeconds);
    } else if (arg == "--cpu_threads" || arg == "--cpu-threads") {
      ensureArg(argc, argv, i);
      string cpuThreadsArg = argv[++i];
      runConfig.cpuThreadsExplicit = true;
      if (cpuThreadsArg == "auto") {
        runConfig.cpuThreads.reset();
      } else {
        runConfig.cpuThreads = stoi(cpuThreadsArg);
        if (runConfig.cpuThreads.value() < 1) {
          throw runtime_error("--cpu-threads must be >= 1 or auto");
        }
      }
    } else if (arg == "--max_text_chunk_bytes" ||
               arg == "--max-text-chunk-bytes") {
      ensureArg(argc, argv, i);
      long chunkBytes = stol(argv[++i]);
      if (chunkBytes < 512) {
        throw runtime_error("--max-text-chunk-bytes must be >= 512");
      }
      runConfig.maxTextChunkBytes = static_cast<size_t>(chunkBytes);
    } else if (arg == "--max_input_bytes" || arg == "--max-input-bytes") {
      ensureArg(argc, argv, i);
      long inputBytes = stol(argv[++i]);
      if (inputBytes < 1024) {
        throw runtime_error("--max-input-bytes must be >= 1024");
      }
      runConfig.maxInputBytes = static_cast<size_t>(inputBytes);
    } else if (arg == "--version") {
      std::cout << piper::getVersion() << std::endl;
      exit(0);
    } else if (arg == "--debug") {
      // Set DEBUG logging
      spdlog::set_level(spdlog::level::debug);
    } else if (arg == "-q" || arg == "--quiet") {
      // diable logging
      spdlog::set_level(spdlog::level::off);
    } else if (arg == "-h" || arg == "--help") {
      printUsage(argv);
      exit(0);
    }
  }

  if (runConfig.inputText && runConfig.inputFilePath) {
    throw runtime_error("Use either --text or --input-file, not both");
  }

  if (runConfig.inputFilePath) {
    ifstream inputFile(runConfig.inputFilePath->c_str(), ios::binary);
    if (!inputFile.good()) {
      throw runtime_error("Input text file doesn't exist");
    }
  }

  if (runConfig.serverMode) {
    filesystem::create_directories(runConfig.modelsDir);

    if (runConfig.modelPath.empty()) {
      auto maybeModel = piper_server::findFirstUsableModel(runConfig.modelsDir);
      if (maybeModel) {
        runConfig.modelPath = maybeModel->modelPath;
        if (!modelConfigPath) {
          modelConfigPath = maybeModel->configPath;
        }
      } else {
        throw runtime_error(
            "No usable .onnx model found. The models directory was created; "
            "copy a model and its .onnx.json config there or pass --model.");
      }
    } else if (!filesystem::exists(runConfig.modelPath)) {
      auto modelInModelsDir = runConfig.modelsDir / runConfig.modelPath;
      if (filesystem::exists(modelInModelsDir)) {
        runConfig.modelPath = modelInModelsDir;
      }
    }
  }

  if (runConfig.serverMode) {
    applyAutoServerResourceConfig(runConfig);
  }

  if (runConfig.modelPath.empty()) {
    throw runtime_error("Model file is required. Use --model FILE");
  }

  // Verify model file exists
  ifstream modelFile(runConfig.modelPath.c_str(), ios::binary);
  if (!modelFile.good()) {
    throw runtime_error("Model file doesn't exist");
  }

  if (!modelConfigPath) {
    runConfig.modelConfigPath =
        filesystem::path(runConfig.modelPath.string() + ".json");
  } else {
    runConfig.modelConfigPath = modelConfigPath.value();
  }

  // Verify model config exists
  ifstream modelConfigFile(runConfig.modelConfigPath.c_str());
  if (!modelConfigFile.good()) {
    throw runtime_error("Model config doesn't exist");
  }
}
