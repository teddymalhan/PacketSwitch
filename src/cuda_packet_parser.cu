#include "project/cuda_packet_parser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <cuda_runtime.h>

namespace project
{
  namespace
  {
    constexpr uint8_t IPV4_VERSION = 4;
    constexpr uint8_t TCP_PROTOCOL = 6;
    constexpr uint8_t UDP_PROTOCOL = 17;
    constexpr uint8_t ICMP_PROTOCOL = 1;
    constexpr size_t IPV4_MINIMUM_HEADER_SIZE = 20;
    constexpr size_t UDP_HEADER_SIZE = 8;
    constexpr size_t TCP_MINIMUM_HEADER_SIZE = 20;
    constexpr size_t ICMP_MINIMUM_HEADER_SIZE = 4;
    constexpr int CUDA_BLOCK_SIZE = 256;

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

    [[noreturn]] void throw_cuda_error(cudaError_t error, const char* operation)
    {
      throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(error));
    }

    void check_cuda(cudaError_t error, const char* operation)
    {
      if (error != cudaSuccess)
      {
        throw_cuda_error(error, operation);
      }
    }

    template<typename T>
    class DeviceBuffer final
    {
     public:
      explicit DeviceBuffer(size_t count) : count_(count)
      {
        if (count_ != 0)
        {
          check_cuda(cudaMalloc(&data_, count_ * sizeof(T)), "cudaMalloc");
        }
      }

      ~DeviceBuffer()
      {
        if (data_ != nullptr)
        {
          cudaFree(data_);
        }
      }

      DeviceBuffer(const DeviceBuffer&) = delete;
      DeviceBuffer& operator=(const DeviceBuffer&) = delete;

      [[nodiscard]] T* data() noexcept { return data_; }
      [[nodiscard]] const T* data() const noexcept { return data_; }

     private:
      T* data_ = nullptr;
      size_t count_ = 0;
    };

    __device__ uint16_t read_network_u16(const uint8_t* bytes)
    {
      return static_cast<uint16_t>(static_cast<uint16_t>(bytes[0]) << 8U | bytes[1]);
    }

    __device__ uint32_t read_network_u32(const uint8_t* bytes)
    {
      return static_cast<uint32_t>(static_cast<uint32_t>(bytes[0]) << 24U |
                                   static_cast<uint32_t>(bytes[1]) << 16U |
                                   static_cast<uint32_t>(bytes[2]) << 8U | bytes[3]);
    }

    __device__ void hash_byte(uint64_t& hash, uint8_t byte)
    {
      constexpr uint64_t FNV1A_PRIME = 1099511628211ULL;
      hash ^= byte;
      hash *= FNV1A_PRIME;
    }

    __device__ void hash_u16(uint64_t& hash, uint16_t value)
    {
      hash_byte(hash, static_cast<uint8_t>(value >> 8U));
      hash_byte(hash, static_cast<uint8_t>(value));
    }

    __device__ void hash_u32(uint64_t& hash, uint32_t value)
    {
      hash_byte(hash, static_cast<uint8_t>(value >> 24U));
      hash_byte(hash, static_cast<uint8_t>(value >> 16U));
      hash_byte(hash, static_cast<uint8_t>(value >> 8U));
      hash_byte(hash, static_cast<uint8_t>(value));
    }

    __device__ uint64_t hash_flow_key(const DevicePacketAnalysis& analysis)
    {
      uint64_t hash = 14695981039346656037ULL;
      hash_u32(hash, analysis.source_ipv4);
      hash_u32(hash, analysis.destination_ipv4);
      hash_u16(hash, analysis.source_port);
      hash_u16(hash, analysis.destination_port);
      hash_byte(hash, analysis.protocol);
      return hash;
    }

