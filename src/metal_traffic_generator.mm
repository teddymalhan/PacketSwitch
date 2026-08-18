// WireLab Metal traffic generator: batch frame synthesis on Apple GPUs.
// Host side only; the kernel is compiled at runtime from the embedded MSL
// source (see scripts/embed_metal.py and src/metal_traffic_kernel.metal).

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "wirelab/metal_traffic_generator.hpp"

#include "metal_traffic_kernel_source.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

#include "wirelab/ethernet_frame.hpp"

namespace wirelab
{
  namespace
  {
    constexpr size_t METAL_THREADS_PER_GROUP = 256;

    // Layout must match the TrafficKernelParameters struct in
    // src/metal_traffic_kernel.metal.
    struct TrafficKernelParameters
    {
      uint64_t seed = 0;
      uint64_t first_sequence = 0;
      uint32_t frame_size = 0;
      uint32_t host_count = 0;
      uint32_t scenario = 0;
      uint32_t frame_count = 0;
    };
    static_assert(sizeof(TrafficKernelParameters) == 32, "TrafficKernelParameters layout must match the MSL kernel");
    static_assert(
        offsetof(TrafficKernelParameters, frame_size) == 16,
        "TrafficKernelParameters frame_size offset must match the MSL kernel");

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
  }  // namespace

  struct MetalTrafficGenerator::Impl
  {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLComputePipelineState> pipeline = nil;
    // Kept between dispatches: a run generates the same batch size thousands of
    // times and must not pay for a new device allocation each time.
    id<MTLBuffer> frames = nil;
    NSUInteger frames_capacity = 0;

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
          throw_metal_error(@"Metal traffic kernel compilation failed", error);
        }
        id<MTLFunction> function = [library newFunctionWithName:@"generate_frames"];
        if (function == nil)
        {
          throw std::runtime_error("Metal kernel function 'generate_frames' was not found");
        }
        pipeline = [device newComputePipelineStateWithFunction:function error:&error];
        if (pipeline == nil)
        {
          throw_metal_error(@"Metal compute pipeline creation failed", error);
        }
      }
    }

    void reserve_frames(NSUInteger bytes)
    {
      if (frames_capacity >= bytes)
      {
        return;
      }
      frames = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
      if (frames == nil)
      {
        throw std::runtime_error("failed to allocate the Metal traffic frame buffer");
      }
      frames_capacity = bytes;
    }
  };

  MetalTrafficGenerator::MetalTrafficGenerator() = default;
  MetalTrafficGenerator::~MetalTrafficGenerator() = default;

  bool MetalTrafficGenerator::is_available() noexcept
  {
    @autoreleasepool
    {
      return MTLCreateSystemDefaultDevice() != nil;
    }
  }

  void MetalTrafficGenerator::generate(
      const TrafficGeneratorConfig& config,
      uint64_t first_sequence,
      size_t count,
      uint8_t* out) const
  {
    @autoreleasepool
    {
      if (config.frame_size < ETHERNET_HEADER_SIZE)
      {
        throw std::invalid_argument("frame_size must include a complete Ethernet header");
      }
      if (config.host_count < 2)
      {
        throw std::invalid_argument("host_count must be at least two");
      }
      if (count == 0)
      {
        return;
      }
      if (count > std::numeric_limits<uint32_t>::max())
      {
        throw std::invalid_argument("traffic batch exceeds the Metal dispatch limit");
      }
      if (out == nullptr)
      {
        throw std::invalid_argument("traffic batch output buffer is null");
      }
      if (!is_available())
      {
        throw std::runtime_error("no compatible Metal device is available");
      }
      if (impl_ == nullptr)
      {
        impl_ = std::make_unique<Impl>();
      }

      const size_t batch_bytes = count * config.frame_size;
      impl_->reserve_frames(static_cast<NSUInteger>(batch_bytes));

      TrafficKernelParameters parameters;
      parameters.seed = config.seed;
      parameters.first_sequence = first_sequence;
      parameters.frame_size = static_cast<uint32_t>(config.frame_size);
      parameters.host_count = config.host_count;
      parameters.scenario = static_cast<uint32_t>(config.scenario);
      parameters.frame_count = static_cast<uint32_t>(count);

      id<MTLCommandBuffer> command = [impl_->queue commandBuffer];
      id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
      [encoder setComputePipelineState:impl_->pipeline];
      [encoder setBuffer:impl_->frames offset:0 atIndex:0];
      [encoder setBytes:&parameters length:sizeof(parameters) atIndex:1];
      const NSUInteger threads_per_group = METAL_THREADS_PER_GROUP;
      const NSUInteger threadgroups = (count + threads_per_group - 1) / threads_per_group;
      [encoder dispatchThreadgroups:MTLSizeMake(threadgroups, 1, 1)
              threadsPerThreadgroup:MTLSizeMake(threads_per_group, 1, 1)];
      [encoder endEncoding];
      [command commit];
      [command waitUntilCompleted];
      if (command.error != nil)
      {
        throw_metal_error(@"Metal traffic generator kernel failed", command.error);
      }

      std::memcpy(out, impl_->frames.contents, batch_bytes);
    }
  }

  MetalTrafficSource::MetalTrafficSource(TrafficGeneratorConfig config) noexcept : config_(config)
  {
  }

  void MetalTrafficSource::fill(uint64_t first_sequence, size_t count, std::vector<std::vector<uint8_t>>& frames)
  {
    batch_.resize(count * config_.frame_size);
    generator_.generate(config_, first_sequence, count, batch_.data());
    frames.resize(count);
    for (size_t index = 0; index < count; ++index)
    {
      frames[index].resize(config_.frame_size);
      std::memcpy(frames[index].data(), batch_.data() + index * config_.frame_size, config_.frame_size);
    }
  }
}  // namespace wirelab
