// WireLab Metal packet parser kernel.
//
// Port of the CUDA `parse_packets` kernel (src/cuda_packet_parser.cu) to MSL.
// One thread per packet; per-packet parse is independent and embarrassingly
// parallel. The host-side aggregator still owns ordered MAC learning,
// histograms, traffic matrices, and flow ranking.

#include <metal_stdlib>
using namespace metal;

constant uint MAC_ADDRESS_SIZE = 6u;
constant uint ETHERNET_HEADER_SIZE = 14u;
constant uint IPV4_MINIMUM_HEADER_SIZE = 20u;
constant uint UDP_HEADER_SIZE = 8u;
constant uint TCP_MINIMUM_HEADER_SIZE = 20u;
constant uint ICMP_MINIMUM_HEADER_SIZE = 4u;
constant uint IPV4_ETHERTYPE = 0x0800u;
constant uchar IPV4_VERSION = 4u;
constant uchar TCP_PROTOCOL = 6u;
constant uchar UDP_PROTOCOL = 17u;
constant uchar ICMP_PROTOCOL = 1u;
constant ulong FNV1A_OFFSET_BASIS = 14695981039346656037ULL;
constant ulong FNV1A_PRIME = 1099511628211ULL;

// wirelab::PacketValidity values: Valid == 0, MalformedEthernet == 1,
// MalformedIpv4 == 2, MalformedTransport == 3.
constant uchar VALIDITY_VALID = 0u;
constant uchar VALIDITY_MALFORMED_ETHERNET = 1u;
constant uchar VALIDITY_MALFORMED_IPV4 = 2u;
constant uchar VALIDITY_MALFORMED_TRANSPORT = 3u;

// Layout must match wirelab::DevicePacketAnalysis in metal_packet_parser.mm
// (static_asserts there pin size 48 and the flow_hash offset 40).
struct DevicePacketAnalysis
{
  uchar source_mac[6];
  uchar destination_mac[6];
  uint source_ipv4;
  uint destination_ipv4;
  uint ingress_port;
  ushort source_port;
  ushort destination_port;
  ushort ethertype;
  ushort frame_length;
  uchar protocol;
  uchar tcp_flags;
  uchar validity;
  ulong flow_hash;
};

ushort read_network_u16(const device uchar* bytes)
{
  return static_cast<ushort>((static_cast<ushort>(bytes[0]) << 8u) | static_cast<ushort>(bytes[1]));
}

uint read_network_u32(const device uchar* bytes)
{
  return (static_cast<uint>(bytes[0]) << 24u) | (static_cast<uint>(bytes[1]) << 16u) |
         (static_cast<uint>(bytes[2]) << 8u) | static_cast<uint>(bytes[3]);
}

void hash_byte(thread ulong& hash, uchar byte)
{
  hash ^= static_cast<ulong>(byte);
  hash *= FNV1A_PRIME;
}

void hash_u16(thread ulong& hash, ushort value)
{
  hash_byte(hash, static_cast<uchar>(value >> 8u));
  hash_byte(hash, static_cast<uchar>(value));
}

void hash_u32(thread ulong& hash, uint value)
{
  hash_byte(hash, static_cast<uchar>(value >> 24u));
  hash_byte(hash, static_cast<uchar>(value >> 16u));
  hash_byte(hash, static_cast<uchar>(value >> 8u));
  hash_byte(hash, static_cast<uchar>(value));
}

ulong hash_flow_key(const thread DevicePacketAnalysis& analysis)
{
  ulong hash = FNV1A_OFFSET_BASIS;
  hash_u32(hash, analysis.source_ipv4);
  hash_u32(hash, analysis.destination_ipv4);
  hash_u16(hash, analysis.source_port);
  hash_u16(hash, analysis.destination_port);
  hash_byte(hash, analysis.protocol);
  return hash;
}

