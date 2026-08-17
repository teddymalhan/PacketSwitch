#include "wirelab/control_protocol.hpp"

#include <chrono>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

namespace wirelab
{
  namespace
  {
    class ControlRequestParser
    {
     public:
      explicit ControlRequestParser(std::string_view input) noexcept : input_(input)
      {
      }

      expected<ControlRequest, ControlParseError> parse()
      {
        ControlRequest request;
        bool has_api_version = false;
        bool has_request_id = false;
        bool has_command = false;
        bool has_topology_revision = false;
        bool has_parameters = false;

        if (!consume('{'))
        {
          return unexpected(error_);
        }
        skip_whitespace();
        if (consume('}'))
        {
          return unexpected(ControlParseError::MissingRequiredField);
        }

        while (true)
        {
          std::string key;
          if (!read_string(key) || !consume(':'))
          {
            return unexpected(error_);
          }

          if (key == "api_version" && !has_api_version)
          {
            has_api_version = read_uint(request.api_version);
          }
          else if (key == "request_id" && !has_request_id)
          {
            has_request_id = read_string(request.request_id);
          }
          else if (key == "command" && !has_command)
          {
            has_command = read_command(request.command);
          }
          else if (key == "topology_revision" && !has_topology_revision)
          {
            has_topology_revision = read_uint(request.topology_revision);
          }
          else if (key == "parameters" && !has_parameters)
          {
            has_parameters = read_parameters(request);
          }
          else
          {
            return unexpected(ControlParseError::InvalidField);
          }

          if (!has_api_version && key == "api_version")
          {
            return unexpected(error_);
          }
          if (!has_request_id && key == "request_id")
          {
            return unexpected(error_);
          }
          if (!has_command && key == "command")
          {
            return unexpected(error_);
          }
          if (!has_topology_revision && key == "topology_revision")
          {
            return unexpected(error_);
          }
          if (!has_parameters && key == "parameters")
          {
            return unexpected(error_);
          }

          skip_whitespace();
          if (consume('}'))
          {
            break;
          }
          if (!consume(','))
          {
            return unexpected(error_);
          }
        }

        skip_whitespace();
        if (position_ != input_.size())
        {
          return unexpected(ControlParseError::MalformedJson);
        }
        if (!has_api_version || !has_request_id || !has_command || !has_topology_revision)
        {
          return unexpected(ControlParseError::MissingRequiredField);
        }
        const bool requires_parameters =
            request.command == ControlCommand::LoadTopology || request.command == ControlCommand::StartBenchmark ||
            request.command == ControlCommand::SetPortFault || request.command == ControlCommand::ClearPortFault ||
            request.command == ControlCommand::SetLinkFault || request.command == ControlCommand::ClearLinkFault;
        if (!has_parameters && requires_parameters)
        {
          return unexpected(ControlParseError::MissingRequiredField);
        }
        if (has_parameters && !parameters_valid_for(request.command))
        {
          return unexpected(
              parameters_missing_required_for(request.command) ? ControlParseError::MissingRequiredField
                                                               : ControlParseError::InvalidField);
        }
        return request;
      }

     private:
      void skip_whitespace() noexcept
      {
        while (position_ < input_.size())
        {
          const char character = input_[position_];
          if (character != ' ' && character != '\n' && character != '\r' && character != '\t')
          {
            break;
          }
          ++position_;
        }
      }

      bool consume(char expected) noexcept
      {
        skip_whitespace();
        if (position_ == input_.size() || input_[position_] != expected)
        {
          error_ = ControlParseError::MalformedJson;
          return false;
        }
        ++position_;
        return true;
      }

