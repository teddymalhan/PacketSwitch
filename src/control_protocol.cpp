#include "project/control_protocol.hpp"

#include <iomanip>
#include <sstream>
#include <string_view>

namespace project
{
  namespace
  {
    std::string json_string(std::string_view value)
    {
      std::ostringstream result;
      result << '"';
      for (const unsigned char character : value)
      {
        switch (character)
        {
          case '"': result << "\\\""; break;
          case '\\': result << "\\\\"; break;
          case '\b': result << "\\b"; break;
          case '\f': result << "\\f"; break;
          case '\n': result << "\\n"; break;
          case '\r': result << "\\r"; break;
          case '\t': result << "\\t"; break;
          default:
            if (character < 0x20U)
            {
              result << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                     << static_cast<unsigned int>(character) << std::dec << std::setfill(' ');
            }
            else
            {
              result << static_cast<char>(character);
            }
        }
      }
      result << '"';
      return result.str();
    }

    bool is_valid_benchmark(const BenchmarkParameters& benchmark) noexcept
    {
      constexpr uint32_t MAX_BATCH_SIZE = 8192;
      return !benchmark.scenario.empty() && benchmark.batch_size > 0 && benchmark.batch_size <= MAX_BATCH_SIZE &&
             benchmark.duration_seconds > 0;
    }
  }

  const char* to_string(AnalyzerBackend backend) noexcept
  {
    return backend == AnalyzerBackend::Cpu ? "cpu" : "cuda";
  }

  const char* to_string(ControlCommand command) noexcept
  {
    switch (command)
    {
      case ControlCommand::GetSwitchState: return "get_switch_state";
      case ControlCommand::StartBenchmark: return "start_benchmark";
      case ControlCommand::StopRun: return "stop_run";
    }
    return "unknown";
  }

  const char* to_string(ControlValidationError error) noexcept
  {
    switch (error)
    {
      case ControlValidationError::UnsupportedApiVersion: return "unsupported API version";
      case ControlValidationError::MissingRequestId: return "request_id is required";
      case ControlValidationError::InvalidBenchmarkConfiguration: return "invalid benchmark configuration";
    }
    return "unknown control validation error";
  }

  expected<void, ControlValidationError> validate(const ControlRequest& request) noexcept
  {
    if (request.api_version != WIRELAB_CONTROL_API_VERSION)
    {
      return unexpected(ControlValidationError::UnsupportedApiVersion);
    }
    if (request.request_id.empty())
    {
      return unexpected(ControlValidationError::MissingRequestId);
    }
    if (request.command == ControlCommand::StartBenchmark && !is_valid_benchmark(request.benchmark))
    {
      return unexpected(ControlValidationError::InvalidBenchmarkConfiguration);
    }
    return expected<void, ControlValidationError>();
  }

  std::string to_json(const ControlRequest& request)
  {
    std::ostringstream result;
    result << "{\"api_version\":" << request.api_version << ",\"request_id\":" << json_string(request.request_id)
           << ",\"command\":" << json_string(to_string(request.command))
           << ",\"topology_revision\":" << request.topology_revision;
    if (request.command == ControlCommand::StartBenchmark)
    {
      result << ",\"parameters\":{\"scenario\":" << json_string(request.benchmark.scenario)
             << ",\"backend\":" << json_string(to_string(request.benchmark.backend))
             << ",\"batch_size\":" << request.benchmark.batch_size
             << ",\"duration_seconds\":" << request.benchmark.duration_seconds << ",\"seed\":" << request.benchmark.seed
             << '}';
    }
    result << '}';
    return result.str();
  }

  std::string to_json(const ControlReply& reply)
  {
    std::ostringstream result;
    result << "{\"api_version\":" << reply.api_version << ",\"request_id\":" << json_string(reply.request_id)
           << ",\"accepted\":" << (reply.accepted ? "true" : "false");
    if (reply.accepted)
    {
      result << ",\"operation_id\":" << json_string(reply.operation_id);
    }
    else
    {
      result << ",\"error\":" << json_string(reply.error);
    }
    result << '}';
    return result.str();
  }

  std::string to_json(const SwitchMetricsEvent& event)
  {
    const auto& metrics = event.metrics;
    std::ostringstream result;
    result << "{\"api_version\":" << event.api_version << ",\"event_sequence\":" << event.event_sequence
           << ",\"topology_revision\":" << event.topology_revision << ",\"event\":\"switch_metrics\",\"metrics\":{"
           << "\"received_packets\":" << metrics.received_packets << ",\"received_bytes\":" << metrics.received_bytes
           << ",\"forwarded_packets\":" << metrics.forwarded_packets << ",\"forwarded_bytes\":" << metrics.forwarded_bytes
           << ",\"broadcast_packets\":" << metrics.broadcast_packets
           << ",\"unknown_unicast_packets\":" << metrics.unknown_unicast_packets
           << ",\"dropped_packets\":" << metrics.dropped_packets
           << ",\"malformed_packets\":" << metrics.malformed_packets << ",\"learned_macs\":" << metrics.learned_macs
           << "}}";
    return result.str();
  }
}
