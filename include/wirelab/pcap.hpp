#ifndef PROJECT_PCAP_HPP_
#define PROJECT_PCAP_HPP_

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "wirelab/expected.hpp"
#include "wirelab/packet_analyzer.hpp"

namespace wirelab
{
  enum class PcapError
  {
    FileRead,
    FileWrite,
    TruncatedHeader,
    UnknownMagic,
    UnsupportedLinkType,
    TruncatedPacket,
    OversizePacket
  };

  [[nodiscard]] const char* to_string(PcapError error) noexcept;

  // libpcap refuses to allocate beyond this, so a larger length means the file
  // is corrupt rather than merely unusual.
  constexpr uint32_t PCAP_MAXIMUM_SNAPLEN = 262144;
  // LINKTYPE_ETHERNET. WireLab analyses Ethernet frames, so nothing else loads.
  constexpr uint32_t PCAP_LINKTYPE_ETHERNET = 1;

  struct PcapPacket
  {
    // Absolute capture time, normalised to nanoseconds regardless of whether
    // the file used microsecond or nanosecond resolution.
    uint64_t timestamp_ns = 0;
    // Offset of the frame within the owning capture's byte buffer.
    size_t offset = 0;
    size_t captured_length = 0;
    // Length on the wire, which exceeds captured_length for snapped captures.
    size_t original_length = 0;
  };

  // An in-memory capture. Classic libpcap and pcapng are both accepted; the
  // file is read once and packets are indexed as offsets into that single
  // buffer, so frame access stays zero-copy.
  class PcapCapture
  {
   public:
    [[nodiscard]] static expected<PcapCapture, PcapError> from_file(const std::string& path);
    [[nodiscard]] static expected<PcapCapture, PcapError> from_bytes(std::vector<uint8_t> bytes);

    [[nodiscard]] const std::vector<PcapPacket>& packets() const noexcept;
    [[nodiscard]] size_t packet_count() const noexcept;
    [[nodiscard]] uint32_t link_type() const noexcept;
    [[nodiscard]] uint32_t snaplen() const noexcept;
    // True when the source was pcapng rather than classic libpcap.
    [[nodiscard]] bool pcapng() const noexcept;
    [[nodiscard]] bool nanosecond_resolution() const noexcept;

    // Borrows the frame in place; the view is valid while the capture lives.
    [[nodiscard]] PacketView view(size_t index, uint32_t ingress_port = 0) const noexcept;
    [[nodiscard]] const uint8_t* frame(size_t index) const noexcept;

   private:
    [[nodiscard]] static expected<PcapCapture, PcapError> from_pcapng_bytes(std::vector<uint8_t> bytes);

    PcapCapture() = default;

    std::vector<uint8_t> bytes_;
    std::vector<PcapPacket> packets_;
    uint32_t link_type_ = PCAP_LINKTYPE_ETHERNET;
    uint32_t snaplen_ = PCAP_MAXIMUM_SNAPLEN;
    bool nanosecond_resolution_ = false;
    bool pcapng_ = false;
  };

  // Writes pcapng rather than classic pcap because only pcapng carries a
  // per-packet comment, which is what makes WireLab's verdict visible in
  // Wireshark without a sidecar file.
  class PcapNgWriter
  {
   public:
    [[nodiscard]] static expected<PcapNgWriter, PcapError>
    create(const std::string& path, std::string_view interface_name = "wirelab", uint32_t snaplen = PCAP_MAXIMUM_SNAPLEN);

    PcapNgWriter(const PcapNgWriter&) = delete;
    PcapNgWriter& operator=(const PcapNgWriter&) = delete;
    PcapNgWriter(PcapNgWriter&&) noexcept = default;
    PcapNgWriter& operator=(PcapNgWriter&&) noexcept = default;
    ~PcapNgWriter() = default;

    // An empty comment writes the packet with no options block.
    [[nodiscard]] expected<void, PcapError> write(
        uint64_t timestamp_ns,
        const uint8_t* bytes,
        size_t captured_length,
        size_t original_length,
        std::string_view comment);
    [[nodiscard]] expected<void, PcapError> flush();
    [[nodiscard]] uint64_t written_packets() const noexcept;

   private:
    PcapNgWriter() = default;

    std::ofstream stream_;
    uint32_t snaplen_ = PCAP_MAXIMUM_SNAPLEN;
    uint64_t written_packets_ = 0;
  };
}  // namespace wirelab

#endif
