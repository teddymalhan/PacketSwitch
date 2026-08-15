#ifndef PROJECT_PACKET_ANALYZER_HPP_
#define PROJECT_PACKET_ANALYZER_HPP_

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "project/ethernet_frame.hpp"

namespace project
{
  struct PacketView
  {
    const uint8_t* bytes = nullptr;
    size_t size = 0;
  };

  enum class PacketClassification
  {
    Malformed,
    Broadcast,
    UnknownUnicast,
    KnownUnicast
  };

  struct PacketAnalysis
  {
    MacAddress source_mac;
    MacAddress destination_mac;
    uint16_t ethertype = 0;
    uint16_t frame_length = 0;
    PacketClassification classification = PacketClassification::Malformed;
  };

  struct AnalysisBatch
  {
    std::vector<PacketAnalysis> packets;
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
  };

  class CpuPacketAnalyzer final : public PacketAnalyzer
  {
   public:
    [[nodiscard]] AnalysisBatch analyze(const PacketView* packets, size_t packet_count) override;
    void reset() noexcept;

   private:
    std::unordered_set<MacAddress> learned_macs_;
  };
}

#endif
