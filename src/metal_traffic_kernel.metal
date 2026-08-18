// WireLab Metal traffic generator kernel.
//
// One thread per frame: a frame is derived from (config, sequence) alone, so a
// whole batch is produced in a single dispatch with no ordering between
// threads. Every value here must match write_traffic_frame() in
// src/traffic_generator.cpp byte for byte; tests/src/traffic_source_test.cpp
// is what holds the two implementations together.

#include <metal_stdlib>
using namespace metal;

constant uint ETHERNET_HEADER_SIZE = 14u;
constant uint IPV4_MINIMUM_HEADER_SIZE = 20u;
constant uint UDP_HEADER_SIZE = 8u;
constant uint IPV4_MAXIMUM_TOTAL_SIZE = 65535u;
constant uchar UDP_PROTOCOL = 17u;
constant ulong SPLITMIX_GAMMA = 0x9E3779B97F4A7C15UL;

// wirelab::TrafficScenario values.
constant uint SCENARIO_KNOWN_UNICAST = 0u;
constant uint SCENARIO_BROADCAST = 1u;
constant uint SCENARIO_UNKNOWN_UNICAST = 2u;
constant uint SCENARIO_MIXED = 3u;
constant uint SCENARIO_UDP_FLOOD = 4u;
constant uint SCENARIO_PORT_SCAN = 5u;
constant uint SCENARIO_BROADCAST_STORM = 6u;

// Layout must match wirelab::TrafficKernelParameters in
// src/metal_traffic_generator.mm (static_asserts there pin the offsets).
struct TrafficKernelParameters
{
  ulong seed;
  ulong first_sequence;
  uint frame_size;
  uint host_count;
  uint scenario;
  uint frame_count;
};

ulong splitmix_next(thread ulong& state)
{
  state += SPLITMIX_GAMMA;
  ulong z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9UL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBUL;
  return z ^ (z >> 31);
}

void write_host_mac(device uchar* out, uint host)
{
  out[0] = 0x02u;
  out[1] = 0x57u;
  out[2] = 0x4cu;
  out[3] = static_cast<uchar>(host >> 16u);
  out[4] = static_cast<uchar>(host >> 8u);
  out[5] = static_cast<uchar>(host);
}

void write_u16(device uchar* out, ushort value)
{
  out[0] = static_cast<uchar>(value >> 8u);
  out[1] = static_cast<uchar>(value);
}