      bool read_hex(uint32_t& value) noexcept
      {
        value = 0;
        for (unsigned int index = 0; index < 4; ++index)
        {
          if (position_ == input_.size())
          {
            error_ = ControlParseError::MalformedJson;
            return false;
          }
          const char character = input_[position_++];
          value <<= 4U;
          if (character >= '0' && character <= '9')
            value |= static_cast<uint32_t>(character - '0');
          else if (character >= 'a' && character <= 'f')
            value |= static_cast<uint32_t>(character - 'a' + 10);
          else if (character >= 'A' && character <= 'F')
            value |= static_cast<uint32_t>(character - 'A' + 10);
          else
          {
            error_ = ControlParseError::MalformedJson;
            return false;
          }
        }
        return true;
      }

      static void append_utf8(std::string& output, uint32_t code_point)
      {
        if (code_point <= 0x7FU)
        {
          output.push_back(static_cast<char>(code_point));
        }
        else if (code_point <= 0x7FFU)
        {
          output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
          output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        }
        else if (code_point <= 0xFFFFU)
        {
          output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
          output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
          output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        }
        else
        {
          output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
          output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
          output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
          output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        }
      }

      bool read_string(std::string& output)
      {
        output.clear();
        if (!consume('"'))
        {
          return false;
        }

        while (position_ < input_.size())
        {
          const unsigned char character = static_cast<unsigned char>(input_[position_++]);
          if (character == '"')
          {
            return true;
          }
          if (character < 0x20U)
          {
            error_ = ControlParseError::MalformedJson;
            return false;
          }
          if (character != '\\')
          {
            output.push_back(static_cast<char>(character));
            continue;
          }
          if (position_ == input_.size())
          {
            error_ = ControlParseError::MalformedJson;
            return false;
          }

          switch (input_[position_++])
          {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u':
            {
              uint32_t code_point = 0;
              if (!read_hex(code_point))
              {
                return false;
              }
              if (code_point >= 0xD800U && code_point <= 0xDBFFU)
              {
                if (position_ + 2 > input_.size() || input_[position_] != '\\' || input_[position_ + 1] != 'u')
                {
                  error_ = ControlParseError::MalformedJson;
                  return false;
                }
                position_ += 2;
                uint32_t low_surrogate = 0;
                if (!read_hex(low_surrogate) || low_surrogate < 0xDC00U || low_surrogate > 0xDFFFU)
                {
                  error_ = ControlParseError::MalformedJson;
                  return false;
                }
                code_point = 0x10000U + ((code_point - 0xD800U) << 10U) + (low_surrogate - 0xDC00U);
              }
              else if (code_point >= 0xDC00U && code_point <= 0xDFFFU)
              {
                error_ = ControlParseError::MalformedJson;
                return false;
              }
              append_utf8(output, code_point);
              break;
            }
            default: error_ = ControlParseError::MalformedJson; return false;
          }
        }
        error_ = ControlParseError::MalformedJson;
        return false;
      }

      template<typename Integer>
      bool read_uint(Integer& output) noexcept
      {
        skip_whitespace();
        if (position_ == input_.size() || input_[position_] < '0' || input_[position_] > '9')
        {
          error_ = ControlParseError::InvalidField;
          return false;
        }

        Integer value = 0;
        do
        {
          const auto digit = static_cast<Integer>(input_[position_++] - '0');
          if (value > (std::numeric_limits<Integer>::max() - digit) / 10)
          {
            error_ = ControlParseError::InvalidField;
            return false;
          }
          value = static_cast<Integer>(value * 10 + digit);
        } while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9');
        output = value;
        return true;
      }

      bool read_command(ControlCommand& command)
      {
        std::string value;
        if (!read_string(value))
        {
          return false;
        }
        if (value == "load_topology")
          command = ControlCommand::LoadTopology;
        else if (value == "get_switch_state")
          command = ControlCommand::GetSwitchState;
        else if (value == "get_active_faults")
          command = ControlCommand::GetActiveFaults;
        else if (value == "start_benchmark")
          command = ControlCommand::StartBenchmark;
        else if (value == "stop_run")
          command = ControlCommand::StopRun;
        else if (value == "set_port_fault")
          command = ControlCommand::SetPortFault;
        else if (value == "clear_port_fault")
          command = ControlCommand::ClearPortFault;
        else if (value == "set_link_fault")
          command = ControlCommand::SetLinkFault;
        else if (value == "clear_link_fault")
          command = ControlCommand::ClearLinkFault;
        else
        {
          error_ = ControlParseError::InvalidField;
          return false;
        }
        return true;
      }