kernel void parse_packets(device const uchar* packet_bytes [[buffer(0)]],
                          device const uint* packet_offsets [[buffer(1)]],
                          device const ushort* packet_lengths [[buffer(2)]],
                          device const uint* sender_ids [[buffer(3)]],
                          device DevicePacketAnalysis* output [[buffer(4)]],
                          constant uint* packet_count [[buffer(5)]],
                          uint index [[thread_position_in_grid]])
{
  if (index >= *packet_count)
  {
    return;
  }

  const device uchar* packet = packet_bytes + packet_offsets[index];
  const uint packet_size = static_cast<uint>(packet_lengths[index]);
  DevicePacketAnalysis analysis{};
  analysis.ingress_port = sender_ids[index];
  analysis.frame_length = static_cast<ushort>(packet_size);
  analysis.validity = VALIDITY_MALFORMED_ETHERNET;
  if (packet_size < ETHERNET_HEADER_SIZE)
  {
    output[index] = analysis;
    return;
  }

  for (uint mac_index = 0; mac_index < MAC_ADDRESS_SIZE; ++mac_index)
  {
    analysis.destination_mac[mac_index] = packet[mac_index];
    analysis.source_mac[mac_index] = packet[MAC_ADDRESS_SIZE + mac_index];
  }
  analysis.ethertype = read_network_u16(packet + MAC_ADDRESS_SIZE * 2u);
  analysis.validity = VALIDITY_VALID;
  if (analysis.ethertype != IPV4_ETHERTYPE)
  {
    output[index] = analysis;
    return;
  }

  const uint payload_size = packet_size - ETHERNET_HEADER_SIZE;
  if (payload_size < IPV4_MINIMUM_HEADER_SIZE)
  {
    analysis.validity = VALIDITY_MALFORMED_IPV4;
    output[index] = analysis;
    return;
  }

  const device uchar* ipv4 = packet + ETHERNET_HEADER_SIZE;
  const uchar version = static_cast<uchar>(ipv4[0] >> 4u);
  const uint header_size = static_cast<uint>(ipv4[0] & 0x0fu) * 4u;
  const uint total_size = static_cast<uint>(read_network_u16(ipv4 + 2u));
  if (version != IPV4_VERSION || header_size < IPV4_MINIMUM_HEADER_SIZE || header_size > payload_size ||
      total_size < header_size || total_size > payload_size)
  {
    analysis.validity = VALIDITY_MALFORMED_IPV4;
    output[index] = analysis;
    return;
  }

  analysis.source_ipv4 = read_network_u32(ipv4 + 12u);
  analysis.destination_ipv4 = read_network_u32(ipv4 + 16u);
  analysis.protocol = ipv4[9];
  if ((read_network_u16(ipv4 + 6u) & 0x1fffu) != 0u)
  {
    analysis.flow_hash = hash_flow_key(analysis);
    output[index] = analysis;
    return;
  }

  const uint transport_size = total_size - header_size;
  const device uchar* transport = ipv4 + header_size;
  if (analysis.protocol == UDP_PROTOCOL)
  {
    if (transport_size < UDP_HEADER_SIZE || read_network_u16(transport + 4u) < UDP_HEADER_SIZE ||
        read_network_u16(transport + 4u) > transport_size)
    {
      analysis.validity = VALIDITY_MALFORMED_TRANSPORT;
    }
    else
    {
      analysis.source_port = read_network_u16(transport);
      analysis.destination_port = read_network_u16(transport + 2u);
    }
  }
  else if (analysis.protocol == TCP_PROTOCOL)
  {
    if (transport_size < TCP_MINIMUM_HEADER_SIZE)
    {
      analysis.validity = VALIDITY_MALFORMED_TRANSPORT;
    }
    else
    {
      const uint tcp_header_size = static_cast<uint>(transport[12] >> 4u) * 4u;
      if (tcp_header_size < TCP_MINIMUM_HEADER_SIZE || tcp_header_size > transport_size)
      {
        analysis.validity = VALIDITY_MALFORMED_TRANSPORT;
      }
      else
      {
        analysis.source_port = read_network_u16(transport);
        analysis.destination_port = read_network_u16(transport + 2u);
        analysis.tcp_flags = transport[13];
      }
    }
  }
  else if (analysis.protocol == ICMP_PROTOCOL && transport_size < ICMP_MINIMUM_HEADER_SIZE)
  {
    analysis.validity = VALIDITY_MALFORMED_TRANSPORT;
  }
  if (analysis.validity == VALIDITY_VALID)
  {
    analysis.flow_hash = hash_flow_key(analysis);
  }
  output[index] = analysis;
}
