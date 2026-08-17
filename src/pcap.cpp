#include "wirelab/pcap.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace wirelab
{
  namespace
  {
    constexpr uint32_t PCAP_MAGIC_MICROSECONDS = 0xa1b2c3d4;
    constexpr uint32_t PCAP_MAGIC_NANOSECONDS = 0xa1b23c4d;
    constexpr uint32_t PCAP_MAGIC_MICROSECONDS_SWAPPED = 0xd4c3b2a1;
    constexpr uint32_t PCAP_MAGIC_NANOSECONDS_SWAPPED = 0x4d3cb2a1;

    constexpr size_t PCAP_FILE_HEADER_SIZE = 24;
    constexpr size_t PCAP_RECORD_HEADER_SIZE = 16;

    constexpr uint32_t PCAPNG_BLOCK_SECTION_HEADER = 0x0a0d0d0a;
    constexpr uint32_t PCAPNG_BLOCK_INTERFACE_DESCRIPTION = 0x00000001;
    constexpr uint32_t PCAPNG_BLOCK_ENHANCED_PACKET = 0x00000006;
    constexpr uint32_t PCAPNG_BLOCK_SIMPLE_PACKET = 0x00000003;
    constexpr uint32_t PCAPNG_BYTE_ORDER_MAGIC_SWAPPED = 0x4d3c2b1a;
    constexpr size_t PCAPNG_BLOCK_MINIMUM_SIZE = 12;
    constexpr uint8_t PCAPNG_DEFAULT_RESOLUTION = 6;
    constexpr uint32_t PCAPNG_BYTE_ORDER_MAGIC = 0x1a2b3c4d;

    constexpr uint16_t PCAPNG_OPTION_END = 0;
    constexpr uint16_t PCAPNG_OPTION_COMMENT = 1;
    constexpr uint16_t PCAPNG_OPTION_INTERFACE_NAME = 2;
    constexpr uint16_t PCAPNG_OPTION_USER_APPLICATION = 4;
    constexpr uint16_t PCAPNG_OPTION_TIMESTAMP_RESOLUTION = 9;

    // if_tsresol carries a negative power of ten; 9 means the timestamps below
    // are nanosecond counts, which matches PcapPacket::timestamp_ns exactly.
    constexpr uint8_t PCAPNG_NANOSECOND_RESOLUTION = 9;

    [[nodiscard]] uint32_t read_u32(const uint8_t* data, bool swapped) noexcept
    {
      const uint32_t value = static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 |
                             static_cast<uint32_t>(data[2]) << 16 | static_cast<uint32_t>(data[3]) << 24;
      if (!swapped)
      {
        return value;
      }
      return value >> 24 | (value >> 8 & 0x0000ff00U) | (value << 8 & 0x00ff0000U) | value << 24;
    }

    [[nodiscard]] uint16_t read_u16(const uint8_t* data, bool swapped) noexcept
    {
      const auto value = static_cast<uint16_t>(static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1]) << 8);
      return swapped ? static_cast<uint16_t>(value >> 8 | value << 8) : value;
    }

    // if_tsresol holds a negative power of ten, or a negative power of two when
    // the high bit is set. Absent the option, pcapng specifies microseconds.
    [[nodiscard]] uint8_t read_timestamp_resolution(const uint8_t* options, size_t length, bool swapped) noexcept
    {
      size_t offset = 0;
      while (offset + 4 <= length)
      {
        const uint16_t code = read_u16(options + offset, swapped);
        const uint16_t option_length = read_u16(options + offset + 2, swapped);
        if (code == PCAPNG_OPTION_END)
        {
          break;
        }
        if (code == PCAPNG_OPTION_TIMESTAMP_RESOLUTION && option_length >= 1 && offset + 4 + option_length <= length)
        {
          return options[offset + 4];
        }
        // Option bodies are padded to four bytes, so the next option starts at
        // the rounded-up length.
        offset += 4 + (option_length + 3) / 4 * 4;
      }
      return PCAPNG_DEFAULT_RESOLUTION;
    }

    [[nodiscard]] uint64_t ticks_to_nanoseconds(uint64_t ticks, uint8_t resolution) noexcept
    {
      if ((resolution & 0x80) != 0)
      {
        const uint8_t power = resolution & 0x7f;
        return power >= 64 ? 0 : ticks * 1'000'000'000ULL / (1ULL << power);
      }
      uint64_t nanoseconds = ticks;
      uint8_t decimals = resolution;
      // Scale up when the source is coarser than nanoseconds, down when finer,
      // so every capture lands on one comparable unit.
      for (; decimals < 9; ++decimals)
      {
        nanoseconds *= 10;
      }
      for (; decimals > 9; --decimals)
      {
        nanoseconds /= 10;
      }
      return nanoseconds;
    }

    void append_u16(std::vector<uint8_t>& buffer, uint16_t value)
    {
      buffer.push_back(static_cast<uint8_t>(value));
      buffer.push_back(static_cast<uint8_t>(value >> 8));
    }

    void append_u32(std::vector<uint8_t>& buffer, uint32_t value)
    {
      buffer.push_back(static_cast<uint8_t>(value));
      buffer.push_back(static_cast<uint8_t>(value >> 8));
      buffer.push_back(static_cast<uint8_t>(value >> 16));
      buffer.push_back(static_cast<uint8_t>(value >> 24));
    }

    void append_u64(std::vector<uint8_t>& buffer, uint64_t value)
    {
      append_u32(buffer, static_cast<uint32_t>(value));
      append_u32(buffer, static_cast<uint32_t>(value >> 32));
    }

    // Every pcapng block and option body is padded to a four byte boundary.
    void append_padding(std::vector<uint8_t>& buffer, size_t unpadded_length)
    {
      const size_t remainder = unpadded_length % 4;
      if (remainder != 0)
      {
        buffer.insert(buffer.end(), 4 - remainder, 0);
      }
    }

    void append_option(std::vector<uint8_t>& buffer, uint16_t code, const uint8_t* value, size_t length)
    {
      append_u16(buffer, code);
      append_u16(buffer, static_cast<uint16_t>(length));
      buffer.insert(buffer.end(), value, value + length);
      append_padding(buffer, length);
    }

    void append_option(std::vector<uint8_t>& buffer, uint16_t code, std::string_view value)
    {
      append_option(buffer, code, reinterpret_cast<const uint8_t*>(value.data()), value.size());
    }

    // Options are only emitted when at least one is present, so the terminator
    // is written by the caller that opened the option list.
    void append_option_end(std::vector<uint8_t>& buffer)
    {
      append_u16(buffer, PCAPNG_OPTION_END);
      append_u16(buffer, 0);
    }

    // A pcapng block repeats its total length in the trailer so readers can walk
    // the file backwards. The length is only known once the body is complete.
    void close_block(std::vector<uint8_t>& buffer, size_t block_start)
    {
      const auto total_length = static_cast<uint32_t>(buffer.size() - block_start + 4);
      buffer[block_start + 4] = static_cast<uint8_t>(total_length);
      buffer[block_start + 5] = static_cast<uint8_t>(total_length >> 8);
      buffer[block_start + 6] = static_cast<uint8_t>(total_length >> 16);
      buffer[block_start + 7] = static_cast<uint8_t>(total_length >> 24);
      append_u32(buffer, total_length);
    }
  }  // namespace

  const char* to_string(PcapError error) noexcept
  {
    switch (error)
    {
      case PcapError::FileRead: return "file could not be read";
      case PcapError::FileWrite: return "file could not be written";
      case PcapError::TruncatedHeader: return "file is shorter than a pcap header";
      case PcapError::UnknownMagic: return "not a classic pcap file";
      case PcapError::UnsupportedLinkType: return "capture link type is not Ethernet";
      case PcapError::TruncatedPacket: return "packet record runs past the end of the file";
      case PcapError::OversizePacket: return "packet record declares an implausible length";
    }
    return "unknown";
  }

  expected<PcapCapture, PcapError> PcapCapture::from_file(const std::string& path)
  {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
    {
      return unexpected(PcapError::FileRead);
    }

    const auto size = stream.tellg();
    if (size < 0)
    {
      return unexpected(PcapError::FileRead);
    }
    stream.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
      return unexpected(PcapError::FileRead);
    }
    return from_bytes(std::move(bytes));
  }

  expected<PcapCapture, PcapError> PcapCapture::from_bytes(std::vector<uint8_t> bytes)
  {
    if (bytes.size() < 4)
    {
      return unexpected(PcapError::TruncatedHeader);
    }
    if (read_u32(bytes.data(), false) == PCAPNG_BLOCK_SECTION_HEADER)
    {
      return from_pcapng_bytes(std::move(bytes));
    }
    if (bytes.size() < PCAP_FILE_HEADER_SIZE)
    {
      return unexpected(PcapError::TruncatedHeader);
    }

    const uint32_t magic = read_u32(bytes.data(), false);
    bool swapped = false;
    bool nanoseconds = false;
    switch (magic)
    {
      case PCAP_MAGIC_MICROSECONDS: break;
      case PCAP_MAGIC_NANOSECONDS: nanoseconds = true; break;
      case PCAP_MAGIC_MICROSECONDS_SWAPPED: swapped = true; break;
      case PCAP_MAGIC_NANOSECONDS_SWAPPED:
        swapped = true;
        nanoseconds = true;
        break;
      default: return unexpected(PcapError::UnknownMagic);
    }

    PcapCapture capture;
    capture.pcapng_ = false;
    capture.snaplen_ = read_u32(bytes.data() + 16, swapped);
    capture.link_type_ = read_u32(bytes.data() + 20, swapped);
    capture.nanosecond_resolution_ = nanoseconds;
    if (capture.link_type_ != PCAP_LINKTYPE_ETHERNET)
    {
      return unexpected(PcapError::UnsupportedLinkType);
    }

    size_t offset = PCAP_FILE_HEADER_SIZE;
    while (offset < bytes.size())
    {
      if (bytes.size() - offset < PCAP_RECORD_HEADER_SIZE)
      {
        return unexpected(PcapError::TruncatedPacket);
      }

      const uint32_t seconds = read_u32(bytes.data() + offset, swapped);
      const uint32_t fraction = read_u32(bytes.data() + offset + 4, swapped);
      const uint32_t captured_length = read_u32(bytes.data() + offset + 8, swapped);
      const uint32_t original_length = read_u32(bytes.data() + offset + 12, swapped);
      offset += PCAP_RECORD_HEADER_SIZE;

      // A corrupt length would otherwise drive an absurd read past the buffer.
      if (captured_length > PCAP_MAXIMUM_SNAPLEN)
      {
        return unexpected(PcapError::OversizePacket);
      }
      if (bytes.size() - offset < captured_length)
      {
        return unexpected(PcapError::TruncatedPacket);
      }

      capture.packets_.push_back(
          { static_cast<uint64_t>(seconds) * 1'000'000'000ULL + (nanoseconds ? fraction : fraction * 1'000ULL),
            offset,
            captured_length,
            original_length });
      offset += captured_length;
    }

    capture.bytes_ = std::move(bytes);
    return capture;
  }

  // pcapng is what Wireshark saves by default and what this project writes, so
  // a replay tool that could not read it would not be able to reread its own
  // output. Only the blocks that carry frames are interpreted; anything else is
  // skipped by its declared length.
  expected<PcapCapture, PcapError> PcapCapture::from_pcapng_bytes(std::vector<uint8_t> bytes)
  {
    PcapCapture capture;
    capture.pcapng_ = true;
    capture.snaplen_ = 0;

    // Timestamp resolution is a per-interface property, and a file may open a
    // new section that redefines every interface.
    std::vector<uint8_t> interface_resolutions;
    bool swapped = false;
    size_t offset = 0;

    while (offset < bytes.size())
    {
      if (bytes.size() - offset < PCAPNG_BLOCK_MINIMUM_SIZE)
      {
        return unexpected(PcapError::TruncatedPacket);
      }

      const uint32_t block_type = read_u32(bytes.data() + offset, swapped);
      if (block_type == PCAPNG_BLOCK_SECTION_HEADER)
      {
        if (bytes.size() - offset < 16)
        {
          return unexpected(PcapError::TruncatedHeader);
        }
        // The byte-order magic is written in the section's own order, so it
        // identifies that order without depending on the current setting.
        const uint32_t order = read_u32(bytes.data() + offset + 8, false);
        if (order == PCAPNG_BYTE_ORDER_MAGIC)
        {
          swapped = false;
        }
        else if (order == PCAPNG_BYTE_ORDER_MAGIC_SWAPPED)
        {
          swapped = true;
        }
        else
        {
          return unexpected(PcapError::UnknownMagic);
        }
        interface_resolutions.clear();
      }

      const uint32_t block_length = read_u32(bytes.data() + offset + 4, swapped);
      if (block_length < PCAPNG_BLOCK_MINIMUM_SIZE || block_length % 4 != 0 || block_length > bytes.size() - offset)
      {
        return unexpected(PcapError::TruncatedPacket);
      }

      const size_t body = offset + 8;
      const size_t body_length = block_length - PCAPNG_BLOCK_MINIMUM_SIZE;

      if (block_type == PCAPNG_BLOCK_INTERFACE_DESCRIPTION)
      {
        if (body_length < 8)
        {
          return unexpected(PcapError::TruncatedHeader);
        }
        const uint16_t link_type = read_u16(bytes.data() + body, swapped);
        if (link_type != PCAP_LINKTYPE_ETHERNET)
        {
          return unexpected(PcapError::UnsupportedLinkType);
        }
        capture.link_type_ = link_type;
        capture.snaplen_ = std::max(capture.snaplen_, read_u32(bytes.data() + body + 4, swapped));
        const uint8_t resolution = read_timestamp_resolution(bytes.data() + body + 8, body_length - 8, swapped);
        interface_resolutions.push_back(resolution);
        // Matches the classic reader's flag: true when the source recorded finer
        // than microsecond ticks.
        capture.nanosecond_resolution_ =
            capture.nanosecond_resolution_ || ((resolution & 0x80) == 0 && resolution > PCAPNG_DEFAULT_RESOLUTION);
      }
      else if (block_type == PCAPNG_BLOCK_ENHANCED_PACKET)
      {
        if (body_length < 20)
        {
          return unexpected(PcapError::TruncatedHeader);
        }
        const uint32_t interface_id = read_u32(bytes.data() + body, swapped);
        const uint64_t ticks = static_cast<uint64_t>(read_u32(bytes.data() + body + 4, swapped)) << 32 |
                               read_u32(bytes.data() + body + 8, swapped);
        const uint32_t captured_length = read_u32(bytes.data() + body + 12, swapped);
        const uint32_t original_length = read_u32(bytes.data() + body + 16, swapped);
        if (captured_length > PCAP_MAXIMUM_SNAPLEN)
        {
          return unexpected(PcapError::OversizePacket);
        }
        if (body_length - 20 < captured_length || interface_id >= interface_resolutions.size())
        {
          return unexpected(PcapError::TruncatedPacket);
        }
        capture.packets_.push_back(
            { ticks_to_nanoseconds(ticks, interface_resolutions[interface_id]),
              body + 20,
              captured_length,
              original_length });
      }
      else if (block_type == PCAPNG_BLOCK_SIMPLE_PACKET)
      {
        // A simple packet block carries no timestamp and no interface, so it
        // borrows interface zero and lands at time zero.
        if (body_length < 4 || interface_resolutions.empty())
        {
          return unexpected(PcapError::TruncatedPacket);
        }
        const uint32_t original_length = read_u32(bytes.data() + body, swapped);
        const uint32_t captured_length =
            capture.snaplen_ == 0 ? original_length : std::min(original_length, capture.snaplen_);
        if (captured_length > PCAP_MAXIMUM_SNAPLEN)
        {
          return unexpected(PcapError::OversizePacket);
        }
        if (body_length - 4 < captured_length)
        {
          return unexpected(PcapError::TruncatedPacket);
        }
        capture.packets_.push_back({ 0, body + 4, captured_length, original_length });
      }

      // The trailing length must agree with the header, which is what lets a
      // reader trust the walk instead of rescanning for the next block.
      if (read_u32(bytes.data() + offset + block_length - 4, swapped) != block_length)
      {
        return unexpected(PcapError::TruncatedPacket);
      }
      offset += block_length;
    }

    // A section with no interface option leaves the default in place.
    if (capture.snaplen_ == 0)
    {
      capture.snaplen_ = PCAP_MAXIMUM_SNAPLEN;
    }
    capture.bytes_ = std::move(bytes);
    return capture;
  }

  const std::vector<PcapPacket>& PcapCapture::packets() const noexcept
  {
    return packets_;
  }

  size_t PcapCapture::packet_count() const noexcept
  {
    return packets_.size();
  }

  uint32_t PcapCapture::link_type() const noexcept
  {
    return link_type_;
  }

  uint32_t PcapCapture::snaplen() const noexcept
  {
    return snaplen_;
  }

  bool PcapCapture::pcapng() const noexcept
  {
    return pcapng_;
  }

  bool PcapCapture::nanosecond_resolution() const noexcept
  {
    return nanosecond_resolution_;
  }

  const uint8_t* PcapCapture::frame(size_t index) const noexcept
  {
    return index < packets_.size() ? bytes_.data() + packets_[index].offset : nullptr;
  }

  PacketView PcapCapture::view(size_t index, uint32_t ingress_port) const noexcept
  {
    if (index >= packets_.size())
    {
      return {};
    }
    return { bytes_.data() + packets_[index].offset, packets_[index].captured_length, ingress_port };
  }

  expected<PcapNgWriter, PcapError>
  PcapNgWriter::create(const std::string& path, std::string_view interface_name, uint32_t snaplen)
  {
    PcapNgWriter writer;
    writer.stream_.open(path, std::ios::binary | std::ios::trunc);
    if (!writer.stream_)
    {
      return unexpected(PcapError::FileWrite);
    }
    writer.snaplen_ = snaplen;

    std::vector<uint8_t> header;
    const size_t section_start = header.size();
    append_u32(header, PCAPNG_BLOCK_SECTION_HEADER);
    append_u32(header, 0);  // Patched by close_block.
    append_u32(header, PCAPNG_BYTE_ORDER_MAGIC);
    append_u16(header, 1);  // Major version.
    append_u16(header, 0);  // Minor version.
    // A section length of -1 means "unspecified", which is correct for a stream
    // whose final size is unknown while packets are still being written.
    append_u64(header, 0xffffffffffffffffULL);
    append_option(header, PCAPNG_OPTION_USER_APPLICATION, "WireLab");
    append_option_end(header);
    close_block(header, section_start);

    const size_t interface_start = header.size();
    append_u32(header, PCAPNG_BLOCK_INTERFACE_DESCRIPTION);
    append_u32(header, 0);  // Patched by close_block.
    append_u16(header, static_cast<uint16_t>(PCAP_LINKTYPE_ETHERNET));
    append_u16(header, 0);  // Reserved.
    append_u32(header, snaplen);
    append_option(header, PCAPNG_OPTION_INTERFACE_NAME, interface_name);
    const std::array<uint8_t, 1> resolution{ PCAPNG_NANOSECOND_RESOLUTION };
    append_option(header, PCAPNG_OPTION_TIMESTAMP_RESOLUTION, resolution.data(), resolution.size());
    append_option_end(header);
    close_block(header, interface_start);

    writer.stream_.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (!writer.stream_)
    {
      return unexpected(PcapError::FileWrite);
    }
    return writer;
  }

  expected<void, PcapError> PcapNgWriter::write(
      uint64_t timestamp_ns,
      const uint8_t* bytes,
      size_t captured_length,
      size_t original_length,
      std::string_view comment)
  {
    if (bytes == nullptr && captured_length != 0)
    {
      return unexpected(PcapError::FileWrite);
    }
    if (captured_length > PCAP_MAXIMUM_SNAPLEN)
    {
      return unexpected(PcapError::OversizePacket);
    }

    std::vector<uint8_t> block;
    block.reserve(32 + captured_length + comment.size());
    const size_t block_start = block.size();
    append_u32(block, PCAPNG_BLOCK_ENHANCED_PACKET);
    append_u32(block, 0);  // Patched by close_block.
    append_u32(block, 0);  // Single interface.
    append_u32(block, static_cast<uint32_t>(timestamp_ns >> 32));
    append_u32(block, static_cast<uint32_t>(timestamp_ns));
    append_u32(block, static_cast<uint32_t>(captured_length));
    // Wireshark reports a malformed block when the wire length is under the
    // captured length, which happens in files that snapped without updating it.
    append_u32(block, static_cast<uint32_t>(std::max(original_length, captured_length)));
    block.insert(block.end(), bytes, bytes + captured_length);
    append_padding(block, captured_length);
    if (!comment.empty())
    {
      append_option(block, PCAPNG_OPTION_COMMENT, comment);
      append_option_end(block);
    }
    close_block(block, block_start);

    stream_.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(block.size()));
    if (!stream_)
    {
      return unexpected(PcapError::FileWrite);
    }
    ++written_packets_;
    return {};
  }

  expected<void, PcapError> PcapNgWriter::flush()
  {
    stream_.flush();
    if (!stream_)
    {
      return unexpected(PcapError::FileWrite);
    }
    return {};
  }

  uint64_t PcapNgWriter::written_packets() const noexcept
  {
    return written_packets_;
  }
}  // namespace wirelab
