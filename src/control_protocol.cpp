#include "project/control_protocol.hpp"

#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

namespace project
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
            has_parameters = read_benchmark(request.benchmark);
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
        if (request.command == ControlCommand::StartBenchmark && !has_parameters)
        {
          return unexpected(ControlParseError::MissingRequiredField);
        }
        if (request.command != ControlCommand::StartBenchmark && has_parameters)
        {
          return unexpected(ControlParseError::InvalidField);
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
          if (character >= '0' && character <= '9') value |= static_cast<uint32_t>(character - '0');
          else if (character >= 'a' && character <= 'f') value |= static_cast<uint32_t>(character - 'a' + 10);
          else if (character >= 'A' && character <= 'F') value |= static_cast<uint32_t>(character - 'A' + 10);
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
            default:
              error_ = ControlParseError::MalformedJson;
              return false;
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
        if (value == "get_switch_state") command = ControlCommand::GetSwitchState;
        else if (value == "start_benchmark") command = ControlCommand::StartBenchmark;
        else if (value == "stop_run") command = ControlCommand::StopRun;
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
        if (value == "cpu") backend = AnalyzerBackend::Cpu;
        else if (value == "cuda") backend = AnalyzerBackend::Cuda;
        else
        {
          error_ = ControlParseError::InvalidField;
          return false;
        }
        return true;
      }

      bool read_benchmark(BenchmarkParameters& benchmark)
      {
        bool has_scenario = false;
        bool has_backend = false;
        bool has_batch_size = false;
        bool has_duration = false;
        bool has_seed = false;
        if (!consume('{'))
        {
          return false;
        }
        skip_whitespace();
        if (consume('}'))
        {
          error_ = ControlParseError::MissingRequiredField;
          return false;
        }

        while (true)
        {
          std::string key;
          if (!read_string(key) || !consume(':'))
          {
            return false;
          }
          if (key == "scenario" && !has_scenario) has_scenario = read_string(benchmark.scenario);
          else if (key == "backend" && !has_backend) has_backend = read_backend(benchmark.backend);
          else if (key == "batch_size" && !has_batch_size) has_batch_size = read_uint(benchmark.batch_size);
          else if (key == "duration_seconds" && !has_duration) has_duration = read_uint(benchmark.duration_seconds);
          else if (key == "seed" && !has_seed) has_seed = read_uint(benchmark.seed);
          else
          {
            error_ = ControlParseError::InvalidField;
            return false;
          }
          if (!has_scenario && key == "scenario") return false;
          if (!has_backend && key == "backend") return false;
          if (!has_batch_size && key == "batch_size") return false;
          if (!has_duration && key == "duration_seconds") return false;
          if (!has_seed && key == "seed") return false;

          skip_whitespace();
          if (consume('}'))
          {
            break;
          }
          if (!consume(','))
          {
            return false;
          }
        }
        if (!has_scenario || !has_backend || !has_batch_size || !has_duration || !has_seed)
        {
          error_ = ControlParseError::MissingRequiredField;
          return false;
        }
        return true;
      }

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
