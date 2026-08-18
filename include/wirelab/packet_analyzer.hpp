#ifndef PROJECT_PACKET_ANALYZER_HPP_
#define PROJECT_PACKET_ANALYZER_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

#include "wirelab/ethernet_frame.hpp"

namespace wirelab
{
  struct PacketBatch;

  struct PacketView
  {
    const uint8_t* bytes = nullptr;
    size_t size = 0;
    uint32_t ingress_port = 0;
  };

  enum class PacketClassification
  {
    Malformed,
    Broadcast,
    UnknownUnicast,
    KnownUnicast
  };

  enum class PacketValidity
  {
    Valid,
    MalformedEthernet,
    MalformedIpv4,
    MalformedTransport
  };

  [[nodiscard]] const char* to_string(PacketClassification classification) noexcept;
  [[nodiscard]] const char* to_string(PacketValidity validity) noexcept;

  struct FlowKey
  {
    uint32_t source_ipv4 = 0;
    uint32_t destination_ipv4 = 0;
    uint16_t source_port = 0;
    uint16_t destination_port = 0;
    uint8_t protocol = 0;

    [[nodiscard]] bool operator==(const FlowKey& other) const noexcept
    {
      return source_ipv4 == other.source_ipv4 && destination_ipv4 == other.destination_ipv4 &&
             source_port == other.source_port && destination_port == other.destination_port && protocol == other.protocol;
    }
  };

  struct PacketAnalysis
  {
    MacAddress source_mac;
    MacAddress destination_mac;
    uint32_t source_ipv4 = 0;
    uint32_t destination_ipv4 = 0;
    uint16_t source_port = 0;
    uint16_t destination_port = 0;
    uint16_t ethertype = 0;
    uint16_t frame_length = 0;
    uint32_t ingress_port = 0;
    uint8_t protocol = 0;
    uint8_t tcp_flags = 0;
    uint64_t flow_hash = 0;
    PacketValidity validity = PacketValidity::MalformedEthernet;
    PacketClassification classification = PacketClassification::Malformed;
  };

  struct PacketSizeHistogramBucket
  {
    uint64_t inclusive_minimum = 0;
    uint64_t inclusive_maximum = 0;
    uint64_t packet_count = 0;
    uint64_t byte_count = 0;
  };

  struct HistogramEntry
  {
    uint32_t value = 0;
    uint64_t packet_count = 0;
    uint64_t byte_count = 0;
  };

  struct FlowRecord
  {
    FlowKey key;
    uint64_t flow_hash = 0;
    uint64_t packet_count = 0;
    uint64_t byte_count = 0;
  };

  struct MacTrafficRecord
  {
    MacAddress mac;
    uint64_t packet_count = 0;
    uint64_t byte_count = 0;
  };

  struct TrafficMatrixEntry
  {
    MacAddress source_mac;
    MacAddress destination_mac;
    uint64_t packet_count = 0;
    uint64_t byte_count = 0;
  };

  struct AnalysisBatch
  {
    std::array<PacketSizeHistogramBucket, 7> frame_size_histogram;
    std::vector<HistogramEntry> ethertype_histogram;
    std::vector<HistogramEntry> protocol_histogram;
    std::vector<HistogramEntry> destination_port_histogram;
    std::vector<PacketAnalysis> packets;
    // Ranked by byte count, packet count, then MAC address.
    std::vector<MacTrafficRecord> source_mac_traffic;
    // Ranked by byte count, packet count, then MAC address.
    std::vector<MacTrafficRecord> destination_mac_traffic;
    std::vector<TrafficMatrixEntry> mac_traffic_matrix;
    // Ranked by byte count, packet count, then five-tuple.
    std::vector<FlowRecord> flows;
    uint64_t received_packets = 0;
    uint64_t received_bytes = 0;
    uint64_t malformed_packets = 0;
    uint64_t broadcast_packets = 0;
    uint64_t unknown_unicast_packets = 0;
    uint64_t known_unicast_packets = 0;
  };

  class PacketAnalyzer
  {
   public:
    virtual ~PacketAnalyzer() = default;
    [[nodiscard]] virtual AnalysisBatch analyze(const PacketView* packets, size_t packet_count) = 0;
    [[nodiscard]] virtual AnalysisBatch analyze(const PacketBatch& batch) = 0;
  };

  // An analyzer that can be handed a batch now and asked for the answer later.
  //
  // A device analyzer that answers synchronously leaves the host idle for the
  // whole kernel, which is affordable offline and not affordable live. This is
  // the shape that lets a caller keep feeding while results come back, and it
  // exists as its own interface because most callers do not want it: a batch
  // answered now is simpler, and simpler is right until it is measurably slow.
  //
  // Results always arrive in submission order, because the analyzers behind it
  // learn MAC addresses in order.
  class StreamingPacketAnalyzer
  {
   public:
    virtual ~StreamingPacketAnalyzer() = default;
    virtual void submit(const PacketBatch& batch) = 0;
    // The oldest finished batch, or nothing when the oldest is still running.
    [[nodiscard]] virtual std::optional<AnalysisBatch> try_collect() = 0;
    // Every outstanding batch, in submission order.
    [[nodiscard]] virtual std::vector<AnalysisBatch> drain() = 0;
    [[nodiscard]] virtual size_t in_flight() const noexcept = 0;
  };

  class PacketAnalysisAggregator final
  {
   public:
    [[nodiscard]] AnalysisBatch aggregate(std::vector<PacketAnalysis> packets, const uint64_t* frame_lengths = nullptr);
    void reset() noexcept;

   private:
    std::unordered_set<MacAddress> learned_macs_;
  };

  class CpuPacketAnalyzer final : public PacketAnalyzer
  {
   public:
    [[nodiscard]] AnalysisBatch analyze(const PacketView* packets, size_t packet_count) override;
    [[nodiscard]] AnalysisBatch analyze(const PacketBatch& batch) override;
    void reset() noexcept;

   private:
    PacketAnalysisAggregator aggregator_;
  };
}  // namespace wirelab

#endif