    __global__ void parse_packets(const uint8_t* packet_bytes, const uint32_t* packet_offsets,
                                  const uint16_t* packet_lengths, const uint32_t* sender_ids,
                                  size_t packet_count, DevicePacketAnalysis* output)
    {
      for (size_t index = static_cast<size_t>(blockIdx.x) * blockDim.x + threadIdx.x; index < packet_count;
           index += static_cast<size_t>(blockDim.x) * gridDim.x)
      {
        const uint8_t* packet = packet_bytes + packet_offsets[index];
        const size_t packet_size = packet_lengths[index];
        DevicePacketAnalysis analysis{};
        analysis.ingress_port = sender_ids[index];
        analysis.frame_length = static_cast<uint16_t>(packet_size);
        if (packet_size < ETHERNET_HEADER_SIZE)
        {
          output[index] = analysis;
          continue;
        }

        for (size_t mac_index = 0; mac_index < MAC_ADDRESS_SIZE; ++mac_index)
        {
          analysis.destination_mac[mac_index] = packet[mac_index];
          analysis.source_mac[mac_index] = packet[MAC_ADDRESS_SIZE + mac_index];
        }
        analysis.ethertype = read_network_u16(packet + MAC_ADDRESS_SIZE * 2);
        analysis.validity = static_cast<uint8_t>(PacketValidity::Valid);
        if (analysis.ethertype != EtherType::IPv4)
        {
          output[index] = analysis;
          continue;
        }

        const size_t payload_size = packet_size - ETHERNET_HEADER_SIZE;
        if (payload_size < IPV4_MINIMUM_HEADER_SIZE)
        {
          analysis.validity = static_cast<uint8_t>(PacketValidity::MalformedIpv4);
          output[index] = analysis;
          continue;
        }

        const uint8_t* ipv4 = packet + ETHERNET_HEADER_SIZE;
        const uint8_t version = static_cast<uint8_t>(ipv4[0] >> 4U);
        const size_t header_size = static_cast<size_t>(ipv4[0] & 0x0fU) * 4U;
        const size_t total_size = read_network_u16(ipv4 + 2);
        if (version != IPV4_VERSION || header_size < IPV4_MINIMUM_HEADER_SIZE || header_size > payload_size ||
            total_size < header_size || total_size > payload_size)
        {
          analysis.validity = static_cast<uint8_t>(PacketValidity::MalformedIpv4);
          output[index] = analysis;
          continue;
        }

        analysis.source_ipv4 = read_network_u32(ipv4 + 12);
        analysis.destination_ipv4 = read_network_u32(ipv4 + 16);
        analysis.protocol = ipv4[9];
        if ((read_network_u16(ipv4 + 6) & 0x1fffU) != 0)
        {
          analysis.flow_hash = hash_flow_key(analysis);
          output[index] = analysis;
          continue;
        }

        const size_t transport_size = total_size - header_size;
        const uint8_t* transport = ipv4 + header_size;
        if (analysis.protocol == UDP_PROTOCOL)
        {
          if (transport_size < UDP_HEADER_SIZE || read_network_u16(transport + 4) < UDP_HEADER_SIZE ||
              read_network_u16(transport + 4) > transport_size)
          {
            analysis.validity = static_cast<uint8_t>(PacketValidity::MalformedTransport);
          }
          else
          {
            analysis.source_port = read_network_u16(transport);
            analysis.destination_port = read_network_u16(transport + 2);
          }
        }
        else if (analysis.protocol == TCP_PROTOCOL)
        {
          if (transport_size < TCP_MINIMUM_HEADER_SIZE)
          {
            analysis.validity = static_cast<uint8_t>(PacketValidity::MalformedTransport);
          }
          else
          {
            const size_t tcp_header_size = static_cast<size_t>(transport[12] >> 4U) * 4U;
            if (tcp_header_size < TCP_MINIMUM_HEADER_SIZE || tcp_header_size > transport_size)
            {
              analysis.validity = static_cast<uint8_t>(PacketValidity::MalformedTransport);
            }
            else
            {
              analysis.source_port = read_network_u16(transport);
              analysis.destination_port = read_network_u16(transport + 2);
              analysis.tcp_flags = transport[13];
            }
          }
        }
        else if (analysis.protocol == ICMP_PROTOCOL && transport_size < ICMP_MINIMUM_HEADER_SIZE)
        {
          analysis.validity = static_cast<uint8_t>(PacketValidity::MalformedTransport);
        }
        if (analysis.validity == static_cast<uint8_t>(PacketValidity::Valid))
        {
          analysis.flow_hash = hash_flow_key(analysis);
        }
        output[index] = analysis;
      }
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

  bool CudaPacketParser::is_available() noexcept
  {
    int device_count = 0;
    return cudaGetDeviceCount(&device_count) == cudaSuccess && device_count > 0;
  }

  std::vector<PacketAnalysis> CudaPacketParser::parse(const PacketBatch& batch) const
  {
    validate_batch(batch);
    if (!is_available())
    {
      throw std::runtime_error("no compatible CUDA device is available");
    }
    const size_t packet_count = batch.packet_count();
    if (packet_count == 0)
    {
      return {};
    }

    DeviceBuffer<uint8_t> device_bytes(batch.packet_bytes.size());
    DeviceBuffer<uint32_t> device_offsets(batch.packet_offsets.size());
    DeviceBuffer<uint16_t> device_lengths(batch.packet_lengths.size());
    DeviceBuffer<uint32_t> device_sender_ids(batch.sender_ids.size());
    DeviceBuffer<DevicePacketAnalysis> device_analyses(packet_count);
    check_cuda(cudaMemcpy(device_bytes.data(), batch.packet_bytes.data(), batch.packet_bytes.size(), cudaMemcpyHostToDevice),
               "cudaMemcpy packet bytes");
    check_cuda(cudaMemcpy(device_offsets.data(), batch.packet_offsets.data(),
                          batch.packet_offsets.size() * sizeof(uint32_t), cudaMemcpyHostToDevice),
               "cudaMemcpy packet offsets");
    check_cuda(cudaMemcpy(device_lengths.data(), batch.packet_lengths.data(),
                          batch.packet_lengths.size() * sizeof(uint16_t), cudaMemcpyHostToDevice),
               "cudaMemcpy packet lengths");
    check_cuda(cudaMemcpy(device_sender_ids.data(), batch.sender_ids.data(),
                          batch.sender_ids.size() * sizeof(uint32_t), cudaMemcpyHostToDevice),
               "cudaMemcpy sender ids");

    const size_t block_count = (packet_count + CUDA_BLOCK_SIZE - 1) / CUDA_BLOCK_SIZE;
    parse_packets<<<static_cast<unsigned int>(block_count), CUDA_BLOCK_SIZE>>>(
        device_bytes.data(), device_offsets.data(), device_lengths.data(), device_sender_ids.data(), packet_count,
        device_analyses.data());
    check_cuda(cudaGetLastError(), "packet parser kernel launch");
    check_cuda(cudaDeviceSynchronize(), "packet parser synchronization");

    std::vector<DevicePacketAnalysis> device_results(packet_count);
    check_cuda(cudaMemcpy(device_results.data(), device_analyses.data(), packet_count * sizeof(DevicePacketAnalysis),
                          cudaMemcpyDeviceToHost),
               "cudaMemcpy packet analyses");
    std::vector<PacketAnalysis> analyses;
    analyses.reserve(packet_count);
    for (const DevicePacketAnalysis& result : device_results)
    {
      PacketAnalysis analysis;
      std::array<uint8_t, MAC_ADDRESS_SIZE> source{};
      std::array<uint8_t, MAC_ADDRESS_SIZE> destination{};
      for (size_t index = 0; index < MAC_ADDRESS_SIZE; ++index)
      {
        source[index] = result.source_mac[index];
        destination[index] = result.destination_mac[index];
      }
      analysis.source_mac = MacAddress(source);
      analysis.destination_mac = MacAddress(destination);
      analysis.source_ipv4 = result.source_ipv4;
      analysis.destination_ipv4 = result.destination_ipv4;
      analysis.ingress_port = result.ingress_port;
      analysis.source_port = result.source_port;
      analysis.destination_port = result.destination_port;
      analysis.ethertype = result.ethertype;
      analysis.frame_length = result.frame_length;
      analysis.protocol = result.protocol;
      analysis.tcp_flags = result.tcp_flags;
      analysis.validity = static_cast<PacketValidity>(result.validity);
      analysis.flow_hash = result.flow_hash;
      analyses.push_back(analysis);
    }
    return analyses;
  }

  AnalysisBatch CudaPacketAnalyzer::analyze(const PacketView* packets, size_t packet_count)
  {
    const auto batch = PacketBatch::create(packets, packet_count);
    if (!batch)
    {
      throw std::invalid_argument(std::string("cannot create CUDA packet batch: ") + to_string(batch.error()));
    }
    return analyze(*batch);
  }

  AnalysisBatch CudaPacketAnalyzer::analyze(const PacketBatch& batch)
  {
    return aggregator_.aggregate(parser_.parse(batch));
  }

  void CudaPacketAnalyzer::reset() noexcept
  {
    aggregator_.reset();
  }
}
