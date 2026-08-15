#ifndef PROJECT_ETHERNET_FRAME_HPP_
#define PROJECT_ETHERNET_FRAME_HPP_

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace project
{
  constexpr size_t MAC_ADDRESS_SIZE = 6;
  constexpr size_t ETHERNET_HEADER_SIZE = 14;
  class MacAddress
  {
   private:
    std::array<uint8_t, MAC_ADDRESS_SIZE> bytes_;

   public:
    MacAddress() noexcept : bytes_{ 0, 0, 0, 0, 0, 0 }
    {
    }
    explicit MacAddress(const std::array<uint8_t, MAC_ADDRESS_SIZE>& bytes) noexcept : bytes_(bytes)
    {
    }
    explicit MacAddress(const uint8_t* data) noexcept;
    [[nodiscard]] static MacAddress from_string(std::string_view str) noexcept;
    [[nodiscard]] static MacAddress broadcast() noexcept
    {
      return MacAddress({ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff });
    }
    [[nodiscard]] const std::array<uint8_t, MAC_ADDRESS_SIZE>& bytes() const noexcept
    {
      return bytes_;
    }
    [[nodiscard]] const uint8_t* data() const noexcept
    {
      return bytes_.data();
    }
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] bool is_broadcast() const noexcept;
    [[nodiscard]] bool is_zero() const noexcept;
    bool operator==(const MacAddress& other) const noexcept
    {
      return bytes_ == other.bytes_;
    }
    bool operator!=(const MacAddress& other) const noexcept
    {
      return bytes_ != other.bytes_;
    }
    bool operator<(const MacAddress& other) const noexcept
    {
      return bytes_ < other.bytes_;
    }
  };
  std::ostream& operator<<(std::ostream& os, const MacAddress& mac);

  class EthernetFrame
  {
   private:
    MacAddress dst_mac_;
    MacAddress src_mac_;
    uint16_t ethertype_ = 0;
    std::vector<uint8_t> payload_;

   public:
    EthernetFrame() = default;
    EthernetFrame(MacAddress dst_mac, MacAddress src_mac, uint16_t ethertype, std::vector<uint8_t> payload = {});
    [[nodiscard]] static EthernetFrame parse(const std::vector<uint8_t>& data);
    [[nodiscard]] static EthernetFrame parse(const uint8_t* data, size_t size);
    [[nodiscard]] std::vector<uint8_t> serialize() const;
    [[nodiscard]] const MacAddress& dst_mac() const noexcept
    {
      return dst_mac_;
    }
    [[nodiscard]] const MacAddress& src_mac() const noexcept
    {
      return src_mac_;
    }
    [[nodiscard]] uint16_t ethertype() const noexcept
    {
      return ethertype_;
    }
    [[nodiscard]] const std::vector<uint8_t>& payload() const noexcept
    {
      return payload_;
    }
    [[nodiscard]] size_t size() const noexcept
    {
      return ETHERNET_HEADER_SIZE + payload_.size();
    }
    void set_dst_mac(const MacAddress& mac) noexcept
    {
      dst_mac_ = mac;
    }
    void set_src_mac(const MacAddress& mac) noexcept
    {
      src_mac_ = mac;
    }
    void set_ethertype(uint16_t ethertype) noexcept
    {
      ethertype_ = ethertype;
    }
    void set_payload(std::vector<uint8_t> payload)
    {
      payload_ = std::move(payload);
    }

    [[nodiscard]] bool is_broadcast() const noexcept
    {
      return dst_mac_.is_broadcast();
    }
  };

  namespace EtherType
  {
    constexpr uint16_t IPv4 = 0x0800;
    constexpr uint16_t ARP = 0x0806;
    constexpr uint16_t IPv6 = 0x86DD;
  }  

}  


namespace std
{
  template<>
  struct hash<project::MacAddress>
  {
    size_t operator()(const project::MacAddress& mac) const noexcept
    {
      const auto& bytes = mac.bytes();
      size_t hash = 0;
      for (size_t i = 0; i < project::MAC_ADDRESS_SIZE; ++i)
      {
        hash ^= static_cast<size_t>(bytes[i]) << (i * 8);
      }
      return hash;
    }
  };
}  

#endif  
