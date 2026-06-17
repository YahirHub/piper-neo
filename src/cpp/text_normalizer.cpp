#include "text_normalizer.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <stdexcept>


namespace piper {
namespace {

std::string lowerAsciiCopy(std::string value) {
  for (auto &c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
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

bool isAsciiAlphaNumericOrUnderscore(char c) {
  const auto value = static_cast<unsigned char>(c);
  return std::isalnum(value) || value == '_';
}

bool isWholeWordBoundaryBefore(const std::string &text, std::size_t index) {
  return index == 0 || !isAsciiAlphaNumericOrUnderscore(text[index - 1]);
}

bool isWholeWordBoundaryAfter(const std::string &text, std::size_t index) {
  return index >= text.size() || !isAsciiAlphaNumericOrUnderscore(text[index]);
}

bool asciiStartsWithAt(const std::string &text, const std::string &needle,
                       std::size_t index, bool caseSensitive) {
  if (needle.empty() || index + needle.size() > text.size()) {
    return false;
  }

  for (std::size_t i = 0; i < needle.size(); ++i) {
    const auto a = text[index + i];
    const auto b = needle[i];
    if (caseSensitive) {
      if (a != b) {
        return false;
      }
    } else if (std::tolower(static_cast<unsigned char>(a)) !=
               std::tolower(static_cast<unsigned char>(b))) {
      return false;
    }
  }

  return true;
}

std::string applyReplacementRule(const std::string &text,
                                 const TextReplacementRule &rule) {
  if (rule.from.empty()) {
    return text;
  }

  std::string out;
  out.reserve(text.size());
  std::size_t i = 0;
  while (i < text.size()) {
    const bool boundaryOk = !rule.wholeWord ||
                            (isWholeWordBoundaryBefore(text, i) &&
                             isWholeWordBoundaryAfter(text, i + rule.from.size()));
    if (boundaryOk && asciiStartsWithAt(text, rule.from, i, rule.caseSensitive)) {
      out += rule.to;
      i += rule.from.size();
      continue;
    }

    out.push_back(text[i]);
    ++i;
  }

  return out;
}

std::string applyCustomReplacements(const std::string &text,
                                    const std::vector<TextReplacementRule> &rules) {
  if (rules.empty()) {
    return text;
  }

  auto sortedRules = rules;
  std::stable_sort(sortedRules.begin(), sortedRules.end(),
                   [](const TextReplacementRule &a, const TextReplacementRule &b) {
                     if (a.priority != b.priority) {
                       return a.priority > b.priority;
                     }
                     return a.from.size() > b.from.size();
                   });

  std::string current = text;
  for (const auto &rule : sortedRules) {
    current = applyReplacementRule(current, rule);
  }

  return current;
}

std::string joinWords(const std::vector<std::string> &words, const std::string &sep = " ") {
  std::string out;
  for (std::size_t i = 0; i < words.size(); ++i) {
    if (i > 0) {
      out += sep;
    }
    out += words[i];
  }
  return out;
}

std::string digitToSpanish(char digit) {
  switch (digit) {
  case '0': return "cero";
  case '1': return "uno";
  case '2': return "dos";
  case '3': return "tres";
  case '4': return "cuatro";
  case '5': return "cinco";
  case '6': return "seis";
  case '7': return "siete";
  case '8': return "ocho";
  case '9': return "nueve";
  default: return std::string(1, digit);
  }
}

std::string digitsToSpanish(const std::string &digits) {
  std::vector<std::string> words;
  words.reserve(digits.size());
  for (char digit : digits) {
    words.push_back(digitToSpanish(digit));
  }
  return joinWords(words);
}

std::string twoDigitsToSpanish(int value) {
  static const char *units[] = {"cero", "uno", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve"};
  static const char *tens[] = {"", "", "veinte", "treinta", "cuarenta", "cincuenta", "sesenta", "setenta", "ochenta", "noventa"};

  if (value < 10) {
    return units[value];
  }
  if (value == 10) return "diez";
  if (value == 11) return "once";
  if (value == 12) return "doce";
  if (value == 13) return "trece";
  if (value == 14) return "catorce";
  if (value == 15) return "quince";
  if (value < 20) return std::string("dieci") + units[value - 10];
  if (value == 20) return "veinte";
  if (value < 30) return std::string("veinti") + units[value - 20];

  const int ten = value / 10;
  const int unit = value % 10;
  if (unit == 0) {
    return tens[ten];
  }
  return std::string(tens[ten]) + " y " + units[unit];
}

std::string threeDigitsToSpanish(int value) {
  if (value < 100) {
    return twoDigitsToSpanish(value);
  }
  if (value == 100) {
    return "cien";
  }

  static const char *hundreds[] = {"", "ciento", "doscientos", "trescientos", "cuatrocientos",
                                   "quinientos", "seiscientos", "setecientos", "ochocientos", "novecientos"};
  const int hundred = value / 100;
  const int rest = value % 100;
  if (rest == 0) {
    return hundreds[hundred];
  }
  return std::string(hundreds[hundred]) + " " + twoDigitsToSpanish(rest);
}

std::string integerToSpanish(long long value) {
  if (value == 0) {
    return "cero";
  }
  if (value < 0) {
    return "menos " + integerToSpanish(-value);
  }
  if (value < 1000) {
    return threeDigitsToSpanish(static_cast<int>(value));
  }
  if (value < 1000000) {
    const auto thousands = value / 1000;
    const auto rest = value % 1000;
    std::string out = (thousands == 1) ? "mil" : (integerToSpanish(thousands) + " mil");
    if (rest > 0) {
      out += " " + threeDigitsToSpanish(static_cast<int>(rest));
    }
    return out;
  }
  if (value < 1000000000000LL) {
    const auto millions = value / 1000000;
    const auto rest = value % 1000000;
    std::string out = (millions == 1) ? "un millón" : (integerToSpanish(millions) + " millones");
    if (rest > 0) {
      out += " " + integerToSpanish(rest);
    }
    return out;
  }

  return digitsToSpanish(std::to_string(value));
}

long long parseIntegerSafe(const std::string &digits) {
  try {
    return std::stoll(digits);
  } catch (...) {
    return 0;
  }
}

std::string numericGroupToSpanish(const std::string &digits) {
  if (digits.empty()) {
    return {};
  }
  if (digits.size() > 1 && digits[0] == '0') {
    return digitsToSpanish(digits);
  }
  if (digits.size() > 12) {
    return digitsToSpanish(digits);
  }
  return integerToSpanish(parseIntegerSafe(digits));
}

std::string decimalToSpanish(const std::string &integerPart, const std::string &fractionPart) {
  std::string out = numericGroupToSpanish(integerPart) + " punto ";
  if (fractionPart.size() > 1 && fractionPart[0] == '0') {
    out += digitsToSpanish(fractionPart);
  } else {
    out += numericGroupToSpanish(fractionPart);
  }
  return out;
}

std::string decimalDigitsToSpeechText(const std::string &integerPart,
                                      const std::string &fractionPart) {
  if (fractionPart.empty()) {
    return integerPart;
  }
  return integerPart + " punto " + fractionPart;
}

std::string versionToSpanish(const std::string &version) {
  bool hasVPrefix = !version.empty() && (version[0] == 'v' || version[0] == 'V');
  const auto raw = hasVPrefix ? version.substr(1) : version;
  std::vector<std::string> parts;
  std::stringstream ss(raw);
  std::string item;
  while (std::getline(ss, item, '.')) {
    parts.push_back(numericGroupToSpanish(item));
  }
  auto out = joinWords(parts, " punto ");
  if (hasVPrefix) {
    out = "versión " + out;
  }
  return out;
}

std::string emailToSpanish(const std::string &email) {
  std::string out;
  for (char c : email) {
    if (c == '@') {
      out += " arroba ";
    } else if (c == '.') {
      out += " punto ";
    } else if (c == '_') {
      out += " guion bajo ";
    } else if (c == '-') {
      out += " guion ";
    } else if (c == '+') {
      out += " más ";
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string urlToSpanish(const std::string &url) {
  std::string value = url;
  auto lower = lowerAsciiCopy(value);
  if (lower.rfind("https://", 0) == 0) {
    value = value.substr(8);
  } else if (lower.rfind("http://", 0) == 0) {
    value = value.substr(7);
  } else if (lower.rfind("www.", 0) == 0) {
    value = value.substr(4);
  }

  std::string out = "enlace ";
  for (char c : value) {
    if (c == '.') {
      out += " punto ";
    } else if (c == '/') {
      out += " diagonal ";
    } else if (c == '-') {
      out += " guion ";
    } else if (c == '_') {
      out += " guion bajo ";
    } else if (c == '?') {
      out += " signo de pregunta ";
    } else if (c == '&') {
      out += " y ";
    } else if (c == '=') {
      out += " igual ";
    } else if (c == ':') {
      out += " dos puntos ";
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool isTerminalPunctuation(char c) {
  return c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == ')';
}

std::string stripTerminalPunctuation(std::string &token) {
  std::string trailing;
  while (!token.empty() && isTerminalPunctuation(token.back())) {
    trailing.insert(trailing.begin(), token.back());
    token.pop_back();
  }
  return trailing;
}

std::string currencyToSpanish(const std::string &currencyRaw,
                              const std::string &suffixRaw,
                              const std::string &integerPart,
                              const std::string &fractionPart) {
  const auto currency = lowerAsciiCopy(trimCopy(currencyRaw));
  const auto suffix = lowerAsciiCopy(trimCopy(suffixRaw));
  const bool isUsd = (currency == "usd") || (suffix == "usd") ||
                     (suffix == "dolares") || (suffix == "dólares");

  // No se convierten decimales monetarios a centavos. En contenido hablado
  // para redes, noticias y tutoriales, `$99.50 pesos` debe conservarse como
  // `99 punto 50 pesos`, sin inventar `con cincuenta centavos`.
  std::string out = decimalDigitsToSpeechText(integerPart, fractionPart);
  out += isUsd ? " dólares" : " pesos";
  return out;
}

struct BuiltinNormalizationResult {
  std::string text;
  std::vector<std::string> protectedSegments;
};

std::string makeProtectedMarker(std::size_t index) {
  return std::string("\x1F") + std::to_string(index) + "\x1F";
}

void appendProtectedSegment(BuiltinNormalizationResult &result,
                            const std::string &speechText) {
  const auto index = result.protectedSegments.size();
  result.text += makeProtectedMarker(index);
  result.protectedSegments.push_back(speechText);
}

std::string restoreProtectedSegments(const std::string &text,
                                     const std::vector<std::string> &segments) {
  std::string out;
  out.reserve(text.size());

  for (std::size_t i = 0; i < text.size();) {
    if (text[i] == '\x1F') {
      const auto end = text.find('\x1F', i + 1);
      if (end != std::string::npos) {
        const auto indexText = text.substr(i + 1, end - i - 1);
        try {
          const auto index = static_cast<std::size_t>(std::stoul(indexText));
          if (index < segments.size()) {
            out += segments[index];
            i = end + 1;
            continue;
          }
        } catch (...) {
        }
      }
    }

    out.push_back(text[i]);
    ++i;
  }

  return out;
}

bool prefixRegexMatch(const std::string &text, std::size_t index,
                      const std::regex &regex, std::smatch &match) {
  const auto begin = text.cbegin() + static_cast<std::ptrdiff_t>(index);
  return std::regex_search(begin, text.cend(), match, regex,
                           std::regex_constants::match_continuous);
}

bool isSafeLeftBoundary(const std::string &text, std::size_t index) {
  return index == 0 || !isAsciiAlphaNumericOrUnderscore(text[index - 1]);
}

bool isSafeRightBoundary(const std::string &text, std::size_t index) {
  return index >= text.size() || !isAsciiAlphaNumericOrUnderscore(text[index]);
}

BuiltinNormalizationResult normalizeBuiltins(const std::string &text,
                                              const TextNormalizationBuiltinConfig &builtin) {
  static const std::regex urlRegex(R"(((?:https?://|www\.)[A-Za-z0-9._~:/?#\[\]@!$&'()*+,;=%-]+))",
                                  std::regex::ECMAScript | std::regex::icase);
  static const std::regex emailRegex(R"(([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}))",
                                    std::regex::ECMAScript);
  static const std::regex versionRegex(R"((v?\d+(?:\.\d+){2,}))",
                                      std::regex::ECMAScript | std::regex::icase);
  static const std::regex currencyPrefixRegex(R"((\$|MXN\s+|USD\s+)(\d+)(?:\.(\d+))?(?:\s*(pesos?|mxn|USD|dolares|dólares))?)",
                                             std::regex::ECMAScript | std::regex::icase);
  static const std::regex percentRegex(R"((\d+)\.(\d+)%|(\d+)%)",
                                      std::regex::ECMAScript);
  static const std::regex decimalRegex(R"((\d+)\.(\d+))",
                                      std::regex::ECMAScript);

  BuiltinNormalizationResult result;
  result.text.reserve(text.size());

  for (std::size_t i = 0; i < text.size();) {
    std::smatch match;

    if (builtin.urls && prefixRegexMatch(text, i, urlRegex, match)) {
      const auto rawToken = match.str(1);
      auto token = rawToken;
      const auto trailing = stripTerminalPunctuation(token);
      appendProtectedSegment(result, urlToSpanish(token));
      result.text += trailing;
      i += rawToken.size();
      continue;
    }

    if (builtin.emails && prefixRegexMatch(text, i, emailRegex, match) && isSafeLeftBoundary(text, i)) {
      const auto token = match.str(1);
      appendProtectedSegment(result, emailToSpanish(token));
      i += token.size();
      continue;
    }

    if (builtin.versions && prefixRegexMatch(text, i, versionRegex, match) &&
        isSafeLeftBoundary(text, i) && isSafeRightBoundary(text, i + match.str(1).size())) {
      const auto token = match.str(1);
      appendProtectedSegment(result, versionToSpanish(token));
      i += token.size();
      continue;
    }

    if (builtin.currency && prefixRegexMatch(text, i, currencyPrefixRegex, match) && isSafeLeftBoundary(text, i)) {
      const auto token = match.str(0);
      appendProtectedSegment(result, currencyToSpanish(match.str(1), match.str(4), match.str(2), match.str(3)));
      i += token.size();
      continue;
    }

    if (builtin.percentages && prefixRegexMatch(text, i, percentRegex, match) && isSafeLeftBoundary(text, i)) {
      const auto token = match.str(0);
      if (!match.str(1).empty()) {
        appendProtectedSegment(result, decimalDigitsToSpeechText(match.str(1), match.str(2)) + " por ciento");
      } else {
        appendProtectedSegment(result, match.str(3) + " por ciento");
      }
      i += token.size();
      continue;
    }

    if (builtin.decimals && prefixRegexMatch(text, i, decimalRegex, match) &&
        isSafeLeftBoundary(text, i) && isSafeRightBoundary(text, i + match.str(0).size())) {
      const auto token = match.str(0);
      appendProtectedSegment(result, decimalToSpanish(match.str(1), match.str(2)));
      i += token.size();
      continue;
    }

    result.text.push_back(text[i]);
    ++i;
  }

  return result;
}

void parseBuiltinFlags(const nlohmann::json &root, TextNormalizationBuiltinConfig &builtin) {
  if (!root.is_object()) {
    return;
  }
  builtin.decimals = root.value("decimals", builtin.decimals);
  builtin.versions = root.value("versions", builtin.versions);
  builtin.percentages = root.value("percentages", builtin.percentages);
  builtin.currency = root.value("currency", builtin.currency);
  builtin.urls = root.value("urls", builtin.urls);
  builtin.emails = root.value("emails", builtin.emails);
}

std::vector<TextReplacementRule> parseReplacementArray(const nlohmann::json &items) {
  std::vector<TextReplacementRule> rules;
  if (!items.is_array()) {
    return rules;
  }

  for (const auto &item : items) {
    TextReplacementRule rule;
    if (item.is_object()) {
      rule.from = item.value("from", "");
      rule.to = item.value("to", "");
      rule.caseSensitive = item.value("case_sensitive", false);
      rule.wholeWord = item.value("whole_word", true);
      rule.priority = item.value("priority", 0);
      rule.note = item.value("note", "");
    } else if (item.is_array() && item.size() >= 2 && item[0].is_string() && item[1].is_string()) {
      rule.from = item[0].get<std::string>();
      rule.to = item[1].get<std::string>();
    }

    rule.from = trimCopy(rule.from);
    if (!rule.from.empty()) {
      rules.push_back(std::move(rule));
    }
  }

  return rules;
}

} // namespace

void parseTextNormalizationConfig(const nlohmann::json &configRoot,
                                  TextNormalizationConfig &config) {
  // A classic Piper JSON must not be modified implicitly. Normalization is
  // enabled only when the model explicitly declares neo.text_normalization, or
  // when it carries legacy modelcard.replacements from older managers.
  config = TextNormalizationConfig{};

  try {
    bool hasNeoTextNormalization = false;

    if (configRoot.contains("neo") && configRoot["neo"].is_object()) {
      const auto &neo = configRoot["neo"];
      if (neo.contains("text_normalization") && neo["text_normalization"].is_object()) {
        hasNeoTextNormalization = true;
        const auto &tn = neo["text_normalization"];

        config.enabled = tn.value("enabled", true);
        config.locale = tn.value("locale", config.locale);

        // In the explicit Piper Neo schema, builtins default to enabled unless
        // the JSON turns individual flags off.
        config.builtin.decimals = true;
        config.builtin.versions = true;
        config.builtin.percentages = true;
        config.builtin.currency = true;
        config.builtin.urls = true;
        config.builtin.emails = true;
        if (tn.contains("builtin")) {
          parseBuiltinFlags(tn["builtin"], config.builtin);
        }

        if (tn.contains("replacements")) {
          auto parsed = parseReplacementArray(tn["replacements"]);
          config.replacements.insert(config.replacements.end(), parsed.begin(), parsed.end());
        }
      }
    }

    // Legacy managers used modelcard.replacements as [[from, to], ...]. Keep it
    // supported, but do not enable smart builtins unless neo.text_normalization
    // exists. This prevents old Piper JSON files from changing unexpectedly.
    if (configRoot.contains("modelcard") && configRoot["modelcard"].is_object()) {
      const auto &card = configRoot["modelcard"];
      if (card.contains("replacements")) {
        auto parsed = parseReplacementArray(card["replacements"]);
        if (!parsed.empty() && !hasNeoTextNormalization) {
          config.enabled = true;
        }
        config.replacements.insert(config.replacements.end(), parsed.begin(), parsed.end());
      }
    }
  } catch (const std::exception &e) {
    (void)e;
    config = TextNormalizationConfig{};
  }
}

std::string normalizeTextForSpeech(const std::string &text,
                                   const TextNormalizationConfig &config) {
  if (!config.enabled || text.empty()) {
    return text;
  }

  auto builtinResult = normalizeBuiltins(text, config.builtin);
  auto normalized = applyCustomReplacements(builtinResult.text, config.replacements);
  return restoreProtectedSegments(normalized, builtinResult.protectedSegments);
}

} // namespace piper
