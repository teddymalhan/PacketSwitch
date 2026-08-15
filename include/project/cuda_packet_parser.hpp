#ifndef PROJECT_CUDA_PACKET_PARSER_HPP_
#define PROJECT_CUDA_PACKET_PARSER_HPP_

#include <vector>

#include "project/packet_analyzer.hpp"
#include "project/packet_batch.hpp"

namespace project
{
  // Offline GPU parser. Forwarding classification remains host-owned because it
  // depends on the ordered MAC-learning state maintained by the dataplane.
  class CudaPacketParser final
  {
   public:
    [[nodiscard]] static bool is_available() noexcept;
    [[nodiscard]] std::vector<PacketAnalysis> parse(const PacketBatch& batch) const;
  };

  // Uses the GPU for packet parsing and the shared host aggregator for ordered
  // MAC learning plus compact histograms, traffic matrices, and flow results.
  class CudaPacketAnalyzer final : public PacketAnalyzer
  {
   public:
    [[nodiscard]] AnalysisBatch analyze(const PacketView* packets, size_t packet_count) override;
    [[nodiscard]] AnalysisBatch analyze(const PacketBatch& batch) override;
    void reset() noexcept;

   private:
    CudaPacketParser parser_;
    PacketAnalysisAggregator aggregator_;
  };
}

#endif
