#ifndef PROJECT_CONTROL_PROTOCOL_HPP_
#define PROJECT_CONTROL_PROTOCOL_HPP_

#include <cstdint>
#include <string>

#include "project/expected.hpp"
#include "project/switch_metrics.hpp"

namespace project
{
  constexpr uint32_t WIRELAB_CONTROL_API_VERSION = 1;

  enum class AnalyzerBackend
  {
    Cpu,
    Cuda
  };

  enum class ControlCommand
  {
    GetSwitchState,
    StartBenchmark,
    StopRun
  };

  enum class ControlValidationError
  {
    UnsupportedApiVersion,
    MissingRequestId,
    InvalidBenchmarkConfiguration
  };

  struct BenchmarkParameters
  {
    std::string scenario;
    AnalyzerBackend backend = AnalyzerBackend::Cpu;
    uint32_t batch_size = 1;
    uint32_t duration_seconds = 0;
    uint64_t seed = 1;
  };

  struct ControlRequest
  {
    uint32_t api_version = WIRELAB_CONTROL_API_VERSION;
    std::string request_id;
    ControlCommand command = ControlCommand::GetSwitchState;
    uint64_t topology_revision = 0;
    BenchmarkParameters benchmark;
  };

  struct ControlReply
  {
    uint32_t api_version = WIRELAB_CONTROL_API_VERSION;
    std::string request_id;
    bool accepted = false;
    std::string operation_id;
    std::string error;
  };

  struct SwitchMetricsEvent
  {
    uint32_t api_version = WIRELAB_CONTROL_API_VERSION;
    uint64_t event_sequence = 0;
    uint64_t topology_revision = 0;
    SwitchMetricsSnapshot metrics;
  };

  [[nodiscard]] const char* to_string(AnalyzerBackend backend) noexcept;
  [[nodiscard]] const char* to_string(ControlCommand command) noexcept;
  [[nodiscard]] const char* to_string(ControlValidationError error) noexcept;
  [[nodiscard]] expected<void, ControlValidationError> validate(const ControlRequest& request) noexcept;
  [[nodiscard]] std::string to_json(const ControlRequest& request);
  [[nodiscard]] std::string to_json(const ControlReply& reply);
  [[nodiscard]] std::string to_json(const SwitchMetricsEvent& event);
}

#endif