      bool read_backend(AnalyzerBackend& backend)
      {
        std::string value;
        if (!read_string(value))
        {
          return false;
        }
        if (value == "cpu")
          backend = AnalyzerBackend::Cpu;
        else if (value == "cuda")
          backend = AnalyzerBackend::Cuda;
        else
        {
          error_ = ControlParseError::InvalidField;
          return false;
        }
        return true;
      }

      bool read_bool(bool& output) noexcept
      {
        skip_whitespace();
        if (input_.substr(position_, 4) == "true")
        {
          position_ += 4;
          output = true;
          return true;
        }
        if (input_.substr(position_, 5) == "false")
        {
          position_ += 5;
          output = false;
          return true;
        }
        error_ = ControlParseError::InvalidField;
        return false;
      }

      bool read_milliseconds(std::chrono::nanoseconds& output) noexcept
      {
        uint64_t milliseconds = 0;
        if (!read_uint(milliseconds) || milliseconds > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        {
          error_ = ControlParseError::InvalidField;
          return false;
        }
        output = std::chrono::milliseconds(milliseconds);
        return true;
      }

      bool read_parameters(ControlRequest& request)
      {
        if (!consume('{'))
        {
          return false;
        }
        skip_whitespace();
        if (consume('}'))
        {
          return true;
        }

        while (true)
        {
          std::string key;
          if (!read_string(key) || !consume(':'))
          {
            return false;
          }
          bool read = false;
          if (key == "topology_path" && !parameters_.topology_path)
          {
            parameters_.topology_path = true;
            read = read_string(request.topology.path);
          }
          else if (key == "scenario" && !parameters_.scenario)
          {
            parameters_.scenario = true;
            read = read_string(request.benchmark.scenario);
          }
          else if (key == "backend" && !parameters_.backend)
          {
            parameters_.backend = true;
            read = read_backend(request.benchmark.backend);
          }
          else if (key == "batch_size" && !parameters_.batch_size)
          {
            parameters_.batch_size = true;
            read = read_uint(request.benchmark.batch_size);
          }
          else if (key == "duration_seconds" && !parameters_.duration_seconds)
          {
            parameters_.duration_seconds = true;
            read = read_uint(request.benchmark.duration_seconds);
          }
          else if (key == "seed" && !parameters_.seed)
          {
            parameters_.seed = true;
            read = read_uint(request.benchmark.seed);
          }
          else if (key == "port_id" && !parameters_.port_id)
          {
            parameters_.port_id = true;
            read = read_string(request.fault.port_id);
          }
          else if (key == "first_endpoint" && !parameters_.first_endpoint)
          {
            parameters_.first_endpoint = true;
            read = read_string(request.fault.first_endpoint);
          }
          else if (key == "second_endpoint" && !parameters_.second_endpoint)
          {
            parameters_.second_endpoint = true;
            read = read_string(request.fault.second_endpoint);
          }
          else if (key == "latency_ms" && !parameters_.latency)
          {
            parameters_.latency = true;
            read = read_milliseconds(request.fault.configuration.latency);
          }
          else if (key == "jitter_ms" && !parameters_.jitter)
          {
            parameters_.jitter = true;
            read = read_milliseconds(request.fault.configuration.jitter);
          }
          else if (key == "loss_basis_points" && !parameters_.loss)
          {
            parameters_.loss = true;
            read = read_uint(request.fault.configuration.loss_basis_points);
          }
          else if (key == "duplication_basis_points" && !parameters_.duplication)
          {
            parameters_.duplication = true;
            read = read_uint(request.fault.configuration.duplication_basis_points);
          }
          else if (key == "bandwidth_bits_per_second" && !parameters_.bandwidth)
          {
            parameters_.bandwidth = true;
            read = read_uint(request.fault.configuration.bandwidth_bits_per_second);
          }
          else if (key == "blackhole" && !parameters_.blackhole)
          {
            parameters_.blackhole = true;
            read = read_bool(request.fault.configuration.blackhole);
          }
          else if (key == "isolated" && !parameters_.isolated)
          {
            parameters_.isolated = true;
            read = read_bool(request.fault.configuration.isolated);
          }
          else
          {
            error_ = ControlParseError::InvalidField;
            return false;
          }
          if (!read)
          {
            return false;
          }
          skip_whitespace();
          if (consume('}'))
          {
            return true;
          }
          if (!consume(','))
          {
            return false;
          }
        }
      }

      bool parameters_valid_for(ControlCommand command) const noexcept
      {
        const bool benchmark = parameters_.scenario && parameters_.backend && parameters_.batch_size &&
                               parameters_.duration_seconds && parameters_.seed;
        const bool fault = parameters_.latency && parameters_.jitter && parameters_.loss && parameters_.duplication &&
                           parameters_.bandwidth && parameters_.blackhole && parameters_.isolated;
        const bool has_benchmark = parameters_.scenario || parameters_.backend || parameters_.batch_size ||
                                   parameters_.duration_seconds || parameters_.seed;
        const bool has_fault = parameters_.latency || parameters_.jitter || parameters_.loss || parameters_.duplication ||
                               parameters_.bandwidth || parameters_.blackhole || parameters_.isolated;
        switch (command)
        {
          case ControlCommand::LoadTopology:
            return parameters_.topology_path && !has_benchmark && !parameters_.port_id && !parameters_.first_endpoint &&
                   !parameters_.second_endpoint && !has_fault;
          case ControlCommand::StartBenchmark:
            return benchmark && !parameters_.topology_path && !parameters_.port_id && !parameters_.first_endpoint &&
                   !parameters_.second_endpoint && !has_fault;
          case ControlCommand::SetPortFault:
            return parameters_.port_id && !parameters_.topology_path && fault && !parameters_.first_endpoint &&
                   !parameters_.second_endpoint && !has_benchmark;
          case ControlCommand::ClearPortFault:
            return parameters_.port_id && !parameters_.topology_path && !parameters_.first_endpoint &&
                   !parameters_.second_endpoint && !has_benchmark && !has_fault;
          case ControlCommand::SetLinkFault:
            return parameters_.first_endpoint && parameters_.second_endpoint && !parameters_.topology_path && fault &&
                   !parameters_.port_id && !has_benchmark;
          case ControlCommand::ClearLinkFault:
            return parameters_.first_endpoint && parameters_.second_endpoint && !parameters_.topology_path &&
                   !parameters_.port_id && !has_benchmark && !has_fault;
          case ControlCommand::GetSwitchState:
          case ControlCommand::GetActiveFaults:
          case ControlCommand::StopRun: return false;
        }
        return false;
      }

      bool parameters_missing_required_for(ControlCommand command) const noexcept
      {
        const bool has_benchmark = parameters_.scenario || parameters_.backend || parameters_.batch_size ||
                                   parameters_.duration_seconds || parameters_.seed;
        const bool has_fault = parameters_.latency || parameters_.jitter || parameters_.loss || parameters_.duplication ||
                               parameters_.bandwidth || parameters_.blackhole || parameters_.isolated;
        switch (command)
        {
          case ControlCommand::LoadTopology:
            return !has_benchmark && !has_fault && !parameters_.port_id && !parameters_.first_endpoint &&
                   !parameters_.second_endpoint;
          case ControlCommand::StartBenchmark:
            return !parameters_.topology_path && !parameters_.port_id && !parameters_.first_endpoint &&
                   !parameters_.second_endpoint && !has_fault;
          case ControlCommand::SetPortFault:
            return parameters_.port_id && !parameters_.topology_path && !parameters_.first_endpoint &&
                   !parameters_.second_endpoint && !has_benchmark;
          case ControlCommand::ClearPortFault:
            return !parameters_.topology_path && !has_benchmark && !has_fault && !parameters_.first_endpoint &&
                   !parameters_.second_endpoint;
          case ControlCommand::SetLinkFault: return !parameters_.topology_path && !parameters_.port_id && !has_benchmark;
          case ControlCommand::ClearLinkFault:
            return !parameters_.topology_path && !parameters_.port_id && !has_benchmark && !has_fault;
          case ControlCommand::GetSwitchState:
          case ControlCommand::GetActiveFaults:
          case ControlCommand::StopRun: return false;
        }
        return false;
      }

      struct ParameterFields
      {
        bool topology_path = false;
        bool scenario = false;
        bool backend = false;
        bool batch_size = false;
        bool duration_seconds = false;
        bool seed = false;
        bool port_id = false;
        bool first_endpoint = false;
        bool second_endpoint = false;
        bool latency = false;
        bool jitter = false;
        bool loss = false;
        bool duplication = false;
        bool bandwidth = false;
        bool blackhole = false;
        bool isolated = false;
      };

      ParameterFields parameters_;

      std::string_view input_;
      size_t position_ = 0;
      ControlParseError error_ = ControlParseError::MalformedJson;
    };

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
              result << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned int>(character)
                     << std::dec << std::setfill(' ');
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

