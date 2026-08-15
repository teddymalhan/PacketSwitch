#ifndef PROJECT_CUDA_PACKET_PARSER_HPP_
#define PROJECT_CUDA_PACKET_PARSER_HPP_

#include <cstdint>
#include <vector>

#include "wirelab/packet_analyzer.hpp"
#include "wirelab/packet_batch.hpp"

namespace wirelab
{
  // Offline GPU parser. Forwarding classification remains host-owned because it
  // depends on the ordered MAC-learning state maintained by the dataplane.
  class CudaPacketParser final
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

    [[nodiscard]] static bool is_available() noexcept;
    [[nodiscard]] std::vector<PacketAnalysis> parse(const PacketBatch& batch) const;
    [[nodiscard]] ParseResult parse_with_timing(const PacketBatch& batch) const;
  };

  // Uses the GPU for packet parsing and the shared host aggregator for ordered
  // MAC learning plus compact histograms, traffic matrices, and flow results.
  class CudaPacketAnalyzer final : public PacketAnalyzer
  {
   public:
    [[nodiscard]] AnalysisBatch analyze(const PacketView* packets, size_t packet_count) override;
    [[nodiscard]] AnalysisBatch analyze(const PacketBatch& batch) override;
    [[nodiscard]] CudaPacketParser::Timing last_timing() const noexcept;
    void reset() noexcept;

   private:
    CudaPacketParser parser_;
    PacketAnalysisAggregator aggregator_;
    CudaPacketParser::Timing last_timing_;
  };
}

#endif
