#ifndef PIPER_TEXT_NORMALIZER_H_
#define PIPER_TEXT_NORMALIZER_H_

#include <string>
#include <vector>

#include "json.hpp"

namespace piper {

struct TextReplacementRule {
  std::string from;
  std::string to;
  bool caseSensitive = false;
  bool wholeWord = true;
  int priority = 0;
  std::string note;
};

struct TextNormalizationBuiltinConfig {
  bool decimals = true;
  bool versions = true;
  bool percentages = true;
  bool currency = true;
  bool urls = true;
  bool emails = true;
};

struct TextNormalizationConfig {
  bool enabled = true;
  std::string locale = "es-MX";
  TextNormalizationBuiltinConfig builtin;
  std::vector<TextReplacementRule> replacements;
};

void parseTextNormalizationConfig(const nlohmann::json &configRoot,
                                  TextNormalizationConfig &config);

std::string normalizeTextForSpeech(const std::string &text,
                                   const TextNormalizationConfig &config);

} // namespace piper

#endif // PIPER_TEXT_NORMALIZER_H_
