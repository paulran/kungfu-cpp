#pragma once

#include <string>

namespace kungfu::wingchun::sim {

enum class MatchMode {
  Reject,
  Pend,
  Cancel,
  PartialFillAndCancel,
  PartialFill,
  Fill,
  Multiple
};

inline std::string match_mode_to_string(MatchMode mode) {
  switch (mode) {
  case MatchMode::Reject:
    return "reject";
  case MatchMode::Pend:
    return "pend";
  case MatchMode::Cancel:
    return "cancel";
  case MatchMode::PartialFillAndCancel:
    return "partialfillandcancel";
  case MatchMode::PartialFill:
    return "partialfill";
  case MatchMode::Fill:
    return "fill";
  case MatchMode::Multiple:
    return "multiple_transactions";
  default:
    return "fill";
  }
}

inline MatchMode match_mode_from_string(const std::string &str) {
  if (str == "reject")
    return MatchMode::Reject;
  if (str == "pend")
    return MatchMode::Pend;
  if (str == "cancel")
    return MatchMode::Cancel;
  if (str == "partialfillandcancel")
    return MatchMode::PartialFillAndCancel;
  if (str == "partialfill")
    return MatchMode::PartialFill;
  if (str == "multiple_transactions")
    return MatchMode::Multiple;
  return MatchMode::Fill;
}

} // namespace kungfu::wingchun::sim