    const char* anomaly_type_name(AnomalyType type) noexcept
    {
      switch (type)
      {
        case AnomalyType::BroadcastStorm: return "broadcast_storm";
        case AnomalyType::MacFlap: return "mac_flap";
        case AnomalyType::UnknownUnicastFlood: return "unknown_unicast_flood";
        case AnomalyType::UdpFlood: return "udp_flood";
        case AnomalyType::PortScan: return "port_scan";
        case AnomalyType::HotTalker: return "hot_talker";
        case AnomalyType::MalformedFrame: return "malformed_frame";
      }
      return "unknown";
    }

    void append_anomaly_json(std::ostringstream& result, const AnomalyEvent& anomaly)
    {
      result << "{\"type\":" << json_string(anomaly_type_name(anomaly.type))
             << ",\"source_mac\":" << json_string(anomaly.source_mac.to_string())
             << ",\"source_ipv4\":" << anomaly.source_ipv4 << ",\"ingress_port\":" << anomaly.ingress_port
             << ",\"observed_packets\":" << anomaly.observed_packets << ",\"observed_bytes\":" << anomaly.observed_bytes
             << ",\"observed_distinct_destinations\":" << anomaly.observed_distinct_destinations
             << ",\"threshold\":" << anomaly.threshold << ",\"window_duration_ns\":" << anomaly.window_duration_ns << '}';
    }

