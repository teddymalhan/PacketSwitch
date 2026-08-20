#include "wirelab/session.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

#include "session_detail.hpp"
#include "wirelab/benchmark.hpp"

#ifdef WIRELAB_HAS_CUDA
#include "wirelab/cuda_packet_parser.hpp"
#endif
#ifdef WIRELAB_HAS_METAL
#include "wirelab/metal_packet_parser.hpp"
#endif

namespace wirelab
{
  namespace session_detail
  {
    AnomalyDetectorConfig desktop_anomaly_config() noexcept
    {
      AnomalyDetectorConfig config;
      config.window_duration_ns = 1'000'000'000;
      config.broadcast_packets_threshold = 100;
      config.unknown_unicast_packets_threshold = 100;
      config.udp_packets_threshold = 200;
      config.port_scan_destinations_threshold = 20;
      config.hot_talker_packets_threshold = 200;
      config.malformed_frames_threshold = 1;
      return config;
    }

    std::string ipv4_string(uint32_t address)
    {
      std::array<char, 16> buffer{};
      const int written = std::snprintf(
          buffer.data(),
          buffer.size(),
          "%u.%u.%u.%u",
          (address >> 24U) & 0xffU,
          (address >> 16U) & 0xffU,
          (address >> 8U) & 0xffU,
          address & 0xffU);
      return written <= 0 ? std::string{} : std::string(buffer.data(), static_cast<size_t>(written));
    }

    std::string yaml_quote(const std::string& text)
    {
      std::string result = "\"";
      result.reserve(text.size() + 2);
      for (const char character : text)
      {
        if (character == '\\' || character == '"')
          result.push_back('\\');
        result.push_back(character);
      }
      result.push_back('"');
      return result;
    }

    std::string enforcement_summary(const EnforcementAction& action)
    {
      switch (action.kind)
      {
        case EnforcementKind::Blackhole: return "Dropping all frames";
        case EnforcementKind::Isolate: return "Port quarantined";
        case EnforcementKind::RateLimit:
          return "Capped at " + std::to_string(action.rate_limit_bits_per_second / 1000) + " kbit/s";
        case EnforcementKind::None: return "Recorded only";
      }
      return "Unknown";
    }

    std::string benchmark_backend_id(const std::string& label)
    {
      if (label == "Metal (live)")
        return "metal-live";
      std::string id = label;
      std::transform(
          id.begin(), id.end(), id.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
      return id;
    }

    std::string benchmark_backend_label(const std::string& id)
    {
      if (id == "cuda")
        return "CUDA";
      if (id == "metal")
        return "Metal";
      if (id == "metal-live")
        return "Metal (live)";
      return "CPU";
    }

    std::string strip_file_url(const std::string& path)
    {
      constexpr const char* prefix = "file://";
      constexpr size_t prefix_size = 7;
      if (path.compare(0, prefix_size, prefix) != 0)
        return path;
      // file:///tmp/x keeps the leading slash of the path; file://host/x is not
      // something a local dialog produces, so the remainder is the path.
      return path.substr(prefix_size);
    }

    std::string file_name_of(const std::string& path)
    {
      const auto separator = path.find_last_of("/\\");
      return separator == std::string::npos ? path : path.substr(separator + 1);
    }

    std::string format_fixed(double value, int precision)
    {
      std::array<char, 64> buffer{};
      const int written = std::snprintf(buffer.data(), buffer.size(), "%.*f", precision, value);
      return written <= 0 ? std::string{} : std::string(buffer.data(), static_cast<size_t>(written));
    }

    std::string format_general(double value)
    {
      std::array<char, 64> buffer{};
      const int written = std::snprintf(buffer.data(), buffer.size(), "%g", value);
      return written <= 0 ? std::string{} : std::string(buffer.data(), static_cast<size_t>(written));
    }

    std::string format_json_number(double value)
    {
      std::array<char, 64> buffer{};
      const int written = std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
      return written <= 0 ? std::string{ "0" } : std::string(buffer.data(), static_cast<size_t>(written));
    }

    std::string json_escape(const std::string& text)
    {
      std::string result;
      result.reserve(text.size());
      for (const char character : text)
      {
        switch (character)
        {
          case '"': result += "\\\""; break;
          case '\\': result += "\\\\"; break;
          case '\n': result += "\\n"; break;
          case '\r': result += "\\r"; break;
          case '\t': result += "\\t"; break;
          default:
            if (static_cast<unsigned char>(character) < 0x20U)
            {
              std::array<char, 8> escape{};
              std::snprintf(escape.data(), escape.size(), "\\u%04x", static_cast<unsigned>(character));
              result += escape.data();
            }
            else
            {
              result.push_back(character);
            }
            break;
        }
      }
      return result;
    }
  }  // namespace session_detail

