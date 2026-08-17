#include "wirelab/anomaly_detector.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

namespace wirelab
{
  namespace
  {
    // Rate detectors aggregate by source MAC or source IPv4, but a policy can
    // only be enforced against a port. Carrying the most recent ingress port for
    // each source keeps that target in the evidence.
    struct TrafficCounter
    {
      uint64_t packets = 0;
      uint64_t bytes = 0;
      uint32_t ingress_port = 0;
    };

    bool event_less(const AnomalyEvent& first, const AnomalyEvent& second) noexcept
    {
      if (first.type != second.type)
      {
        return first.type < second.type;
      }
      if (first.source_mac != second.source_mac)
      {
        return first.source_mac < second.source_mac;
      }
      if (first.source_ipv4 != second.source_ipv4)
      {
        return first.source_ipv4 < second.source_ipv4;
      }
      return first.ingress_port < second.ingress_port;
    }
  }  // namespace

  const char* to_string(AnomalyType type) noexcept
  {
    switch (type)
    {
      case AnomalyType::BroadcastStorm: return "broadcast-storm";
      case AnomalyType::MacFlap: return "mac-flap";
      case AnomalyType::UnknownUnicastFlood: return "unknown-unicast-flood";
      case AnomalyType::UdpFlood: return "udp-flood";
      case AnomalyType::PortScan: return "port-scan";
      case AnomalyType::HotTalker: return "hot-talker";
      case AnomalyType::MalformedFrame: return "malformed-frame";
    }
    return "unknown";
  }

  AnomalyDetector::AnomalyDetector(AnomalyDetectorConfig config) : config_(config)
  {
    if (config_.window_duration_ns == 0)
    {
      throw std::invalid_argument("Anomaly detector window duration must be non-zero");
    }
  }

  bool AnomalyDetector::ActiveAnomaly::operator<(const ActiveAnomaly& other) const noexcept
  {
    if (type != other.type)
    {
      return type < other.type;
    }
    if (source_mac != other.source_mac)
    {
      return source_mac < other.source_mac;
    }
    if (source_ipv4 != other.source_ipv4)
    {
      return source_ipv4 < other.source_ipv4;
    }
    return ingress_port < other.ingress_port;
  }

