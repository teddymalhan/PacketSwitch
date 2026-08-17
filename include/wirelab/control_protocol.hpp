#ifndef PROJECT_CONTROL_PROTOCOL_HPP_
#define PROJECT_CONTROL_PROTOCOL_HPP_

#include <cstdint>
#include <string>
#include <string_view>

#include "wirelab/anomaly_detector.hpp"
#include "wirelab/fault_engine.hpp"
#include "wirelab/policy_engine.hpp"
#include "wirelab/switch_metrics.hpp"
#include "wirelab/topology.hpp"

namespace wirelab
{
  constexpr uint32_t WIRELAB_CONTROL_API_VERSION = 1;

  enum class AnalyzerBackend
  {
    Cpu,
    Cuda
  };

  enum class ControlCommand
  {
    LoadTopology,
    GetSwitchState,
    GetActiveFaults,
    StartBenchmark,
    StopRun,
    SetPortFault,
    ClearPortFault,
    SetLinkFault,
    ClearLinkFault
  };

  enum class ControlValidationError
  {
    UnsupportedApiVersion,
    MissingRequestId,
    InvalidBenchmarkConfiguration
  };

  enum class ControlParseError
  {
    MalformedJson,
    InvalidField,
    MissingRequiredField
  };

  struct BenchmarkParameters
  {
    std::string scenario;
    AnalyzerBackend backend = AnalyzerBackend::Cpu;
    uint32_t batch_size = 1;
    uint32_t duration_seconds = 0;
    uint64_t seed = 1;
  };

  struct TopologyParameters
  {
    std::string path;
  };

  struct FaultParameters
  {
    std::string port_id;
    std::string first_endpoint;
    std::string second_endpoint;
    FaultConfiguration configuration;
  };

  struct ControlRequest
  {
    uint32_t api_version = WIRELAB_CONTROL_API_VERSION;
    std::string request_id;
    ControlCommand command = ControlCommand::GetSwitchState;
    uint64_t topology_revision = 0;
    BenchmarkParameters benchmark;
    TopologyParameters topology;
    FaultParameters fault;
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

  struct FaultStateEvent
  {
    uint32_t api_version = WIRELAB_CONTROL_API_VERSION;
    uint64_t event_sequence = 0;
    uint64_t topology_revision = 0;
    std::string first_endpoint;
    std::string second_endpoint;
    FaultConfiguration configuration;
    bool active = false;
  };

  struct TopologyStateEvent
  {
    uint32_t api_version = WIRELAB_CONTROL_API_VERSION;
    uint64_t event_sequence = 0;
    uint64_t topology_revision = 0;
    std::string name;
    std::vector<TopologyNode> nodes;
    std::vector<TopologyLink> links;
  };

  struct AnomalyDetectedEvent
  {
    uint32_t api_version = WIRELAB_CONTROL_API_VERSION;
    uint64_t event_sequence = 0;
    uint64_t topology_revision = 0;
    AnomalyEvent anomaly;
  };

  struct PolicyActionEvent
  {
    uint32_t api_version = WIRELAB_CONTROL_API_VERSION;
    uint64_t event_sequence = 0;
    uint64_t topology_revision = 0;
    PolicyDecision decision;
  };

  // Which client the switch decided owns a topology port. A UDP dataplane
  // offers no stable identity, so the binding is reported rather than assumed.
  struct PortBinding
  {
    std::string port_id;
    std::string endpoint;
  };

  struct SupervisionStateEvent
  {
    uint32_t api_version = WIRELAB_CONTROL_API_VERSION;
    uint64_t event_sequence = 0;
    uint64_t topology_revision = 0;
    uint64_t analysed_frames = 0;
    uint64_t blocked_frames = 0;
    std::vector<PortBinding> bindings;
  };

  [[nodiscard]] const char* to_string(AnalyzerBackend backend) noexcept;
  [[nodiscard]] const char* to_string(ControlCommand command) noexcept;
  [[nodiscard]] const char* to_string(ControlValidationError error) noexcept;
  [[nodiscard]] const char* to_string(ControlParseError error) noexcept;
  [[nodiscard]] expected<void, ControlValidationError> validate(const ControlRequest& request) noexcept;
  [[nodiscard]] expected<ControlRequest, ControlParseError> control_request_from_json(std::string_view json);
  [[nodiscard]] std::string to_json(const ControlRequest& request);
  [[nodiscard]] std::string to_json(const ControlReply& reply);
  [[nodiscard]] std::string to_json(const SwitchMetricsEvent& event);
  [[nodiscard]] std::string to_json(const FaultStateEvent& event);
  [[nodiscard]] std::string to_json(const TopologyStateEvent& event);
  [[nodiscard]] std::string to_json(const AnomalyDetectedEvent& event);
  [[nodiscard]] std::string to_json(const PolicyActionEvent& event);
  [[nodiscard]] std::string to_json(const SupervisionStateEvent& event);
}  // namespace wirelab

#endif
