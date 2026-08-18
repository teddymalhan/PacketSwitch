// WireLab Metal packet parser: Apple GPU port of the CUDA offline parser.
// Host side only; the kernel is compiled at runtime from the embedded MSL
// source (see scripts/embed_metal.py and src/metal_kernel.metal).

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "wirelab/metal_packet_parser.hpp"

#include "metal_kernel_source.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace wirelab
{
  namespace
  {
    constexpr size_t METAL_THREADS_PER_GROUP = 256;

    // Layout must match the DevicePacketAnalysis struct in src/metal_kernel.metal.
    struct DevicePacketAnalysis
    {
      uint8_t source_mac[MAC_ADDRESS_SIZE]{};
      uint8_t destination_mac[MAC_ADDRESS_SIZE]{};
      uint32_t source_ipv4 = 0;
      uint32_t destination_ipv4 = 0;
      uint32_t ingress_port = 0;
      uint16_t source_port = 0;
      uint16_t destination_port = 0;
      uint16_t ethertype = 0;
      uint16_t frame_length = 0;
      uint8_t protocol = 0;
      uint8_t tcp_flags = 0;
      uint8_t validity = static_cast<uint8_t>(PacketValidity::MalformedEthernet);
      uint64_t flow_hash = 0;
    };
    static_assert(sizeof(DevicePacketAnalysis) == 48, "DevicePacketAnalysis layout must match the MSL kernel");
    static_assert(offsetof(DevicePacketAnalysis, ingress_port) == 20,
                  "DevicePacketAnalysis ingress_port offset must match the MSL kernel");
    static_assert(offsetof(DevicePacketAnalysis, flow_hash) == 40,
                  "DevicePacketAnalysis flow_hash offset must match the MSL kernel");

    [[noreturn]] void throw_metal_error(NSString* operation, NSError* error)
    {
      std::string message([operation UTF8String]);
      if (error != nil)
      {
        message += ": ";
        message += [error.localizedDescription UTF8String];
      }
      throw std::runtime_error(message);
    }

    uint64_t elapsed_ns(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point finish)
    {
      return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count());
    }

    void validate_batch(const PacketBatch& batch)
    {
      const size_t packet_count = batch.packet_count();
      if (batch.packet_offsets.size() != packet_count + 1 || batch.packet_lengths.size() != packet_count ||
          batch.sender_ids.size() != packet_count || batch.packet_offsets.empty() || batch.packet_offsets.front() != 0 ||
          batch.packet_offsets.back() != batch.packet_bytes.size())
      {
        throw std::invalid_argument("invalid contiguous packet batch");
      }
      for (size_t index = 0; index < packet_count; ++index)
      {
        if (batch.packet_offsets[index] > batch.packet_offsets[index + 1] ||
            batch.packet_offsets[index + 1] - batch.packet_offsets[index] != batch.packet_lengths[index])
        {
          throw std::invalid_argument("invalid contiguous packet batch offsets");
        }
      }
    }

    PacketAnalysis to_packet_analysis(const DevicePacketAnalysis& device_result)
    {
      PacketAnalysis analysis;
      std::array<uint8_t, MAC_ADDRESS_SIZE> source{};
      std::array<uint8_t, MAC_ADDRESS_SIZE> destination{};
      for (size_t index = 0; index < MAC_ADDRESS_SIZE; ++index)
      {
        source[index] = device_result.source_mac[index];
        destination[index] = device_result.destination_mac[index];
      }
      analysis.source_mac = MacAddress(source);
      analysis.destination_mac = MacAddress(destination);
      analysis.source_ipv4 = device_result.source_ipv4;
      analysis.destination_ipv4 = device_result.destination_ipv4;
      analysis.ingress_port = device_result.ingress_port;
      analysis.source_port = device_result.source_port;
      analysis.destination_port = device_result.destination_port;
      analysis.ethertype = device_result.ethertype;
      analysis.frame_length = device_result.frame_length;
      analysis.protocol = device_result.protocol;
      analysis.tcp_flags = device_result.tcp_flags;
      analysis.validity = static_cast<PacketValidity>(device_result.validity);
      analysis.flow_hash = device_result.flow_hash;
      return analysis;
    }

    // Device, queue and compiled kernel. Both parsers need exactly this much
    // Metal, and compiling the kernel twice for one process would be waste.
    struct MetalContext
    {
      id<MTLDevice> device = nil;
      id<MTLCommandQueue> queue = nil;
      id<MTLComputePipelineState> pipeline = nil;

      MetalContext()
      {
        @autoreleasepool
        {
          device = MTLCreateSystemDefaultDevice();
          if (device == nil)
          {
            throw std::runtime_error("no compatible Metal device is available");
          }
          queue = [device newCommandQueue];
          if (queue == nil)
          {
            throw std::runtime_error("failed to create the Metal command queue");
          }
          NSString* source = [[NSString alloc] initWithBytes:wirelab::metal::kernel_source
                                                      length:wirelab::metal::kernel_source_len
                                                    encoding:NSUTF8StringEncoding];
          NSError* error = nil;
          id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
          if (library == nil)
          {
            throw_metal_error(@"Metal kernel compilation failed", error);
          }
          id<MTLFunction> function = [library newFunctionWithName:@"parse_packets"];
          if (function == nil)
          {
            throw std::runtime_error("Metal kernel function 'parse_packets' was not found");
          }
          pipeline = [device newComputePipelineStateWithFunction:function error:&error];
          if (pipeline == nil)
          {
            throw_metal_error(@"Metal compute pipeline creation failed", error);
          }
        }
      }

      void encode(id<MTLCommandBuffer> command,
                  id<MTLBuffer> bytes,
                  id<MTLBuffer> offsets,
                  id<MTLBuffer> lengths,
                  id<MTLBuffer> sender_ids,
                  id<MTLBuffer> analyses,
                  id<MTLBuffer> count,
                  size_t packet_count) const
      {
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:bytes offset:0 atIndex:0];
        [encoder setBuffer:offsets offset:0 atIndex:1];
        [encoder setBuffer:lengths offset:0 atIndex:2];
        [encoder setBuffer:sender_ids offset:0 atIndex:3];
        [encoder setBuffer:analyses offset:0 atIndex:4];
        [encoder setBuffer:count offset:0 atIndex:5];
        const NSUInteger threads_per_group = METAL_THREADS_PER_GROUP;
        const NSUInteger threadgroups = (packet_count + threads_per_group - 1) / threads_per_group;
        [encoder dispatchThreadgroups:MTLSizeMake(threadgroups, 1, 1)
                threadsPerThreadgroup:MTLSizeMake(threads_per_group, 1, 1)];
        [encoder endEncoding];
      }
    };
  }

  struct MetalPacketParser::Impl
  {
    MetalContext context;

    id<MTLDevice> device() const noexcept
    {
      return context.device;
    }
  };

  MetalPacketParser::MetalPacketParser() = default;
  MetalPacketParser::~MetalPacketParser() = default;

  bool MetalPacketParser::is_available() noexcept
  {
    @autoreleasepool
    {
      return MTLCreateSystemDefaultDevice() != nil;
    }
  }

  std::vector<PacketAnalysis> MetalPacketParser::parse(const PacketBatch& batch) const
  {
    return parse_with_timing(batch).packets;
  }

  MetalPacketParser::ParseResult MetalPacketParser::parse_with_timing(const PacketBatch& batch) const
  {
    @autoreleasepool
    {
      validate_batch(batch);
      if (!is_available())
      {
        throw std::runtime_error("no compatible Metal device is available");
      }
      if (impl_ == nullptr)
      {
        impl_ = std::make_unique<Impl>();
      }
      const size_t packet_count = batch.packet_count();
      if (packet_count == 0)
      {
        return {};
      }
      if (packet_count > std::numeric_limits<uint32_t>::max())
      {
        throw std::invalid_argument("packet batch exceeds the Metal dispatch limit");
      }

      const auto host_to_device_started = std::chrono::steady_clock::now();
      id<MTLBuffer> device_bytes = [impl_->context.device newBufferWithBytes:batch.packet_bytes.data()
                                                                      length:batch.packet_bytes.size()
                                                                     options:MTLResourceStorageModeShared];
      id<MTLBuffer> device_offsets = [impl_->context.device newBufferWithBytes:batch.packet_offsets.data()
                                                                        length:batch.packet_offsets.size() * sizeof(uint32_t)
                                                                       options:MTLResourceStorageModeShared];
      id<MTLBuffer> device_lengths = [impl_->context.device newBufferWithBytes:batch.packet_lengths.data()
                                                                        length:batch.packet_lengths.size() * sizeof(uint16_t)
                                                                       options:MTLResourceStorageModeShared];
      id<MTLBuffer> device_sender_ids = [impl_->context.device newBufferWithBytes:batch.sender_ids.data()
                                                                           length:batch.sender_ids.size() * sizeof(uint32_t)
                                                                          options:MTLResourceStorageModeShared];
      const uint32_t count_value = static_cast<uint32_t>(packet_count);
      id<MTLBuffer> device_count = [impl_->context.device newBufferWithBytes:&count_value
                                                                      length:sizeof(count_value)
                                                                     options:MTLResourceStorageModeShared];
      id<MTLBuffer> device_analyses =
          [impl_->context.device newBufferWithLength:packet_count * sizeof(DevicePacketAnalysis)
                                             options:MTLResourceStorageModeShared];
      const auto host_to_device_finished = std::chrono::steady_clock::now();

      const auto kernel_started = std::chrono::steady_clock::now();
      id<MTLCommandBuffer> command = [impl_->context.queue commandBuffer];
      impl_->context.encode(command,
                            device_bytes,
                            device_offsets,
                            device_lengths,
                            device_sender_ids,
                            device_analyses,
                            device_count,
                            packet_count);
      [command commit];
      [command waitUntilCompleted];
      if (command.error != nil)
      {
        throw_metal_error(@"Metal packet parser kernel failed", command.error);
      }
      const auto kernel_finished = std::chrono::steady_clock::now();

      const auto device_to_host_started = std::chrono::steady_clock::now();
      std::vector<DevicePacketAnalysis> device_results(packet_count);
      std::memcpy(device_results.data(), device_analyses.contents, packet_count * sizeof(DevicePacketAnalysis));
      const auto device_to_host_finished = std::chrono::steady_clock::now();

      std::vector<PacketAnalysis> analyses;
      analyses.reserve(packet_count);
      for (const DevicePacketAnalysis& device_result : device_results)
      {
        analyses.push_back(to_packet_analysis(device_result));
      }

      ParseResult result;
      result.packets = std::move(analyses);
      result.timing.host_to_device_ns = elapsed_ns(host_to_device_started, host_to_device_finished);
      result.timing.kernel_ns = elapsed_ns(kernel_started, kernel_finished);
      result.timing.device_to_host_ns = elapsed_ns(device_to_host_started, device_to_host_finished);
      return result;
    }
  }

  AnalysisBatch MetalPacketAnalyzer::analyze(const PacketView* packets, size_t packet_count)
  {
    const auto batch = PacketBatch::create(packets, packet_count);
    if (!batch)
    {
      throw std::invalid_argument(std::string("cannot create Metal packet batch: ") + to_string(batch.error()));
    }
    return analyze(*batch);
  }

  AnalysisBatch MetalPacketAnalyzer::analyze(const PacketBatch& batch)
  {
    auto parsed = parser_.parse_with_timing(batch);
    last_timing_ = parsed.timing;
    return aggregator_.aggregate(std::move(parsed.packets));
  }

  MetalPacketParser::Timing MetalPacketAnalyzer::last_timing() const noexcept
  {
    return last_timing_;
  }

  void MetalPacketAnalyzer::reset() noexcept
  {
    aggregator_.reset();
    last_timing_ = {};
  }

  struct MetalStreamParser::Impl
  {
    // One in-flight batch: its persistent buffers, the command buffer that is
    // still running on them, and what we knew at submit time.
    struct Slot
    {
      id<MTLBuffer> bytes = nil;
      id<MTLBuffer> offsets = nil;
      id<MTLBuffer> lengths = nil;
      id<MTLBuffer> sender_ids = nil;
      id<MTLBuffer> count = nil;
      id<MTLBuffer> analyses = nil;
      id<MTLCommandBuffer> command = nil;
      size_t packet_count = 0;
      uint64_t sequence = 0;
      uint64_t timestamp_ns = 0;
      Timing timing;
      std::chrono::steady_clock::time_point submitted_at;
      bool busy = false;
    };

    MetalContext context;
    std::vector<Slot> slots;
    // Ring cursors: submit into next_slot, harvest from oldest_slot.
    size_t next_slot = 0;
    size_t oldest_slot = 0;
    size_t busy_count = 0;
    uint64_t next_sequence = 0;
    size_t allocations = 0;
    std::deque<Batch> ready;

    explicit Impl(size_t pipeline_depth) : slots(pipeline_depth)
    {
    }

    // Grows a slot buffer only when the batch outgrows it, doubling so that a
    // workload with a jittery batch size settles instead of reallocating on
    // every peak. Metal rejects a zero-length buffer, hence the floor.
    void ensure_capacity(__strong id<MTLBuffer>& buffer, size_t required_length)
    {
      const size_t wanted = std::max<size_t>(required_length, 1);
      if (buffer != nil && buffer.length >= wanted)
      {
        return;
      }
      const size_t existing = buffer == nil ? 0 : static_cast<size_t>(buffer.length);
      const size_t length = std::max(wanted, existing * 2);
      buffer = [context.device newBufferWithLength:length options:MTLResourceStorageModeShared];
      if (buffer == nil)
      {
        throw std::runtime_error("failed to allocate a Metal streaming buffer");
      }
      ++allocations;
    }

    static void fill(id<MTLBuffer> buffer, const void* source, size_t length)
    {
      if (length != 0)
      {
        std::memcpy(buffer.contents, source, length);
      }
    }

    // Moves the oldest in-flight batch into ready. Returns false when there is
    // nothing in flight, or when wait is false and the GPU has not finished.
    bool harvest_oldest(bool wait)
    {
      if (busy_count == 0)
      {
        return false;
      }
      Slot& slot = slots[oldest_slot];
      // A slot with no command buffer is an empty batch holding its place in the
      // sequence. It is finished by definition, and waiting on it would be a
      // wait for nothing.
      const bool has_work = slot.command != nil;
      if (has_work)
      {
        if (!wait && slot.command.status != MTLCommandBufferStatusCompleted)
        {
          return false;
        }
        [slot.command waitUntilCompleted];
        if (slot.command.error != nil)
        {
          throw_metal_error(@"Metal streaming packet parser kernel failed", slot.command.error);
        }
      }

      const auto device_to_host_started = std::chrono::steady_clock::now();
      Batch batch;
      batch.packets.reserve(slot.packet_count);
      if (has_work)
      {
        const auto* results = static_cast<const DevicePacketAnalysis*>(slot.analyses.contents);
        for (size_t index = 0; index < slot.packet_count; ++index)
        {
          batch.packets.push_back(to_packet_analysis(results[index]));
        }
      }
      const auto device_to_host_finished = std::chrono::steady_clock::now();

      slot.timing.device_to_host_ns = elapsed_ns(device_to_host_started, device_to_host_finished);
      if (has_work)
      {
        // The device's own clock, so the number is the kernel and not the host's
        // wait for it. Zero on a device that does not report it, in which case
        // the host-observed wait is the closest honest answer we have.
        const double gpu_seconds = slot.command.GPUEndTime - slot.command.GPUStartTime;
        slot.timing.kernel_ns = gpu_seconds > 0.0 ? static_cast<uint64_t>(gpu_seconds * 1e9)
                                                  : elapsed_ns(slot.submitted_at, device_to_host_started);
      }
      slot.timing.transfer_inclusive_ns = elapsed_ns(slot.submitted_at, device_to_host_finished);

      batch.timing = slot.timing;
      batch.sequence = slot.sequence;
      batch.timestamp_ns = slot.timestamp_ns;
      ready.push_back(std::move(batch));

      slot.command = nil;
      slot.busy = false;
      oldest_slot = (oldest_slot + 1) % slots.size();
      --busy_count;
      return true;
    }

    // Claims the next slot, reclaiming the oldest when every one is in flight.
    // Returns how long the reclaim cost, which is the GPU telling us it is
    // behind the host.
    uint64_t claim_slot(std::chrono::steady_clock::time_point submitted_at)
    {
      if (busy_count != slots.size())
      {
        return 0;
      }
      harvest_oldest(true);
      return elapsed_ns(submitted_at, std::chrono::steady_clock::now());
    }

    void commit_slot(Slot& slot, uint64_t timestamp_ns, std::chrono::steady_clock::time_point submitted_at)
    {
      slot.sequence = next_sequence++;
      slot.timestamp_ns = timestamp_ns;
      slot.submitted_at = submitted_at;
      slot.busy = true;
      next_slot = (next_slot + 1) % slots.size();
      ++busy_count;
    }
  };

  MetalStreamParser::MetalStreamParser(size_t pipeline_depth)
  {
    if (pipeline_depth == 0 || pipeline_depth > MAX_PIPELINE_DEPTH)
    {
      throw std::invalid_argument("Metal pipeline depth must be between 1 and 16");
    }
    if (!is_available())
    {
      throw std::runtime_error("no compatible Metal device is available");
    }
    impl_ = std::make_unique<Impl>(pipeline_depth);
  }

  MetalStreamParser::~MetalStreamParser()
  {
    // A command buffer outliving the buffers it reads is a use-after-free the
    // GPU would discover for us, so let the queue finish before teardown.
    try
    {
      while (impl_ != nullptr && impl_->busy_count != 0)
      {
        impl_->harvest_oldest(true);
      }
    }
    catch (const std::exception&)
    {
      // A failed kernel cannot make a destructor useful; the wait still happened.
    }
  }

  bool MetalStreamParser::is_available() noexcept
  {
    @autoreleasepool
    {
      return MTLCreateSystemDefaultDevice() != nil;
    }
  }

  size_t MetalStreamParser::pipeline_depth() const noexcept
  {
    return impl_->slots.size();
  }

  size_t MetalStreamParser::in_flight() const noexcept
  {
    return impl_->busy_count + impl_->ready.size();
  }

  bool MetalStreamParser::idle() const noexcept
  {
    return in_flight() == 0;
  }

  size_t MetalStreamParser::buffer_allocations() const noexcept
  {
    return impl_->allocations;
  }

  void MetalStreamParser::submit(const PacketBatch& batch)
  {
    @autoreleasepool
    {
      validate_batch(batch);
      const size_t packet_count = batch.packet_count();
      if (packet_count > std::numeric_limits<uint32_t>::max())
      {
        throw std::invalid_argument("packet batch exceeds the Metal dispatch limit");
      }

      const auto submitted_at = std::chrono::steady_clock::now();
      if (packet_count == 0)
      {
        // Still a batch, and still in order. A live source ticks empty often, so
        // an empty batch takes a slot with no kernel rather than either jumping
        // the queue or draining the pipeline to stay behind it.
        const uint64_t queue_wait_ns = impl_->claim_slot(submitted_at);
        Impl::Slot& empty_slot = impl_->slots[impl_->next_slot];
        empty_slot.command = nil;
        empty_slot.packet_count = 0;
        empty_slot.timing = Timing{};
        empty_slot.timing.queue_wait_ns = queue_wait_ns;
        impl_->commit_slot(empty_slot, batch.timestamp_ns, submitted_at);
        return;
      }

      const uint64_t queue_wait_ns = impl_->claim_slot(submitted_at);

      Impl::Slot& slot = impl_->slots[impl_->next_slot];
      const auto host_to_device_started = std::chrono::steady_clock::now();
      impl_->ensure_capacity(slot.bytes, batch.packet_bytes.size());
      impl_->ensure_capacity(slot.offsets, batch.packet_offsets.size() * sizeof(uint32_t));
      impl_->ensure_capacity(slot.lengths, batch.packet_lengths.size() * sizeof(uint16_t));
      impl_->ensure_capacity(slot.sender_ids, batch.sender_ids.size() * sizeof(uint32_t));
      impl_->ensure_capacity(slot.count, sizeof(uint32_t));
      impl_->ensure_capacity(slot.analyses, packet_count * sizeof(DevicePacketAnalysis));

      const uint32_t count_value = static_cast<uint32_t>(packet_count);
      Impl::fill(slot.bytes, batch.packet_bytes.data(), batch.packet_bytes.size());
      Impl::fill(slot.offsets, batch.packet_offsets.data(), batch.packet_offsets.size() * sizeof(uint32_t));
      Impl::fill(slot.lengths, batch.packet_lengths.data(), batch.packet_lengths.size() * sizeof(uint16_t));
      Impl::fill(slot.sender_ids, batch.sender_ids.data(), batch.sender_ids.size() * sizeof(uint32_t));
      Impl::fill(slot.count, &count_value, sizeof(count_value));
      const auto host_to_device_finished = std::chrono::steady_clock::now();

      id<MTLCommandBuffer> command = [impl_->context.queue commandBuffer];
      impl_->context
          .encode(command, slot.bytes, slot.offsets, slot.lengths, slot.sender_ids, slot.analyses, slot.count, packet_count);
      [command commit];

      slot.command = command;
      slot.packet_count = packet_count;
      slot.timing = Timing{};
      slot.timing.queue_wait_ns = queue_wait_ns;
      slot.timing.host_to_device_ns = elapsed_ns(host_to_device_started, host_to_device_finished);
      impl_->commit_slot(slot, batch.timestamp_ns, submitted_at);
    }
  }

  std::optional<MetalStreamParser::Batch> MetalStreamParser::try_collect()
  {
    @autoreleasepool
    {
      if (impl_->ready.empty())
      {
        impl_->harvest_oldest(false);
      }
      if (impl_->ready.empty())
      {
        return std::nullopt;
      }
      Batch batch = std::move(impl_->ready.front());
      impl_->ready.pop_front();
      return batch;
    }
  }

  std::optional<MetalStreamParser::Batch> MetalStreamParser::collect()
  {
    @autoreleasepool
    {
      if (impl_->ready.empty())
      {
        impl_->harvest_oldest(true);
      }
      if (impl_->ready.empty())
      {
        return std::nullopt;
      }
      Batch batch = std::move(impl_->ready.front());
      impl_->ready.pop_front();
      return batch;
    }
  }

  std::vector<MetalStreamParser::Batch> MetalStreamParser::drain()
  {
    @autoreleasepool
    {
      while (impl_->busy_count != 0)
      {
        impl_->harvest_oldest(true);
      }
      std::vector<Batch> batches;
      batches.reserve(impl_->ready.size());
      while (!impl_->ready.empty())
      {
        batches.push_back(std::move(impl_->ready.front()));
        impl_->ready.pop_front();
      }
      return batches;
    }
  }

  MetalStreamingAnalyzer::MetalStreamingAnalyzer(size_t pipeline_depth) : parser_(pipeline_depth), last_timing_()
  {
  }

  AnalysisBatch MetalStreamingAnalyzer::aggregate(MetalStreamParser::Batch parsed)
  {
    last_timing_ = parsed.timing;
    return aggregator_.aggregate(std::move(parsed.packets));
  }

  AnalysisBatch MetalStreamingAnalyzer::analyze(const PacketView* packets, size_t packet_count)
  {
    const auto batch = PacketBatch::create(packets, packet_count);
    if (!batch)
    {
      throw std::invalid_argument(std::string("cannot create Metal packet batch: ") + to_string(batch.error()));
    }
    return analyze(*batch);
  }

  AnalysisBatch MetalStreamingAnalyzer::analyze(const PacketBatch& batch)
  {
    // Anything already submitted precedes this batch, and the aggregator learns
    // MAC addresses in order, so those have to be folded in first.
    for (auto& outstanding : parser_.drain())
    {
      (void)aggregate(std::move(outstanding));
    }
    parser_.submit(batch);
    auto parsed = parser_.collect();
    if (!parsed)
    {
      return aggregator_.aggregate({});
    }
    return aggregate(std::move(*parsed));
  }

  void MetalStreamingAnalyzer::submit(const PacketBatch& batch)
  {
    parser_.submit(batch);
  }

  std::optional<AnalysisBatch> MetalStreamingAnalyzer::try_collect()
  {
    auto parsed = parser_.try_collect();
    if (!parsed)
    {
      return std::nullopt;
    }
    return aggregate(std::move(*parsed));
  }

  std::vector<AnalysisBatch> MetalStreamingAnalyzer::drain()
  {
    std::vector<AnalysisBatch> batches;
    for (auto& parsed : parser_.drain())
    {
      batches.push_back(aggregate(std::move(parsed)));
    }
    return batches;
  }

  size_t MetalStreamingAnalyzer::in_flight() const noexcept
  {
    return parser_.in_flight();
  }

  MetalStreamParser::Timing MetalStreamingAnalyzer::last_timing() const noexcept
  {
    return last_timing_;
  }

  void MetalStreamingAnalyzer::reset() noexcept
  {
    aggregator_.reset();
    last_timing_ = {};
  }
}