  const char* display_name(AnomalyType type) noexcept
  {
    switch (type)
    {
      case AnomalyType::BroadcastStorm: return "Broadcast storm";
      case AnomalyType::MacFlap: return "MAC flap";
      case AnomalyType::UnknownUnicastFlood: return "Unknown-unicast flood";
      case AnomalyType::UdpFlood: return "UDP flood";
      case AnomalyType::PortScan: return "Port scan";
      case AnomalyType::HotTalker: return "Hot talker";
      case AnomalyType::MalformedFrame: return "Malformed frame";
    }
    return "Unknown anomaly";
  }

  const char* display_name(PolicyAction action) noexcept
  {
    switch (action)
    {
      case PolicyAction::Allow: return "Allow";
      case PolicyAction::Drop: return "Drop";
      case PolicyAction::Mirror: return "Mirror";
      case PolicyAction::RateLimit: return "Rate limit";
      case PolicyAction::Quarantine: return "Quarantine";
      case PolicyAction::AlertOnly: return "Alert only";
    }
    return "Unknown";
  }

  const char* display_name(PacketClassification classification) noexcept
  {
    switch (classification)
    {
      case PacketClassification::Broadcast: return "Broadcast";
      case PacketClassification::UnknownUnicast: return "Unknown unicast";
      case PacketClassification::KnownUnicast: return "Known unicast";
      case PacketClassification::Malformed: return "Malformed";
    }
    return "Unknown";
  }

  const char* display_name(PacketValidity validity) noexcept
  {
    switch (validity)
    {
      case PacketValidity::Valid: return "Valid";
      case PacketValidity::MalformedEthernet: return "Malformed Ethernet";
      case PacketValidity::MalformedIpv4: return "Malformed IPv4";
      case PacketValidity::MalformedTransport: return "Malformed transport";
    }
    return "Unknown";
  }

  bool anomaly_type_from_display_name(const std::string& name, AnomalyType& type) noexcept
  {
    constexpr std::array<AnomalyType, 7> candidates{ AnomalyType::BroadcastStorm,
                                                     AnomalyType::MacFlap,
                                                     AnomalyType::UnknownUnicastFlood,
                                                     AnomalyType::UdpFlood,
                                                     AnomalyType::PortScan,
                                                     AnomalyType::HotTalker,
                                                     AnomalyType::MalformedFrame };
    for (const auto candidate : candidates)
    {
      if (name == display_name(candidate))
      {
        type = candidate;
        return true;
      }
    }
    return false;
  }

  bool policy_action_from_display_name(const std::string& name, PolicyAction& action) noexcept
  {
    constexpr std::array<PolicyAction, 6> candidates{ PolicyAction::Allow,      PolicyAction::Drop,
                                                      PolicyAction::Mirror,     PolicyAction::RateLimit,
                                                      PolicyAction::Quarantine, PolicyAction::AlertOnly };
    for (const auto candidate : candidates)
    {
      if (name == display_name(candidate))
      {
        action = candidate;
        return true;
      }
    }
    return false;
  }

  const std::vector<std::string>& scenario_names()
  {
    // Asking the benchmark engine which names it parses keeps the traffic and
    // report forms from offering a scenario the engine would reject.
    static const std::vector<std::string> names = []
    {
      std::vector<std::string> parsed;
      for (const char* candidate :
           { "known-unicast", "broadcast", "unknown-unicast", "mixed-traffic", "udp-flood", "port-scan", "broadcast-storm" })
      {
        if (traffic_scenario_from_string(candidate))
          parsed.emplace_back(candidate);
      }
      return parsed;
    }();
    return names;
  }