  std::vector<AnomalyEvent> AnomalyDetector::evaluate(const AnalysisBatch& batch, uint64_t timestamp_ns)
  {
    if (has_timestamp_ && timestamp_ns < last_timestamp_ns_)
    {
      throw std::invalid_argument("Anomaly detector timestamps must be monotonic");
    }

    has_timestamp_ = true;
    last_timestamp_ns_ = timestamp_ns;
    for (const auto& packet : batch.packets)
    {
      observations_.push_back(
          { timestamp_ns,
            packet.source_mac,
            packet.source_ipv4,
            packet.destination_port,
            packet.frame_length,
            packet.ingress_port,
            packet.protocol,
            packet.classification,
            packet.validity });
    }
    expire(timestamp_ns);

    std::map<MacAddress, TrafficCounter> broadcasts;
    std::map<MacAddress, TrafficCounter> unknown_unicasts;
    std::map<uint32_t, TrafficCounter> udp_sources;
    std::map<uint32_t, std::set<uint16_t>> scan_destinations;
    std::map<uint32_t, uint32_t> scan_ingress_ports;
    std::map<MacAddress, std::vector<uint32_t>> ingress_ports;
    std::map<MacAddress, TrafficCounter> source_traffic;
    std::map<uint32_t, TrafficCounter> malformed_ingress_ports;

    for (const auto& observation : observations_)
    {
      if (observation.classification == PacketClassification::Broadcast)
      {
        auto& counter = broadcasts[observation.source_mac];
        ++counter.packets;
        counter.bytes += observation.frame_length;
        counter.ingress_port = observation.ingress_port;
      }
      if (observation.classification == PacketClassification::UnknownUnicast)
      {
        auto& counter = unknown_unicasts[observation.source_mac];
        ++counter.packets;
        counter.bytes += observation.frame_length;
        counter.ingress_port = observation.ingress_port;
      }
      if (observation.validity == PacketValidity::Valid && observation.protocol == 17)
      {
        auto& counter = udp_sources[observation.source_ipv4];
        ++counter.packets;
        counter.bytes += observation.frame_length;
        counter.ingress_port = observation.ingress_port;
      }
      if (observation.validity == PacketValidity::Valid && (observation.protocol == 6 || observation.protocol == 17))
      {
        scan_destinations[observation.source_ipv4].insert(observation.destination_port);
        scan_ingress_ports[observation.source_ipv4] = observation.ingress_port;
      }
      if (observation.classification != PacketClassification::Malformed)
      {
        ingress_ports[observation.source_mac].push_back(observation.ingress_port);
      }
      if (observation.validity == PacketValidity::Valid)
      {
        auto& counter = source_traffic[observation.source_mac];
        ++counter.packets;
        counter.bytes += observation.frame_length;
        counter.ingress_port = observation.ingress_port;
      }
      else
      {
        auto& counter = malformed_ingress_ports[observation.ingress_port];
        ++counter.packets;
        counter.bytes += observation.frame_length;
      }
    }

    std::vector<AnomalyEvent> candidates;
    const auto add_candidate = [this, &candidates](
                                   AnomalyType type,
                                   const MacAddress& source_mac,
                                   uint32_t source_ipv4,
                                   uint32_t ingress_port,
                                   uint64_t observed_packets,
                                   uint64_t observed_bytes,
                                   uint64_t observed_distinct_destinations,
                                   uint64_t threshold)
    {
      if (threshold == 0)
      {
        return;
      }
      if (observed_packets <= threshold && observed_distinct_destinations <= threshold)
      {
        return;
      }
      candidates.push_back(
          { type,
            source_mac,
            source_ipv4,
            ingress_port,
            observed_packets,
            observed_bytes,
            observed_distinct_destinations,
            threshold,
            config_.window_duration_ns });
    };

    for (const auto& [source_mac, counter] : broadcasts)
    {
      add_candidate(
          AnomalyType::BroadcastStorm,
          source_mac,
          0,
          counter.ingress_port,
          counter.packets,
          counter.bytes,
          0,
          config_.broadcast_packets_threshold);
    }
    for (const auto& [source_mac, counter] : unknown_unicasts)
    {
      add_candidate(
          AnomalyType::UnknownUnicastFlood,
          source_mac,
          0,
          counter.ingress_port,
          counter.packets,
          counter.bytes,
          0,
          config_.unknown_unicast_packets_threshold);
    }
    for (const auto& [source_ipv4, counter] : udp_sources)
    {
      add_candidate(
          AnomalyType::UdpFlood,
          MacAddress(),
          source_ipv4,
          counter.ingress_port,
          counter.packets,
          counter.bytes,
          0,
          config_.udp_packets_threshold);
    }
    for (const auto& [source_ipv4, destinations] : scan_destinations)
    {
      const auto port = scan_ingress_ports.find(source_ipv4);
      add_candidate(
          AnomalyType::PortScan,
          MacAddress(),
          source_ipv4,
          port == scan_ingress_ports.end() ? 0 : port->second,
          0,
          0,
          destinations.size(),
          config_.port_scan_destinations_threshold);
    }
    for (const auto& [source_mac, ports] : ingress_ports)
    {
      uint64_t transitions = 0;
      for (size_t index = 1; index < ports.size(); ++index)
      {
        transitions += ports[index] != ports[index - 1];
      }
      add_candidate(
          AnomalyType::MacFlap,
          source_mac,
          0,
          ports.empty() ? 0 : ports.back(),
          transitions,
          0,
          0,
          config_.mac_flap_transitions_threshold);
    }
    for (const auto& [source_mac, counter] : source_traffic)
    {
      add_candidate(
          AnomalyType::HotTalker,
          source_mac,
          0,
          counter.ingress_port,
          counter.packets,
          counter.bytes,
          0,
          config_.hot_talker_packets_threshold);
    }
    for (const auto& [ingress_port, counter] : malformed_ingress_ports)
    {
      add_candidate(
          AnomalyType::MalformedFrame,
          MacAddress(),
          0,
          ingress_port,
          counter.packets,
          counter.bytes,
          0,
          config_.malformed_frames_threshold);
    }

    std::sort(candidates.begin(), candidates.end(), event_less);
    std::set<ActiveAnomaly> current_anomalies;
    std::vector<AnomalyEvent> events;
    for (const auto& candidate : candidates)
    {
      const ActiveAnomaly active{ candidate.type, candidate.source_mac, candidate.source_ipv4, candidate.ingress_port };
      current_anomalies.insert(active);
      if (active_anomalies_.find(active) == active_anomalies_.end())
      {
        events.push_back(candidate);
      }
    }
    active_anomalies_ = std::move(current_anomalies);
    return events;
  }

  void AnomalyDetector::reset() noexcept
  {
    observations_.clear();
    active_anomalies_.clear();
    last_timestamp_ns_ = 0;
    has_timestamp_ = false;
  }

  void AnomalyDetector::expire(uint64_t timestamp_ns) noexcept
  {
    const uint64_t cutoff = timestamp_ns > config_.window_duration_ns ? timestamp_ns - config_.window_duration_ns : 0;
    while (!observations_.empty() && observations_.front().timestamp_ns < cutoff)
    {
      observations_.pop_front();
    }
  }
}  // namespace wirelab