kernel void generate_frames(device uchar* frames [[buffer(0)]],
                            constant TrafficKernelParameters& parameters [[buffer(1)]],
                            uint index [[thread_position_in_grid]])
{
  if (index >= parameters.frame_count)
  {
    return;
  }

  const ulong sequence = parameters.first_sequence + static_cast<ulong>(index);
  ulong state = parameters.seed ^ (sequence * SPLITMIX_GAMMA);
  const ulong entropy = splitmix_next(state);

  uint scenario = parameters.scenario;
  if (scenario == SCENARIO_MIXED)
  {
    scenario = static_cast<uint>(sequence % 3ul);
  }

  device uchar* frame = frames + static_cast<ulong>(index) * static_cast<ulong>(parameters.frame_size);
  device uchar* payload = frame + ETHERNET_HEADER_SIZE;
  const uint payload_size = parameters.frame_size - ETHERNET_HEADER_SIZE;

  // The payload is random first and overwritten by whatever headers fit, so a
  // frame too small for a header still carries entropy rather than zeroes.
  uint offset = 0u;
  while (offset + 8u <= payload_size)
  {
    const ulong value = splitmix_next(state);
    for (uint byte = 0u; byte < 8u; ++byte)
    {
      payload[offset + byte] = static_cast<uchar>(value >> (8u * byte));
    }
    offset += 8u;
  }
  if (offset < payload_size)
  {
    const ulong value = splitmix_next(state);
    for (uint byte = 0u; offset + byte < payload_size; ++byte)
    {
      payload[offset + byte] = static_cast<uchar>(value >> (8u * byte));
    }
  }

  uint source_host = static_cast<uint>(sequence % static_cast<ulong>(parameters.host_count));
  uint destination_host = (source_host + 1u) % parameters.host_count;
  uchar protocol = 0u;
  ushort source_port = 0u;
  ushort destination_port = 0u;
  bool broadcast_destination = false;
  bool unknown_destination = false;

  if (scenario == SCENARIO_BROADCAST)
  {
    broadcast_destination = true;
  }
  else if (scenario == SCENARIO_UNKNOWN_UNICAST)
  {
    unknown_destination = true;
  }
  else if (scenario == SCENARIO_UDP_FLOOD)
  {
    source_host = 0u;
    destination_host = 1u;
    protocol = UDP_PROTOCOL;
    source_port = static_cast<ushort>(0xC000u | (static_cast<uint>(entropy) & 0x3FFFu));
    destination_port = 9000u;
  }
  else if (scenario == SCENARIO_PORT_SCAN)
  {
    source_host = 0u;
    destination_host = 1u;
    protocol = UDP_PROTOCOL;
    source_port = 44444u;
    destination_port = static_cast<ushort>(1ul + (sequence % 1024ul));
  }
  else if (scenario == SCENARIO_BROADCAST_STORM)
  {
    broadcast_destination = true;
    source_host = static_cast<uint>(sequence % static_cast<ulong>(min(parameters.host_count, 3u)));
    destination_host = 0xffffu;
    protocol = UDP_PROTOCOL;
    source_port = 137u;
    destination_port = 137u;
  }

  if (broadcast_destination)
  {
    for (uint byte = 0u; byte < 6u; ++byte)
    {
      frame[byte] = 0xffu;
    }
  }
  else if (unknown_destination)
  {
    frame[0] = 0x02u;
    frame[1] = 0xfeu;
    frame[2] = static_cast<uchar>(entropy >> 24u);
    frame[3] = static_cast<uchar>(entropy >> 16u);
    frame[4] = static_cast<uchar>(entropy >> 8u);
    frame[5] = static_cast<uchar>(entropy);
  }
  else
  {
    write_host_mac(frame, destination_host);
  }
  write_host_mac(frame + 6u, source_host);
  write_u16(frame + 12u, 0x0800u);

  if (payload_size < IPV4_MINIMUM_HEADER_SIZE || payload_size > IPV4_MAXIMUM_TOTAL_SIZE)
  {
    return;
  }
  // A frame with no room for the transport header carries none, and must not
  // claim a protocol whose header is not there.
  if (protocol == UDP_PROTOCOL && payload_size < IPV4_MINIMUM_HEADER_SIZE + UDP_HEADER_SIZE)
  {
    protocol = 0u;
  }

  payload[0] = 0x45u;
  payload[1] = 0u;
  write_u16(payload + 2u, static_cast<ushort>(payload_size));
  write_u16(payload + 4u, static_cast<ushort>(sequence));
  write_u16(payload + 6u, 0u);
  payload[8] = 64u;
  payload[9] = protocol;
  write_u16(payload + 10u, 0u);
  payload[12] = 10u;
  payload[13] = 0u;
  write_u16(payload + 14u, static_cast<ushort>(source_host));
  payload[16] = 10u;
  payload[17] = 0u;
  write_u16(payload + 18u, static_cast<ushort>(destination_host));

  uint sum = 0u;
  for (uint word = 0u; word < IPV4_MINIMUM_HEADER_SIZE; word += 2u)
  {
    sum += (static_cast<uint>(payload[word]) << 8u) | static_cast<uint>(payload[word + 1u]);
  }
  while ((sum >> 16u) != 0u)
  {
    sum = (sum & 0xffffu) + (sum >> 16u);
  }
  write_u16(payload + 10u, static_cast<ushort>(~sum));

  if (protocol != UDP_PROTOCOL)
  {
    return;
  }
  write_u16(payload + IPV4_MINIMUM_HEADER_SIZE, source_port);
  write_u16(payload + IPV4_MINIMUM_HEADER_SIZE + 2u, destination_port);
  write_u16(payload + IPV4_MINIMUM_HEADER_SIZE + 4u, static_cast<ushort>(payload_size - IPV4_MINIMUM_HEADER_SIZE));
  write_u16(payload + IPV4_MINIMUM_HEADER_SIZE + 6u, 0u);
}
