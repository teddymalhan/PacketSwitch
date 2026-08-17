// Replays a capture through WireLab analysis and writes an annotated pcapng.
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "wirelab/analysis_pipeline.hpp"
#include "wirelab/anomaly_detector.hpp"
#include "wirelab/packet_analyzer.hpp"
#include "wirelab/pcap.hpp"
#include "wirelab/policy_engine.hpp"

namespace
{
  struct Options
  {
    std::string input;
    std::string output;
    size_t batch_size = 256;
    uint64_t window_ms = 1000;
    uint64_t broadcast_threshold = 100;
    uint64_t unknown_unicast_threshold = 100;
    uint64_t udp_threshold = 500;
    uint64_t port_scan_threshold = 20;
    uint64_t hot_talker_threshold = 1000;
    uint64_t malformed_threshold = 10;
    uint64_t mac_flap_threshold = 5;
    bool only_flagged = false;
  };

  void print_usage()
  {
    std::cerr << "Usage: wirelab_pcap <capture.pcap> [options]\n"
                 "\n"
                 "Replays a classic libpcap capture through WireLab packet analysis and\n"
                 "anomaly detection, then optionally writes a pcapng whose per-packet\n"
                 "comments carry WireLab's verdict for viewing in Wireshark.\n"
                 "\n"
                 "Options:\n"
                 "  --out <file.pcapng>        Write an annotated capture.\n"
                 "  --batch <packets>          Packets per analysis batch (default 256).\n"
                 "  --window <milliseconds>    Detection window (default 1000).\n"
                 "  --broadcast <packets>      Broadcast storm threshold (default 100).\n"
                 "  --unknown-unicast <n>      Unknown unicast flood threshold (default 100).\n"
                 "  --udp <packets>            UDP flood threshold (default 500).\n"
                 "  --port-scan <ports>        Distinct destination ports (default 20).\n"
                 "  --hot-talker <packets>     Hot talker threshold (default 1000).\n"
                 "  --malformed <frames>       Malformed frame threshold (default 10).\n"
                 "  --mac-flap <transitions>   MAC flap threshold (default 5).\n"
                 "  --only-flagged             Export only packets that carry a verdict.\n";
  }

  bool parse_u64(const char* text, uint64_t& out)
  {
    try
    {
      size_t consumed = 0;
      const auto value = std::stoull(text, &consumed);
      if (consumed != std::strlen(text))
      {
        return false;
      }
      out = value;
      return true;
    }
    catch (const std::exception&)
    {
      return false;
    }
  }

  bool parse_options(int argc, char** argv, Options& options)
  {
    if (argc < 2)
    {
      return false;
    }
    options.input = argv[1];

    const std::map<std::string, uint64_t*> numeric = {
      { "--window", &options.window_ms },
      { "--broadcast", &options.broadcast_threshold },
      { "--unknown-unicast", &options.unknown_unicast_threshold },
      { "--udp", &options.udp_threshold },
      { "--port-scan", &options.port_scan_threshold },
      { "--hot-talker", &options.hot_talker_threshold },
      { "--malformed", &options.malformed_threshold },
      { "--mac-flap", &options.mac_flap_threshold },
    };

    for (int index = 2; index < argc; ++index)
    {
      const std::string flag = argv[index];
      if (flag == "--only-flagged")
      {
        options.only_flagged = true;
        continue;
      }
      if (index + 1 >= argc)
      {
        std::cerr << "Missing value for " << flag << "\n";
        return false;
      }
      const char* value = argv[++index];
      if (flag == "--out")
      {
        options.output = value;
        continue;
      }
      if (flag == "--batch")
      {
        uint64_t batch = 0;
        if (!parse_u64(value, batch) || batch == 0)
        {
          std::cerr << "Batch size must be a positive integer\n";
          return false;
        }
        options.batch_size = static_cast<size_t>(batch);
        continue;
      }
      const auto entry = numeric.find(flag);
      if (entry == numeric.end())
      {
        std::cerr << "Unknown option " << flag << "\n";
        return false;
      }
      if (!parse_u64(value, *entry->second))
      {
        std::cerr << "Value for " << flag << " must be an integer\n";
        return false;
      }
    }
    return true;
  }

  std::string ipv4_string(uint32_t address)
  {
    std::ostringstream stream;
    stream << (address >> 24 & 0xff) << '.' << (address >> 16 & 0xff) << '.' << (address >> 8 & 0xff) << '.'
           << (address & 0xff);
    return stream.str();
  }

