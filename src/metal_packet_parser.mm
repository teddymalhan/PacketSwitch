// WireLab Metal packet parser: Apple GPU port of the CUDA offline parser.
// Host side only; the kernel is compiled at runtime from the embedded MSL
// source (see scripts/embed_metal.py and src/metal_kernel.metal).

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "wirelab/metal_packet_parser.hpp"

#include "metal_kernel_source.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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
  }

  struct MetalPacketParser::Impl
  {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLComputePipelineState> pipeline = nil;

    Impl()
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
      id<MTLBuffer> device_bytes = [impl_->device newBufferWithBytes:batch.packet_bytes.data()
                                                              length:batch.packet_bytes.size()
                                                             options:MTLResourceStorageModeShared];
      id<MTLBuffer> device_offsets = [impl_->device newBufferWithBytes:batch.packet_offsets.data()
                                                                length:batch.packet_offsets.size() * sizeof(uint32_t)
                                                               options:MTLResourceStorageModeShared];
      id<MTLBuffer> device_lengths = [impl_->device newBufferWithBytes:batch.packet_lengths.data()
                                                                length:batch.packet_lengths.size() * sizeof(uint16_t)
                                                               options:MTLResourceStorageModeShared];
      id<MTLBuffer> device_sender_ids = [impl_->device newBufferWithBytes:batch.sender_ids.data()
                                                                   length:batch.sender_ids.size() * sizeof(uint32_t)
                                                                  options:MTLResourceStorageModeShared];
      const uint32_t count_value = static_cast<uint32_t>(packet_count);
      id<MTLBuffer> device_count = [impl_->device newBufferWithBytes:&count_value
                                                              length:sizeof(count_value)
                                                             options:MTLResourceStorageModeShared];
      id<MTLBuffer> device_analyses = [impl_->device newBufferWithLength:packet_count * sizeof(DevicePacketAnalysis)
                                                                 options:MTLResourceStorageModeShared];
      const auto host_to_device_finished = std::chrono::steady_clock::now();

      const auto kernel_started = std::chrono::steady_clock::now();
      id<MTLCommandBuffer> command = [impl_->queue commandBuffer];
      id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
      [encoder setComputePipelineState:impl_->pipeline];
      [encoder setBuffer:device_bytes offset:0 atIndex:0];
      [encoder setBuffer:device_offsets offset:0 atIndex:1];
      [encoder setBuffer:device_lengths offset:0 atIndex:2];
      [encoder setBuffer:device_sender_ids offset:0 atIndex:3];
      [encoder setBuffer:device_analyses offset:0 atIndex:4];
      [encoder setBuffer:device_count offset:0 atIndex:5];
      const NSUInteger threads_per_group = METAL_THREADS_PER_GROUP;
      const NSUInteger threadgroups = (packet_count + threads_per_group - 1) / threads_per_group;
      [encoder dispatchThreadgroups:MTLSizeMake(threadgroups, 1, 1)
              threadsPerThreadgroup:MTLSizeMake(threads_per_group, 1, 1)];
      [encoder endEncoding];
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
        analyses.push_back(analysis);
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
}
