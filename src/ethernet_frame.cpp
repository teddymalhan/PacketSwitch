#include "wirelab/ethernet_frame.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace wirelab
{
  MacAddress::MacAddress(const uint8_t* data) noexcept
  {
    std::memcpy(bytes_.data(), data, MAC_ADDRESS_SIZE);
  }

  MacAddress MacAddress::from_string(std::string_view str) noexcept
  {
    MacAddress mac;

    if (str.size() != 17)
    {
      return mac;
    }

    char delimiter = str[2];
    if (delimiter != ':' && delimiter != '-')
    {
      return mac;
    }

    try
    {
      for (size_t i = 0; i < MAC_ADDRESS_SIZE; ++i)
      {
        size_t pos = i * 3;
        if (i > 0 && str[pos - 1] != delimiter)
        {
          return MacAddress{};
        }

        std::string byte_str(str.substr(pos, 2));
        mac.bytes_[i] = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
      }
    }
    catch (...)
    {
      return MacAddress{};
    }

    return mac;
  }

  std::string MacAddress::to_string() const
  {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    for (size_t i = 0; i < MAC_ADDRESS_SIZE; ++i)
    {
      if (i > 0)
      {
        oss << ':';
      }
      oss << std::setw(2) << static_cast<int>(bytes_[i]);
    }

    return oss.str();
  }

  bool MacAddress::is_broadcast() const noexcept
  {
    return std::all_of(bytes_.begin(), bytes_.end(), [](uint8_t b) { return b == 0xff; });
  }

  bool MacAddress::is_zero() const noexcept
  {
    return std::all_of(bytes_.begin(), bytes_.end(), [](uint8_t b) { return b == 0x00; });
  }

  std::ostream& operator<<(std::ostream& os, const MacAddress& mac)
  {
    os << mac.to_string();
    return os;
  }

  EthernetFrame::EthernetFrame(MacAddress dst_mac, MacAddress src_mac, uint16_t ethertype, std::vector<uint8_t> payload)
      : dst_mac_(std::move(dst_mac)),
        src_mac_(std::move(src_mac)),
        ethertype_(ethertype),
        payload_(std::move(payload))
  {
  }

  expected<EthernetFrame, FrameParseError> EthernetFrame::try_parse(const std::vector<uint8_t>& data)
  {
    return try_parse(data.data(), data.size());
  }

  expected<EthernetFrame, FrameParseError> EthernetFrame::try_parse(const uint8_t* data, size_t size)
  {
    if (data == nullptr || size < ETHERNET_HEADER_SIZE)
    {
      return unexpected(FrameParseError::TooShort);
    }

    MacAddress dst_mac(data);
    MacAddress src_mac(data + MAC_ADDRESS_SIZE);
    const uint16_t ethertype = static_cast<uint16_t>((data[12] << 8) | data[13]);

    std::vector<uint8_t> payload;
    if (size > ETHERNET_HEADER_SIZE)
    {
      payload.assign(data + ETHERNET_HEADER_SIZE, data + size);
    }

    return EthernetFrame(dst_mac, src_mac, ethertype, std::move(payload));
  }

  EthernetFrame EthernetFrame::parse(const std::vector<uint8_t>& data)
  {
    return parse(data.data(), data.size());
  }

  EthernetFrame EthernetFrame::parse(const uint8_t* data, size_t size)
  {
    return try_parse(data, size).value_or(EthernetFrame{});
  }

  std::vector<uint8_t> EthernetFrame::serialize() const
  {
    std::vector<uint8_t> frame;
    frame.reserve(ETHERNET_HEADER_SIZE + payload_.size());

    const auto& dst_bytes = dst_mac_.bytes();
    frame.insert(frame.end(), dst_bytes.begin(), dst_bytes.end());

    const auto& src_bytes = src_mac_.bytes();
    frame.insert(frame.end(), src_bytes.begin(), src_bytes.end());

    frame.push_back(static_cast<uint8_t>((ethertype_ >> 8) & 0xff));
    frame.push_back(static_cast<uint8_t>(ethertype_ & 0xff));

    frame.insert(frame.end(), payload_.begin(), payload_.end());

    return frame;
  }

}  // namespace wirelab