  // The comment Wireshark shows for a packet: what WireLab decided and why.
  std::string build_comment(
      const wirelab::PacketAnalysis& analysis,
      const std::vector<const wirelab::AnomalyEvent*>& anomalies,
      const std::vector<const wirelab::PolicyDecision*>& decisions)
  {
    std::ostringstream stream;
    stream << "WireLab: " << wirelab::to_string(analysis.classification) << ", " << wirelab::to_string(analysis.validity);
    if (analysis.validity == wirelab::PacketValidity::Valid && analysis.protocol != 0)
    {
      stream << ", proto=" << static_cast<unsigned>(analysis.protocol);
      if (analysis.source_ipv4 != 0 || analysis.destination_ipv4 != 0)
      {
        stream << ", " << ipv4_string(analysis.source_ipv4) << ':' << analysis.source_port << " -> "
               << ipv4_string(analysis.destination_ipv4) << ':' << analysis.destination_port;
      }
      stream << ", flow=0x" << std::hex << std::setw(16) << std::setfill('0') << analysis.flow_hash << std::dec;
    }

    for (const auto* anomaly : anomalies)
    {
      stream << "\nANOMALY " << wirelab::to_string(anomaly->type);
      if (anomaly->observed_distinct_destinations != 0)
      {
        stream << ": " << anomaly->observed_distinct_destinations << " destinations > threshold " << anomaly->threshold;
      }
      else
      {
        stream << ": " << anomaly->observed_packets << " packets > threshold " << anomaly->threshold;
      }
      stream << " in " << anomaly->window_duration_ns / 1'000'000 << " ms";
      if (!anomaly->source_mac.is_zero())
      {
        stream << ", source " << anomaly->source_mac.to_string();
      }
      if (anomaly->source_ipv4 != 0)
      {
        stream << ", source " << ipv4_string(anomaly->source_ipv4);
      }
    }

    for (const auto* decision : decisions)
    {
      stream << "\nPOLICY " << decision->rule_name << " -> " << wirelab::to_string(decision->action);
      if (decision->action == wirelab::PolicyAction::RateLimit && decision->rate_limit_packets_per_second != 0)
      {
        stream << " at " << decision->rate_limit_packets_per_second << " pps";
      }
    }
    return stream.str();
  }
}  // namespace