    bool is_valid_benchmark(const BenchmarkParameters& benchmark) noexcept
    {
      constexpr uint32_t MAX_BATCH_SIZE = 8192;
      return !benchmark.scenario.empty() && benchmark.batch_size > 0 && benchmark.batch_size <= MAX_BATCH_SIZE &&
             benchmark.duration_seconds > 0;
    }
  }  // namespace

  const char* to_string(AnalyzerBackend backend) noexcept
  {
    return backend == AnalyzerBackend::Cpu ? "cpu" : "cuda";
  }

  const char* to_string(ControlCommand command) noexcept
  {
    switch (command)
    {
      case ControlCommand::LoadTopology: return "load_topology";
      case ControlCommand::GetSwitchState: return "get_switch_state";
      case ControlCommand::GetActiveFaults: return "get_active_faults";
      case ControlCommand::StartBenchmark: return "start_benchmark";
      case ControlCommand::StopRun: return "stop_run";
      case ControlCommand::SetPortFault: return "set_port_fault";
      case ControlCommand::ClearPortFault: return "clear_port_fault";
      case ControlCommand::SetLinkFault: return "set_link_fault";
      case ControlCommand::ClearLinkFault: return "clear_link_fault";
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

  const char* to_string(ControlParseError error) noexcept
  {
    switch (error)
    {
      case ControlParseError::MalformedJson: return "malformed JSON";
      case ControlParseError::InvalidField: return "invalid control request field";
      case ControlParseError::MissingRequiredField: return "missing required control request field";
    }
    return "unknown control parse error";
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

  expected<ControlRequest, ControlParseError> control_request_from_json(std::string_view json)
  {
    return ControlRequestParser(json).parse();
  }

  std::string to_json(const ControlRequest& request)
  {
    std::ostringstream result;
    result << "{\"api_version\":" << request.api_version << ",\"request_id\":" << json_string(request.request_id)
           << ",\"command\":" << json_string(to_string(request.command))
           << ",\"topology_revision\":" << request.topology_revision;
    if (request.command == ControlCommand::LoadTopology)
    {
      result << ",\"parameters\":{\"topology_path\":" << json_string(request.topology.path) << '}';
    }
    else if (request.command == ControlCommand::StartBenchmark)
    {
      result << ",\"parameters\":{\"scenario\":" << json_string(request.benchmark.scenario)
             << ",\"backend\":" << json_string(to_string(request.benchmark.backend))
             << ",\"batch_size\":" << request.benchmark.batch_size
             << ",\"duration_seconds\":" << request.benchmark.duration_seconds << ",\"seed\":" << request.benchmark.seed
             << '}';
    }
    else if (request.command == ControlCommand::SetPortFault || request.command == ControlCommand::SetLinkFault)
    {
      const auto& fault = request.fault;
      result << ",\"parameters\":{";
      if (request.command == ControlCommand::SetPortFault)
      {
        result << "\"port_id\":" << json_string(fault.port_id);
      }
      else
      {
        result << "\"first_endpoint\":" << json_string(fault.first_endpoint)
               << ",\"second_endpoint\":" << json_string(fault.second_endpoint);
      }
      result << ",\"latency_ms\":"
             << std::chrono::duration_cast<std::chrono::milliseconds>(fault.configuration.latency).count()
             << ",\"jitter_ms\":"
             << std::chrono::duration_cast<std::chrono::milliseconds>(fault.configuration.jitter).count()
             << ",\"loss_basis_points\":" << fault.configuration.loss_basis_points
             << ",\"duplication_basis_points\":" << fault.configuration.duplication_basis_points
             << ",\"bandwidth_bits_per_second\":" << fault.configuration.bandwidth_bits_per_second
             << ",\"blackhole\":" << (fault.configuration.blackhole ? "true" : "false")
             << ",\"isolated\":" << (fault.configuration.isolated ? "true" : "false") << '}';
    }
    else if (request.command == ControlCommand::ClearPortFault)
    {
      result << ",\"parameters\":{\"port_id\":" << json_string(request.fault.port_id) << '}';
    }
    else if (request.command == ControlCommand::ClearLinkFault)
    {
      result << ",\"parameters\":{\"first_endpoint\":" << json_string(request.fault.first_endpoint)
             << ",\"second_endpoint\":" << json_string(request.fault.second_endpoint) << '}';
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
           << ",\"dropped_packets\":" << metrics.dropped_packets << ",\"malformed_packets\":" << metrics.malformed_packets
           << ",\"learned_macs\":" << metrics.learned_macs << "}}";
    return result.str();
  }
  std::string to_json(const FaultStateEvent& event)
  {
    const auto& configuration = event.configuration;
    std::ostringstream result;
    result << "{\"api_version\":" << event.api_version << ",\"event_sequence\":" << event.event_sequence
           << ",\"topology_revision\":" << event.topology_revision << ",\"event\":\"fault_state_changed\""
           << ",\"active\":" << (event.active ? "true" : "false")
           << ",\"first_endpoint\":" << json_string(event.first_endpoint);
    if (!event.second_endpoint.empty())
    {
      result << ",\"second_endpoint\":" << json_string(event.second_endpoint);
    }
    if (event.active)
    {
      result << ",\"configuration\":{\"latency_ms\":"
             << std::chrono::duration_cast<std::chrono::milliseconds>(configuration.latency).count()
             << ",\"jitter_ms\":" << std::chrono::duration_cast<std::chrono::milliseconds>(configuration.jitter).count()
             << ",\"loss_basis_points\":" << configuration.loss_basis_points
             << ",\"duplication_basis_points\":" << configuration.duplication_basis_points
             << ",\"bandwidth_bits_per_second\":" << configuration.bandwidth_bits_per_second
             << ",\"blackhole\":" << (configuration.blackhole ? "true" : "false")
             << ",\"isolated\":" << (configuration.isolated ? "true" : "false") << '}';
    }
    result << '}';
    return result.str();
  }
  std::string to_json(const TopologyStateEvent& event)
  {
    std::ostringstream result;
    result << "{\"api_version\":" << event.api_version << ",\"event_sequence\":" << event.event_sequence
           << ",\"topology_revision\":" << event.topology_revision << ",\"event\":\"topology_loaded\""
           << ",\"name\":" << json_string(event.name) << ",\"nodes\":[";
    for (size_t index = 0; index < event.nodes.size(); ++index)
    {
      if (index != 0)
        result << ',';
      const auto& node = event.nodes[index];
      result << "{\"id\":" << json_string(node.id) << ",\"type\":" << json_string(to_string(node.type)) << '}';
    }
    result << "],\"links\":[";
    for (size_t index = 0; index < event.links.size(); ++index)
    {
      if (index != 0)
        result << ',';
      const auto& link = event.links[index];
      result << "{\"from\":" << json_string(link.from) << ",\"to\":" << json_string(link.to)
             << ",\"latency_ms\":" << link.latency.count() << '}';
    }
    result << "]}";
    return result.str();
  }
  std::string to_json(const AnomalyDetectedEvent& event)
  {
    std::ostringstream result;
    result << "{\"api_version\":" << event.api_version << ",\"event_sequence\":" << event.event_sequence
           << ",\"topology_revision\":" << event.topology_revision << ",\"event\":\"anomaly_detected\",\"anomaly\":";
    append_anomaly_json(result, event.anomaly);
    result << '}';
    return result.str();
  }

  std::string to_json(const PolicyActionEvent& event)
  {
    std::ostringstream result;
    result << "{\"api_version\":" << event.api_version << ",\"event_sequence\":" << event.event_sequence
           << ",\"topology_revision\":" << event.topology_revision
           << ",\"event\":\"policy_action\",\"rule_name\":" << json_string(event.decision.rule_name)
           << ",\"action\":" << json_string(to_string(event.decision.action))
           << ",\"hit_count\":" << event.decision.hit_count
           << ",\"rate_limit_packets_per_second\":" << event.decision.rate_limit_packets_per_second << ",\"anomaly\":";
    append_anomaly_json(result, event.decision.anomaly);
    result << '}';
    return result.str();
  }

  std::string to_json(const SupervisionStateEvent& event)
  {
    std::ostringstream result;
    result << "{\"api_version\":" << event.api_version << ",\"event_sequence\":" << event.event_sequence
           << ",\"topology_revision\":" << event.topology_revision << ",\"event\":\"supervision_state\""
           << ",\"analysed_frames\":" << event.analysed_frames << ",\"blocked_frames\":" << event.blocked_frames
           << ",\"bindings\":[";
    for (size_t index = 0; index < event.bindings.size(); ++index)
    {
      if (index != 0)
        result << ',';
      const auto& binding = event.bindings[index];
      result << "{\"port_id\":" << json_string(binding.port_id) << ",\"endpoint\":" << json_string(binding.endpoint) << '}';
    }
    result << "]}";
    return result.str();
  }

}  // namespace wirelab
