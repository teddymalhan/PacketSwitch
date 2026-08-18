#ifndef PROJECT_METAL_PACKET_PARSER_HPP_
#define PROJECT_METAL_PACKET_PARSER_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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

  // Live GPU parser: the batch-at-a-time parser above, pipelined.
  //
  // MetalPacketParser is an offline shape. It allocates a fresh buffer set per
  // batch and blocks on the GPU, so a live caller pays an allocation and then
  // stands idle for the whole kernel. This parser keeps a ring of slots whose
  // buffers persist across batches and hands the command buffer to the GPU
  // without waiting, so the host fills batch N+1 while batch N is still running.
  //
  // Slots use MTLResourceStorageModeShared, which is the Apple equivalent of the
  // pinned host memory the CUDA path would need: the buffer is visible to both
  // processors, so filling it *is* the transfer and no staging copy exists to
  // overlap. What overlaps here is host fill against GPU execution.
  //
  // Results are always returned in submission order, because the host aggregator
  // downstream learns MAC addresses in order and would otherwise disagree with
  // the CPU analyzer on which frames were unknown unicast.
  //
  // One instance is not thread-safe; it expects the single thread that owns the
  // batches, which is how both the dataplane and the benchmark drive it.
  class MetalStreamParser final
  {
   public:
    struct Timing
    {
      // Filling the shared buffers, which is the transfer on unified memory.
      uint64_t host_to_device_ns = 0;
      // GPU execution, taken from the command buffer's own device timestamps.
      uint64_t kernel_ns = 0;
      // Copying analyses back out of the shared result buffer.
      uint64_t device_to_host_ns = 0;
      // How long submit() stood still because every slot was still in flight.
      // Non-zero means the GPU, not the host, is the bottleneck.
      uint64_t queue_wait_ns = 0;
      // Submit to result in hand, transfers included. The honest live latency:
      // a kernel time that hides the copy is not a result.
      uint64_t transfer_inclusive_ns = 0;
    };

    struct Batch
    {
      std::vector<PacketAnalysis> packets;
      Timing timing;
      // Submission order, from zero, so a caller can prove nothing was reordered.
      uint64_t sequence = 0;
      uint64_t timestamp_ns = 0;
    };

    // Three slots keep one batch filling, one running, and one draining. Two
    // already overlaps; beyond three the extra memory buys nothing measurable.
    static constexpr size_t DEFAULT_PIPELINE_DEPTH = 3;
    static constexpr size_t MAX_PIPELINE_DEPTH = 16;

    explicit MetalStreamParser(size_t pipeline_depth = DEFAULT_PIPELINE_DEPTH);
    ~MetalStreamParser();
    MetalStreamParser(const MetalStreamParser&) = delete;
    MetalStreamParser& operator=(const MetalStreamParser&) = delete;

    [[nodiscard]] static bool is_available() noexcept;
    [[nodiscard]] size_t pipeline_depth() const noexcept;
    // Batches submitted whose results have not been harvested yet.
    [[nodiscard]] size_t in_flight() const noexcept;
    [[nodiscard]] bool idle() const noexcept;

    // Copies the batch into a free slot and commits it without waiting. When
    // every slot is busy this waits for the oldest, whose results are kept for
    // collection rather than dropped, and reuses its slot.
    void submit(const PacketBatch& batch);

    // The oldest finished batch, or nothing when the oldest is still running.
    [[nodiscard]] std::optional<Batch> try_collect();
    // The oldest submitted batch, waiting for the GPU if it has not finished.
    [[nodiscard]] std::optional<Batch> collect();
    // Every outstanding batch, in submission order.
    [[nodiscard]] std::vector<Batch> drain();

    // Buffer allocations since construction. A steady workload should stop
    // growing these, which is what makes the reuse claim testable rather than
    // merely stated.
    [[nodiscard]] size_t buffer_allocations() const noexcept;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

  // The streaming parser plus the shared host aggregator, in the two shapes
  // callers need: PacketAnalyzer for anything that wants one batch answered now,
  // and submit/collect for a live caller that would rather not block.
  class MetalStreamingAnalyzer final : public PacketAnalyzer, public StreamingPacketAnalyzer
  {
   public:
    explicit MetalStreamingAnalyzer(size_t pipeline_depth = MetalStreamParser::DEFAULT_PIPELINE_DEPTH);

    // Drains anything outstanding first, so the ordered aggregator sees batches
    // in submission order even when a caller mixes both shapes.
    [[nodiscard]] AnalysisBatch analyze(const PacketView* packets, size_t packet_count) override;
    [[nodiscard]] AnalysisBatch analyze(const PacketBatch& batch) override;

    void submit(const PacketBatch& batch) override;
    [[nodiscard]] std::optional<AnalysisBatch> try_collect() override;
    [[nodiscard]] std::vector<AnalysisBatch> drain() override;
    [[nodiscard]] size_t in_flight() const noexcept override;

    [[nodiscard]] MetalStreamParser::Timing last_timing() const noexcept;
    void reset() noexcept;

   private:
    [[nodiscard]] AnalysisBatch aggregate(MetalStreamParser::Batch parsed);

    MetalStreamParser parser_;
    PacketAnalysisAggregator aggregator_;
    MetalStreamParser::Timing last_timing_;
  };
}

#endif