int main(int argc, char** argv)
{
  Options options;
  if (!parse_options(argc, argv, options))
  {
    print_usage();
    return 1;
  }

  auto capture = wirelab::PcapCapture::from_file(options.input);
  if (!capture)
  {
    std::cerr << "Cannot read " << options.input << ": " << wirelab::to_string(capture.error()) << "\n";
    return 1;
  }

  const auto& packets = capture.value().packets();
  std::cout << "Read " << packets.size() << " packets from " << options.input << " ("
            << (capture.value().nanosecond_resolution() ? "nanosecond" : "microsecond") << " timestamps, snaplen "
            << capture.value().snaplen() << ")\n";

  wirelab::AnomalyDetectorConfig detector_config;
  detector_config.window_duration_ns = options.window_ms * 1'000'000ULL;
  detector_config.broadcast_packets_threshold = options.broadcast_threshold;
  detector_config.unknown_unicast_packets_threshold = options.unknown_unicast_threshold;
  detector_config.udp_packets_threshold = options.udp_threshold;
  detector_config.port_scan_destinations_threshold = options.port_scan_threshold;
  detector_config.hot_talker_packets_threshold = options.hot_talker_threshold;
  detector_config.malformed_frames_threshold = options.malformed_threshold;
  detector_config.mac_flap_transitions_threshold = options.mac_flap_threshold;
  // No topology controller is attached: a capture has no live port to enforce
  // on, so the pipeline detects and decides but never applies a fault.
  wirelab::AnalysisPipeline pipeline(detector_config);

  // Default rules exist so a replay demonstrates the whole pipeline without
  // requiring a policy file.
  auto& policies = pipeline.policies();
  (void)policies.add_rule(
      { "quarantine-broadcast-storm", wirelab::AnomalyType::BroadcastStorm, wirelab::PolicyAction::Quarantine });
  (void)policies.add_rule({ "drop-udp-flood", wirelab::AnomalyType::UdpFlood, wirelab::PolicyAction::Drop });
  (void)policies.add_rule({ "mirror-port-scan", wirelab::AnomalyType::PortScan, wirelab::PolicyAction::Mirror });
  (void)policies.add_rule({ "mirror-malformed", wirelab::AnomalyType::MalformedFrame, wirelab::PolicyAction::Mirror });

  wirelab::CpuPacketAnalyzer analyzer;
  std::vector<wirelab::PcapNgWriter> writer;
  if (!options.output.empty())
  {
    auto created = wirelab::PcapNgWriter::create(options.output);
    if (!created)
    {
      std::cerr << "Cannot write " << options.output << ": " << wirelab::to_string(created.error()) << "\n";
      return 1;
    }
    writer.push_back(std::move(created.value()));
  }

  uint64_t total_anomalies = 0;
  uint64_t total_decisions = 0;
  uint64_t annotated_packets = 0;
  uint64_t exported_packets = 0;
  std::map<std::string, uint64_t> anomaly_counts;
  std::map<std::string, uint64_t> rule_hits;
  wirelab::AnalysisBatch totals;
  // Enforcement leases are never taken here, so one wall reading serves the
  // whole replay; detection windows advance on capture timestamps instead.
  const auto now = std::chrono::steady_clock::now();

  for (size_t start = 0; start < packets.size(); start += options.batch_size)
  {
    const size_t count = std::min(options.batch_size, packets.size() - start);
    std::vector<wirelab::PacketView> views;
    views.reserve(count);
    for (size_t index = start; index < start + count; ++index)
    {
      views.push_back(capture.value().view(index));
    }

    const auto analysis = analyzer.analyze(views.data(), views.size());
    totals.received_packets += analysis.received_packets;
    totals.received_bytes += analysis.received_bytes;
    totals.malformed_packets += analysis.malformed_packets;
    totals.broadcast_packets += analysis.broadcast_packets;
    totals.unknown_unicast_packets += analysis.unknown_unicast_packets;
    totals.known_unicast_packets += analysis.known_unicast_packets;

    const auto outcome = pipeline.evaluate(analysis, packets[start + count - 1].timestamp_ns, now);
    const auto& anomalies = outcome.anomalies;
    const auto& decisions = outcome.decisions;
    total_anomalies += anomalies.size();
    total_decisions += decisions.size();
    for (const auto& anomaly : anomalies)
    {
      ++anomaly_counts[wirelab::to_string(anomaly.type)];
    }
    for (const auto& decision : decisions)
    {
      ++rule_hits[decision.rule_name];
    }

    if (writer.empty())
    {
      continue;
    }

    // An anomaly indicts a source, so its verdict is attached to the packets in
    // this batch that came from that source rather than to the batch as a whole.
    for (size_t index = 0; index < analysis.packets.size(); ++index)
    {
      const auto& packet = analysis.packets[index];
      std::vector<const wirelab::AnomalyEvent*> matched;
      std::vector<const wirelab::PolicyDecision*> matched_decisions;
      for (const auto& anomaly : anomalies)
      {
        const bool by_mac = !anomaly.source_mac.is_zero() && anomaly.source_mac == packet.source_mac;
        const bool by_ipv4 = anomaly.source_ipv4 != 0 && anomaly.source_ipv4 == packet.source_ipv4;
        const bool by_port = anomaly.type == wirelab::AnomalyType::MalformedFrame &&
                             packet.classification == wirelab::PacketClassification::Malformed;
        if (!by_mac && !by_ipv4 && !by_port)
        {
          continue;
        }
        matched.push_back(&anomaly);
        // A decision carries the anomaly that triggered it, so the pairing is by
        // source identity rather than by type alone; two sources can raise the
        // same anomaly in one batch and earn different rules.
        for (const auto& decision : decisions)
        {
          if (decision.anomaly.type == anomaly.type && decision.anomaly.source_mac == anomaly.source_mac &&
              decision.anomaly.source_ipv4 == anomaly.source_ipv4 && decision.anomaly.ingress_port == anomaly.ingress_port)
          {
            matched_decisions.push_back(&decision);
          }
        }
      }

      if (!matched.empty())
      {
        ++annotated_packets;
      }
      else if (options.only_flagged)
      {
        continue;
      }

      const auto& record = packets[start + index];
      const auto comment = build_comment(packet, matched, matched_decisions);
      const auto written = writer.front().write(
          record.timestamp_ns,
          capture.value().frame(start + index),
          record.captured_length,
          record.original_length,
          comment);
      if (!written)
      {
        std::cerr << "Cannot write " << options.output << ": " << wirelab::to_string(written.error()) << "\n";
        return 1;
      }
      ++exported_packets;
    }
  }

  if (!writer.empty() && !writer.front().flush())
  {
    std::cerr << "Cannot flush " << options.output << "\n";
    return 1;
  }

  std::cout << "\nAnalysis\n"
            << "  received       " << totals.received_packets << " packets, " << totals.received_bytes << " bytes\n"
            << "  broadcast      " << totals.broadcast_packets << "\n"
            << "  unknown unicast" << ' ' << totals.unknown_unicast_packets << "\n"
            << "  known unicast  " << totals.known_unicast_packets << "\n"
            << "  malformed      " << totals.malformed_packets << "\n";

  std::cout << "\nDetection\n  " << total_anomalies << " anomaly events, " << total_decisions << " policy decisions\n";
  for (const auto& [name, count] : anomaly_counts)
  {
    std::cout << "  " << name << ' ' << count << "\n";
  }
  for (const auto& [name, count] : rule_hits)
  {
    std::cout << "  rule " << name << ' ' << count << "\n";
  }

  if (!writer.empty())
  {
    std::cout << "\nWrote " << exported_packets << " packets to " << options.output << " (" << annotated_packets
              << " carry an anomaly verdict)\n";
  }
  return 0;
}
