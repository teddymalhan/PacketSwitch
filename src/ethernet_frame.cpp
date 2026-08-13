/**
 * @file ethernet_frame.cpp
 * @brief Implementation of Ethernet frame parsing and MAC address handling
 */

#include "project/ethernet_frame.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace project
{
  // MacAddress implementation

  MacAddress::MacAddress(const uint8_t* data) noexcept
  {
    std::memcpy(bytes_.data(), data, MAC_ADDRESS_SIZE);
  }

  MacAddress MacAddress::from_string(std::string_view str) noexcept
  {
    MacAddress mac;

    // Expected format: "xx:xx:xx:xx:xx:xx" or "xx-xx-xx-xx-xx-xx"
    if (str.size() != 17)
    {
      return mac;  // Return zero MAC on invalid length
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
          return MacAddress{};  // Invalid format
        }

        // Parse two hex digits
        std::string byte_str(str.substr(pos, 2));
        mac.bytes_[i] = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
      }
    }
    catch (...)
    {
      return MacAddress{};  // Return zero MAC on parse error
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

  // EthernetFrame implementation

  EthernetFrame::EthernetFrame(MacAddress dst_mac, MacAddress src_mac, uint16_t ethertype, std::vector<uint8_t> payload)
      : dst_mac_(std::move(dst_mac)),
        src_mac_(std::move(src_mac)),
        ethertype_(ethertype),
        payload_(std::move(payload))
  {
  }

  EthernetFrame EthernetFrame::parse(const std::vector<uint8_t>& data)
  {
    return parse(data.data(), data.size());
  }

  EthernetFrame EthernetFrame::parse(const uint8_t* data, size_t size)
  {
    if (size < ETHERNET_HEADER_SIZE)
    {
      return EthernetFrame{};  // Invalid frame - too short
    }

    // Parse destination MAC (bytes 0-5)
    MacAddress dst_mac(data);

    // Parse source MAC (bytes 6-11)
    MacAddress src_mac(data + 6);

    // Parse EtherType (bytes 12-13, network byte order - big endian)
    uint16_t ethertype = static_cast<uint16_t>((data[12] << 8) | data[13]);

    // Extract payload (everything after the header)
    std::vector<uint8_t> payload;
    if (size > ETHERNET_HEADER_SIZE)
    {
      payload.assign(data + ETHERNET_HEADER_SIZE, data + size);
    }

    return EthernetFrame(dst_mac, src_mac, ethertype, std::move(payload));
  }

  std::vector<uint8_t> EthernetFrame::serialize() const
  {
    std::vector<uint8_t> frame;
    frame.reserve(ETHERNET_HEADER_SIZE + payload_.size());

    // Destination MAC (6 bytes)
    const auto& dst_bytes = dst_mac_.bytes();
    frame.insert(frame.end(), dst_bytes.begin(), dst_bytes.end());

    // Source MAC (6 bytes)
    const auto& src_bytes = src_mac_.bytes();
    frame.insert(frame.end(), src_bytes.begin(), src_bytes.end());

    // EtherType (2 bytes, network byte order - big endian)
    frame.push_back(static_cast<uint8_t>((ethertype_ >> 8) & 0xff));
    frame.push_back(static_cast<uint8_t>(ethertype_ & 0xff));

    // Payload
    frame.insert(frame.end(), payload_.begin(), payload_.end());

    return frame;
  }

}  // namespace project
