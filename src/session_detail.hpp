#ifndef PROJECT_WIRELAB_SESSION_DETAIL_HPP_
#define PROJECT_WIRELAB_SESSION_DETAIL_HPP_

// Helpers shared by the Session translation units. Not installed and not part
// of any public contract.

#include <cstdint>
#include <string>

#include "wirelab/anomaly_detector.hpp"
#include "wirelab/policy_enforcer.hpp"

namespace wirelab::session_detail
{
  // Detector thresholds the desktop lab runs with. Tuned for a per-tick batch
  // rather than a live switch, so a demo produces anomalies within a few ticks.
  [[nodiscard]] AnomalyDetectorConfig desktop_anomaly_config() noexcept;

  [[nodiscard]] std::string ipv4_string(uint32_t address);
  [[nodiscard]] std::string yaml_quote(const std::string& text);
  [[nodiscard]] std::string enforcement_summary(const EnforcementAction& action);

  // The frontend labels a backend the way a person reads it and the benchmark
  // engine names it the way the CLI spells it; these two are the only place the
  // spellings meet.
  [[nodiscard]] std::string benchmark_backend_id(const std::string& label);
  [[nodiscard]] std::string benchmark_backend_label(const std::string& id);

  // Native file dialogs hand back file:// URLs; everything else hands back
  // paths. Accept both.
  [[nodiscard]] std::string strip_file_url(const std::string& path);
  [[nodiscard]] std::string file_name_of(const std::string& path);

  // %.<precision>f, matching the fixed-notation formatting the Qt frontend used
  // for the traffic summary line.
  [[nodiscard]] std::string format_fixed(double value, int precision);
  // Six significant digits, the shortest form a spreadsheet reads back cleanly.
  [[nodiscard]] std::string format_general(double value);
  // Shortest form that round-trips through a JSON parser.
  [[nodiscard]] std::string format_json_number(double value);
  [[nodiscard]] std::string json_escape(const std::string& text);
}  // namespace wirelab::session_detail

#endif
