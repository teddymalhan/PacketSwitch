/**
 * @file ethernet_frame.hpp
 * @brief Ethernet frame parsing and MAC address handling
 *
 * Provides classes for working with Layer 2 Ethernet frames and MAC addresses.
 */

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
  /**
   * @brief Size of a MAC address in bytes
   */
  constexpr size_t MAC_ADDRESS_SIZE = 6;

  /**
   * @brief Minimum Ethernet frame size (header only)
   */
  constexpr size_t ETHERNET_HEADER_SIZE = 14;

  /**
   * @brief Represents a MAC (Media Access Control) address
   *
   * A MAC address is a 6-byte hardware address used in Ethernet networking.
   * This class provides formatting, parsing, and comparison operations.
   *
   * Example:
   * @code
   * MacAddress mac({0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
   * std::cout << mac << std::endl;  // Prints: 00:11:22:33:44:55
   * @endcode
   */
  class MacAddress
  {
   private:
    std::array<uint8_t, MAC_ADDRESS_SIZE> bytes_;

   public:
    /**
     * @brief Default constructor - creates a zero MAC address
     */
    MacAddress() noexcept : bytes_{ 0, 0, 0, 0, 0, 0 }
    {
    }

    /**
     * @brief Construct from a byte array
     * @param bytes The 6 bytes of the MAC address
     */
    explicit MacAddress(const std::array<uint8_t, MAC_ADDRESS_SIZE>& bytes) noexcept : bytes_(bytes)
    {
    }

    /**
     * @brief Construct from a raw buffer
     * @param data Pointer to 6 bytes
     */
    explicit MacAddress(const uint8_t* data) noexcept;

    /**
     * @brief Parse a MAC address from string (e.g., "00:11:22:33:44:55")
     * @param str The string representation
     * @return MacAddress instance, or zero MAC if parsing fails
     */
    [[nodiscard]] static MacAddress from_string(std::string_view str) noexcept;

    /**
     * @brief Create a broadcast MAC address (ff:ff:ff:ff:ff:ff)
     * @return Broadcast MAC address
     */
    [[nodiscard]] static MacAddress broadcast() noexcept
    {
      return MacAddress({ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff });
    }

    /**
     * @brief Get the raw bytes of the MAC address
     * @return Const reference to the byte array
     */
    [[nodiscard]] const std::array<uint8_t, MAC_ADDRESS_SIZE>& bytes() const noexcept
    {
      return bytes_;
    }

    /**
     * @brief Get a pointer to the raw bytes
     * @return Const pointer to the first byte
     */
    [[nodiscard]] const uint8_t* data() const noexcept
    {
      return bytes_.data();
    }

    /**
     * @brief Convert to string representation (e.g., "00:11:22:33:44:55")
     * @return String representation
     */
    [[nodiscard]] std::string to_string() const;

    /**
     * @brief Check if this is a broadcast address
     * @return true if all bytes are 0xff
     */
    [[nodiscard]] bool is_broadcast() const noexcept;

    /**
     * @brief Check if this is a zero address
     * @return true if all bytes are 0x00
     */
    [[nodiscard]] bool is_zero() const noexcept;

    /**
     * @brief Equality comparison
     */
    bool operator==(const MacAddress& other) const noexcept
    {
      return bytes_ == other.bytes_;
    }

    /**
     * @brief Inequality comparison
     */
    bool operator!=(const MacAddress& other) const noexcept
    {
      return bytes_ != other.bytes_;
    }

    /**
     * @brief Less-than comparison (for use in maps)
     */
    bool operator<(const MacAddress& other) const noexcept
    {
      return bytes_ < other.bytes_;
    }
  };

  /**
   * @brief Output stream operator for MacAddress
   */
  std::ostream& operator<<(std::ostream& os, const MacAddress& mac);

  /**
   * @brief Represents an Ethernet frame
   *
   * An Ethernet frame consists of:
   * - Destination MAC address (6 bytes)
   * - Source MAC address (6 bytes)
   * - EtherType (2 bytes)
   * - Payload (variable length)
   *
   * This class provides parsing and construction of Ethernet frames.
   */
  class EthernetFrame
  {
   private:
    MacAddress dst_mac_;
    MacAddress src_mac_;
    uint16_t ethertype_ = 0;
    std::vector<uint8_t> payload_;

   public:
    /**
     * @brief Default constructor
     */
    EthernetFrame() = default;

    /**
     * @brief Construct an Ethernet frame
     * @param dst_mac Destination MAC address
     * @param src_mac Source MAC address
     * @param ethertype EtherType field (e.g., 0x0800 for IPv4)
     * @param payload Frame payload data
     */
    EthernetFrame(MacAddress dst_mac, MacAddress src_mac, uint16_t ethertype, std::vector<uint8_t> payload = {});

    /**
     * @brief Parse an Ethernet frame from raw bytes
     * @param data The raw frame data (must be at least 14 bytes)
     * @return EthernetFrame instance, or default frame if parsing fails
     */
    [[nodiscard]] static EthernetFrame parse(const std::vector<uint8_t>& data);

    /**
     * @brief Parse an Ethernet frame from a raw buffer
     * @param data Pointer to raw frame data
     * @param size Size of the data buffer
     * @return EthernetFrame instance, or default frame if parsing fails
     */
    [[nodiscard]] static EthernetFrame parse(const uint8_t* data, size_t size);

    /**
     * @brief Serialize the frame to raw bytes
     * @return Vector containing the complete frame
     */
    [[nodiscard]] std::vector<uint8_t> serialize() const;

    /**
     * @brief Get the destination MAC address
     */
    [[nodiscard]] const MacAddress& dst_mac() const noexcept
    {
      return dst_mac_;
    }

    /**
     * @brief Get the source MAC address
     */
    [[nodiscard]] const MacAddress& src_mac() const noexcept
    {
      return src_mac_;
    }

    /**
     * @brief Get the EtherType field
     */
    [[nodiscard]] uint16_t ethertype() const noexcept
    {
      return ethertype_;
    }

    /**
     * @brief Get the payload data
     */
    [[nodiscard]] const std::vector<uint8_t>& payload() const noexcept
    {
      return payload_;
    }

    /**
     * @brief Get the total frame size (header + payload)
     */
    [[nodiscard]] size_t size() const noexcept
    {
      return ETHERNET_HEADER_SIZE + payload_.size();
    }

    /**
     * @brief Set the destination MAC address
     */
    void set_dst_mac(const MacAddress& mac) noexcept
    {
      dst_mac_ = mac;
    }

    /**
     * @brief Set the source MAC address
     */
    void set_src_mac(const MacAddress& mac) noexcept
    {
      src_mac_ = mac;
    }

    /**
     * @brief Set the EtherType field
     */
    void set_ethertype(uint16_t ethertype) noexcept
    {
      ethertype_ = ethertype;
    }

    /**
     * @brief Set the payload data
     */
    void set_payload(std::vector<uint8_t> payload)
    {
      payload_ = std::move(payload);
    }

    /**
     * @brief Check if this is a broadcast frame
     */
    [[nodiscard]] bool is_broadcast() const noexcept
    {
      return dst_mac_.is_broadcast();
    }
  };

  /**
   * @brief Common EtherType values
   */
  namespace EtherType
  {
    constexpr uint16_t IPv4 = 0x0800;
    constexpr uint16_t ARP = 0x0806;
    constexpr uint16_t IPv6 = 0x86DD;
  }  // namespace EtherType

}  // namespace project

// Hash function for MacAddress (to use in unordered_map)
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
}  // namespace std

#endif  // PROJECT_ETHERNET_FRAME_HPP_