  const std::vector<std::string>& Session::available_backends()
  {
    // Probing a device is not free and its answer does not change while the
    // process lives, so the list is settled once.
    static const std::vector<std::string> backends = []
    {
      std::vector<std::string> present{ "CPU" };
#ifdef WIRELAB_HAS_CUDA
      if (CudaPacketParser::is_available())
        present.emplace_back("CUDA");
#endif
#ifdef WIRELAB_HAS_METAL
      if (MetalPacketParser::is_available())
        present.emplace_back("Metal");
      if (MetalStreamParser::is_available())
        present.emplace_back("Metal (live)");
#endif
      return present;
    }();
    return backends;
  }

  Session::Session()
      : analysis_pipeline_(session_detail::desktop_anomaly_config(), topology_controller_),
        simulation_start_(std::chrono::steady_clock::now())
  {
    rebuild_policy_rows();
  }

  Session::~Session() = default;

  uint32_t Session::take_dirty() noexcept
  {
    return std::exchange(dirty_, 0U);
  }

  void Session::mark(SessionDirty flag) noexcept
  {
    dirty_ |= dirty_bit(flag);
  }

  void Session::set_status(std::string message)
  {
    status_message_ = std::move(message);
    mark(SessionDirty::Status);
  }

  const std::string& Session::status_message() const noexcept
  {
    return status_message_;
  }

  bool Session::has_topology() const noexcept
  {
    return topology_controller_.has_topology();
  }
  const std::string& Session::topology_name() const noexcept
  {
    return topology_configuration_.name;
  }
  const std::vector<NodeRow>& Session::topology_nodes() const noexcept
  {
    return topology_nodes_;
  }
  const std::vector<LinkRow>& Session::topology_links() const noexcept
  {
    return topology_links_;
  }
  SelectionKind Session::selection_kind() const noexcept
  {
    return selection_kind_;
  }
  const std::string& Session::selected_id() const noexcept
  {
    return selected_id_;
  }
  const std::string& Session::selected_summary() const noexcept
  {
    return selected_summary_;
  }
  const std::vector<FaultRow>& Session::active_faults() const noexcept
  {
    return active_faults_;
  }
  const std::vector<PolicyRow>& Session::policy_rules() const noexcept
  {
    return policy_rules_;
  }
  const std::vector<PolicyActionRow>& Session::policy_actions() const noexcept
  {
    return policy_actions_;
  }
  const std::vector<EnforcedPortRow>& Session::enforced_ports() const noexcept
  {
    return enforced_ports_;
  }
  bool Session::traffic_running() const noexcept
  {
    return traffic_running_;
  }
  const std::string& Session::active_backend() const noexcept
  {
    return active_backend_;
  }
  const std::string& Session::traffic_result() const noexcept
  {
    return traffic_result_;
  }
  const std::vector<MetricSample>& Session::metrics_history() const noexcept
  {
    return metrics_history_;
  }
  const std::vector<MacTableRow>& Session::mac_table() const noexcept
  {
    return mac_table_;
  }
  const std::vector<PortStateRow>& Session::port_states() const noexcept
  {
    return port_states_;
  }
  const std::vector<PacketRow>& Session::packet_rows() const noexcept
  {
    return packet_rows_;
  }
  const std::vector<AnomalyRow>& Session::anomaly_rows() const noexcept
  {
    return anomaly_rows_;
  }
  bool Session::report_running() const noexcept
  {
    return report_running_;
  }
  double Session::report_progress() const noexcept
  {
    return report_progress_;
  }
  const std::string& Session::report_stage() const noexcept
  {
    return report_stage_;
  }
  const std::vector<ReportRow>& Session::report_rows() const noexcept
  {
    return report_rows_;
  }
  const ReportProvenance& Session::report_provenance() const noexcept
  {
    return report_provenance_;
  }
  const std::string& Session::report_export_path() const noexcept
  {
    return report_export_path_;
  }
}  // namespace wirelab
