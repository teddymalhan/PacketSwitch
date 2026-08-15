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
}

#endif
