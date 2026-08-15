#ifndef PROJECT_METAL_PACKET_PARSER_HPP_
#define PROJECT_METAL_PACKET_PARSER_HPP_

#include <cstdint>
#include <memory>
#include <vector>

#include "wirelab/packet_analyzer.hpp"
#include "wirelab/packet_batch.hpp"

namespace wirelab
{
  // Offline GPU parser for Apple GPUs (Metal). Mirrors CudaPacketParser;
  // forwarding classification remains host-owned because it depends on the
  // ordered MAC-learning state maintained by the dataplane.
  class MetalPacketParser final
  {
   public:
    struct Timing
    {
      uint64_t host_to_device_ns = 0;
      uint64_t kernel_ns = 0;
      uint64_t device_to_host_ns = 0;
    };

    struct ParseResult
    {
      std::vector<PacketAnalysis> packets;
      Timing timing;
    };

    MetalPacketParser();
    ~MetalPacketParser();
    MetalPacketParser(const MetalPacketParser&) = delete;
    MetalPacketParser& operator=(const MetalPacketParser&) = delete;

    [[nodiscard]] static bool is_available() noexcept;
    [[nodiscard]] std::vector<PacketAnalysis> parse(const PacketBatch& batch) const;
    [[nodiscard]] ParseResult parse_with_timing(const PacketBatch& batch) const;

   private:
    struct Impl;
    mutable std::unique_ptr<Impl> impl_;
  };

  // Uses the GPU for packet parsing and the shared host aggregator for ordered
  // MAC learning plus compact histograms, traffic matrices, and flow results.
  class MetalPacketAnalyzer final : public PacketAnalyzer
  {
   public:
    [[nodiscard]] AnalysisBatch analyze(const PacketView* packets, size_t packet_count) override;
    [[nodiscard]] AnalysisBatch analyze(const PacketBatch& batch) override;
    [[nodiscard]] MetalPacketParser::Timing last_timing() const noexcept;
    void reset() noexcept;

   private:
    MetalPacketParser parser_;
    PacketAnalysisAggregator aggregator_;
    MetalPacketParser::Timing last_timing_;
  };
}

#endif
