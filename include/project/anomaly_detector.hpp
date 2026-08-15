#ifndef PROJECT_ANOMALY_DETECTOR_HPP_
#define PROJECT_ANOMALY_DETECTOR_HPP_

#include <cstdint>
#include <deque>
#include <set>
#include <vector>

#include "project/packet_analyzer.hpp"

namespace project
{
  enum class AnomalyType
  {
    BroadcastStorm,
    MacFlap,
    UnknownUnicastFlood,
    UdpFlood,
    PortScan,
    HotTalker,
    MalformedFrame
  };

  struct AnomalyDetectorConfig
  {
    uint64_t window_duration_ns = 1'000'000'000;
    uint64_t broadcast_packets_threshold = 0;
    uint64_t mac_flap_transitions_threshold = 0;
    uint64_t unknown_unicast_packets_threshold = 0;
    uint64_t udp_packets_threshold = 0;
    uint64_t port_scan_destinations_threshold = 0;
    uint64_t hot_talker_packets_threshold = 0;
    uint64_t malformed_frames_threshold = 0;

  };

  struct AnomalyEvent
  {
    AnomalyType type = AnomalyType::BroadcastStorm;
    MacAddress source_mac;
    uint32_t source_ipv4 = 0;
    uint32_t ingress_port = 0;
    uint64_t observed_packets = 0;
    uint64_t observed_bytes = 0;
    uint64_t observed_distinct_destinations = 0;
    uint64_t threshold = 0;
    uint64_t window_duration_ns = 0;
  };

  class AnomalyDetector
  {
   public:
    explicit AnomalyDetector(AnomalyDetectorConfig config);

    [[nodiscard]] std::vector<AnomalyEvent> evaluate(const AnalysisBatch& batch, uint64_t timestamp_ns);
    void reset() noexcept;

   private:
    struct ObservedPacket
    {
      uint64_t timestamp_ns = 0;
      MacAddress source_mac;
      uint32_t source_ipv4 = 0;
      uint16_t destination_port = 0;
      uint16_t frame_length = 0;
      uint32_t ingress_port = 0;
      uint8_t protocol = 0;
      PacketClassification classification = PacketClassification::Malformed;
      PacketValidity validity = PacketValidity::MalformedEthernet;
    };

    struct ActiveAnomaly
    {
      AnomalyType type = AnomalyType::BroadcastStorm;
      MacAddress source_mac;
      uint32_t source_ipv4 = 0;
      uint32_t ingress_port = 0;

      [[nodiscard]] bool operator<(const ActiveAnomaly& other) const noexcept;
    };

    void expire(uint64_t timestamp_ns) noexcept;

    AnomalyDetectorConfig config_;
    std::deque<ObservedPacket> observations_;
    std::set<ActiveAnomaly> active_anomalies_;
    uint64_t last_timestamp_ns_ = 0;
    bool has_timestamp_ = false;
  };
}

#endif
