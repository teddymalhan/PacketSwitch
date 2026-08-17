#include "wirelab/pcap.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
  constexpr std::array<uint8_t, 14> ETHERNET_BROADCAST_HEADER = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02,
                                                                  0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00 };

  void append_u16(std::vector<uint8_t>& buffer, uint16_t value, bool swapped)
  {
    if (swapped)
    {
      buffer.push_back(static_cast<uint8_t>(value >> 8));
      buffer.push_back(static_cast<uint8_t>(value));
      return;
    }
    buffer.push_back(static_cast<uint8_t>(value));
    buffer.push_back(static_cast<uint8_t>(value >> 8));
  }

  void append_u32(std::vector<uint8_t>& buffer, uint32_t value, bool swapped)
  {
    if (swapped)
    {
      buffer.push_back(static_cast<uint8_t>(value >> 24));
      buffer.push_back(static_cast<uint8_t>(value >> 16));
      buffer.push_back(static_cast<uint8_t>(value >> 8));
      buffer.push_back(static_cast<uint8_t>(value));
      return;
    }
    buffer.push_back(static_cast<uint8_t>(value));
    buffer.push_back(static_cast<uint8_t>(value >> 8));
    buffer.push_back(static_cast<uint8_t>(value >> 16));
    buffer.push_back(static_cast<uint8_t>(value >> 24));
  }

  std::vector<uint8_t> make_header(uint32_t magic, bool swapped, uint32_t link_type = 1)
  {
    std::vector<uint8_t> bytes;
    // The magic is what declares the byte order, so it is always written in the
    // file's own order rather than swapped.
    bytes.push_back(static_cast<uint8_t>(magic));
    bytes.push_back(static_cast<uint8_t>(magic >> 8));
    bytes.push_back(static_cast<uint8_t>(magic >> 16));
    bytes.push_back(static_cast<uint8_t>(magic >> 24));
    append_u16(bytes, 2, swapped);
    append_u16(bytes, 4, swapped);
    append_u32(bytes, 0, swapped);
    append_u32(bytes, 0, swapped);
    append_u32(bytes, 65535, swapped);
    append_u32(bytes, link_type, swapped);
    return bytes;
  }

  void append_record(
      std::vector<uint8_t>& bytes,
      uint32_t seconds,
      uint32_t fraction,
      const std::vector<uint8_t>& payload,
      bool swapped,
      uint32_t original_length = 0)
  {
    append_u32(bytes, seconds, swapped);
    append_u32(bytes, fraction, swapped);
    append_u32(bytes, static_cast<uint32_t>(payload.size()), swapped);
    append_u32(bytes, original_length == 0 ? static_cast<uint32_t>(payload.size()) : original_length, swapped);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
  }

  // Builds a minimal pcapng section header plus one interface, matching the
  // layout the reader has to walk.
  std::vector<uint8_t> make_pcapng(uint8_t resolution, bool swapped, uint16_t link_type = 1)
  {
    std::vector<uint8_t> bytes;
    append_u32(bytes, 0x0a0d0d0a, swapped);
    append_u32(bytes, 28, swapped);
    // The byte-order magic is always stored in the section's own order.
    append_u32(bytes, 0x1a2b3c4d, swapped);
    append_u16(bytes, 1, swapped);
    append_u16(bytes, 0, swapped);
    append_u32(bytes, 0xffffffff, swapped);
    append_u32(bytes, 0xffffffff, swapped);
    append_u32(bytes, 28, swapped);

    append_u32(bytes, 0x00000001, swapped);
    // 8 byte header, 8 byte body, 12 bytes of options, 4 byte trailer.
    append_u32(bytes, 32, swapped);
    append_u16(bytes, link_type, swapped);
    append_u16(bytes, 0, swapped);
    append_u32(bytes, 65535, swapped);
    append_u16(bytes, 9, swapped);  // if_tsresol
    append_u16(bytes, 1, swapped);
    bytes.push_back(resolution);
    bytes.insert(bytes.end(), 3, 0);  // Option bodies pad to four bytes.
    append_u16(bytes, 0, swapped);    // opt_endofopt
    append_u16(bytes, 0, swapped);
    append_u32(bytes, 32, swapped);
    return bytes;
  }

  void append_pcapng_packet(
      std::vector<uint8_t>& bytes,
      uint64_t ticks,
      const std::vector<uint8_t>& payload,
      bool swapped,
      uint32_t interface_id = 0)
  {
    const size_t padded = (payload.size() + 3) / 4 * 4;
    const auto block_length = static_cast<uint32_t>(32 + padded);
    append_u32(bytes, 0x00000006, swapped);
    append_u32(bytes, block_length, swapped);
    append_u32(bytes, interface_id, swapped);
    append_u32(bytes, static_cast<uint32_t>(ticks >> 32), swapped);
    append_u32(bytes, static_cast<uint32_t>(ticks), swapped);
    append_u32(bytes, static_cast<uint32_t>(payload.size()), swapped);
    append_u32(bytes, static_cast<uint32_t>(payload.size()), swapped);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    bytes.insert(bytes.end(), padded - payload.size(), 0);
    append_u32(bytes, block_length, swapped);
  }

  std::vector<uint8_t> make_frame(uint8_t marker, size_t size = 60)
  {
    std::vector<uint8_t> frame(ETHERNET_BROADCAST_HEADER.begin(), ETHERNET_BROADCAST_HEADER.end());
    frame.resize(size, marker);
    return frame;
  }

  std::vector<uint8_t> read_file(const std::filesystem::path& path)
  {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    const auto size = stream.tellg();
    stream.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
  }

  uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t offset)
  {
    return static_cast<uint32_t>(bytes[offset]) | static_cast<uint32_t>(bytes[offset + 1]) << 8 |
           static_cast<uint32_t>(bytes[offset + 2]) << 16 | static_cast<uint32_t>(bytes[offset + 3]) << 24;
  }

  class TemporaryFile
  {
   public:
    explicit TemporaryFile(const char* name) : path_(std::filesystem::temp_directory_path() / name)
    {
      std::filesystem::remove(path_);
    }
    ~TemporaryFile()
    {
      std::filesystem::remove(path_);
    }
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
      return path_;
    }
    [[nodiscard]] std::string string() const
    {
      return path_.string();
    }

   private:
    std::filesystem::path path_;
  };
}  // namespace

TEST(PcapCaptureTest, ReadsLittleEndianMicrosecondCapture)
{
  auto bytes = make_header(0xa1b2c3d4, false);
  append_record(bytes, 5, 250'000, make_frame(0xaa), false);
  append_record(bytes, 6, 750'000, make_frame(0xbb, 74), false);

  const auto capture = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_TRUE(capture.has_value());
  ASSERT_EQ(capture.value().packet_count(), 2U);
  EXPECT_FALSE(capture.value().nanosecond_resolution());
  EXPECT_EQ(capture.value().snaplen(), 65535U);
  EXPECT_EQ(capture.value().packets()[0].timestamp_ns, 5'250'000'000ULL);
  EXPECT_EQ(capture.value().packets()[1].timestamp_ns, 6'750'000'000ULL);
  EXPECT_EQ(capture.value().packets()[1].captured_length, 74U);
}

TEST(PcapCaptureTest, ReadsBigEndianCaptureIdenticallyToLittleEndian)
{
  auto little = make_header(0xa1b2c3d4, false);
  append_record(little, 9, 1, make_frame(0xcd), false);
  auto big = make_header(0xd4c3b2a1, true);
  append_record(big, 9, 1, make_frame(0xcd), true);

  const auto from_little = wirelab::PcapCapture::from_bytes(std::move(little));
  const auto from_big = wirelab::PcapCapture::from_bytes(std::move(big));
  ASSERT_TRUE(from_little.has_value());
  ASSERT_TRUE(from_big.has_value());
  ASSERT_EQ(from_big.value().packet_count(), 1U);
  EXPECT_EQ(from_big.value().packets()[0].timestamp_ns, from_little.value().packets()[0].timestamp_ns);
  EXPECT_EQ(from_big.value().snaplen(), from_little.value().snaplen());
}

TEST(PcapCaptureTest, ScalesMicrosecondsButNotNanoseconds)
{
  auto microseconds = make_header(0xa1b2c3d4, false);
  append_record(microseconds, 1, 2, make_frame(0x11), false);
  auto nanoseconds = make_header(0xa1b23c4d, false);
  append_record(nanoseconds, 1, 2, make_frame(0x11), false);

  const auto from_microseconds = wirelab::PcapCapture::from_bytes(std::move(microseconds));
  const auto from_nanoseconds = wirelab::PcapCapture::from_bytes(std::move(nanoseconds));
  ASSERT_TRUE(from_microseconds.has_value());
  ASSERT_TRUE(from_nanoseconds.has_value());
  EXPECT_EQ(from_microseconds.value().packets()[0].timestamp_ns, 1'000'002'000ULL);
  EXPECT_EQ(from_nanoseconds.value().packets()[0].timestamp_ns, 1'000'000'002ULL);
  EXPECT_TRUE(from_nanoseconds.value().nanosecond_resolution());
}

TEST(PcapCaptureTest, ViewsBorrowFramesInPlace)
{
  auto bytes = make_header(0xa1b2c3d4, false);
  const auto first = make_frame(0x41);
  const auto second = make_frame(0x42);
  append_record(bytes, 1, 0, first, false);
  append_record(bytes, 2, 0, second, false);

  const auto capture = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_TRUE(capture.has_value());
  const auto view = capture.value().view(1, 7);
  ASSERT_NE(view.bytes, nullptr);
  EXPECT_EQ(view.size, second.size());
  EXPECT_EQ(view.ingress_port, 7U);
  EXPECT_EQ(view.bytes[second.size() - 1], 0x42);
  EXPECT_EQ(capture.value().frame(0)[first.size() - 1], 0x41);
  // Frames are indexed into one owned buffer, so consecutive views are adjacent.
  EXPECT_EQ(capture.value().frame(1) - capture.value().frame(0), static_cast<ptrdiff_t>(first.size() + 16));
}

TEST(PcapCaptureTest, RejectsUnknownMagic)
{
  auto bytes = make_header(0x12345678, false);
  const auto capture = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), wirelab::PcapError::UnknownMagic);
}

TEST(PcapCaptureTest, RejectsNonEthernetLinkType)
{
  auto bytes = make_header(0xa1b2c3d4, false, 101);
  const auto capture = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), wirelab::PcapError::UnsupportedLinkType);
}

TEST(PcapCaptureTest, RejectsHeaderShorterThanTheFileHeader)
{
  std::vector<uint8_t> bytes(23, 0);
  const auto capture = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), wirelab::PcapError::TruncatedHeader);
}

TEST(PcapCaptureTest, RejectsRecordRunningPastTheEndOfTheFile)
{
  auto bytes = make_header(0xa1b2c3d4, false);
  append_record(bytes, 1, 0, make_frame(0x33), false);
  bytes.resize(bytes.size() - 4);

  const auto capture = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), wirelab::PcapError::TruncatedPacket);
}

TEST(PcapCaptureTest, RejectsPartialRecordHeader)
{
  auto bytes = make_header(0xa1b2c3d4, false);
  append_u32(bytes, 1, false);
  append_u32(bytes, 0, false);

  const auto capture = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), wirelab::PcapError::TruncatedPacket);
}

TEST(PcapCaptureTest, RejectsImplausibleCapturedLengthWithoutAllocating)
{
  auto bytes = make_header(0xa1b2c3d4, false);
  append_u32(bytes, 1, false);
  append_u32(bytes, 0, false);
  append_u32(bytes, 0xfffffff0, false);
  append_u32(bytes, 0xfffffff0, false);

  const auto capture = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), wirelab::PcapError::OversizePacket);
}

TEST(PcapCaptureTest, ReadsAnEmptyCapture)
{
  auto bytes = make_header(0xa1b2c3d4, false);
  const auto capture = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_TRUE(capture.has_value());
  EXPECT_EQ(capture.value().packet_count(), 0U);
  EXPECT_EQ(capture.value().frame(0), nullptr);
}

TEST(PcapCaptureTest, ReportsAMissingFile)
{
  const auto capture = wirelab::PcapCapture::from_file("/nonexistent/wirelab/capture.pcap");
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), wirelab::PcapError::FileRead);
}

TEST(PcapNgWriterTest, WritesASectionAndInterfaceHeaderBeforeAnyPacket)
{
  TemporaryFile file("wirelab_pcapng_header_test.pcapng");
  {
    auto writer = wirelab::PcapNgWriter::create(file.string());
    ASSERT_TRUE(writer.has_value());
    ASSERT_TRUE(writer.value().flush().has_value());
    EXPECT_EQ(writer.value().written_packets(), 0U);
  }

  const auto bytes = read_file(file.path());
  ASSERT_GE(bytes.size(), 28U);
  EXPECT_EQ(read_u32(bytes, 0), 0x0a0d0d0aU);
  EXPECT_EQ(read_u32(bytes, 8), 0x1a2b3c4dU);
  const auto section_length = read_u32(bytes, 4);
  // Every block repeats its total length in the trailer.
  EXPECT_EQ(read_u32(bytes, section_length - 4), section_length);
  EXPECT_EQ(read_u32(bytes, section_length), 0x00000001U);
  EXPECT_EQ(bytes.size() % 4, 0U);
}

TEST(PcapNgWriterTest, WritesAnEnhancedPacketBlockCarryingTheComment)
{
  TemporaryFile file("wirelab_pcapng_packet_test.pcapng");
  const auto frame = make_frame(0x5a, 64);
  {
    auto writer = wirelab::PcapNgWriter::create(file.string());
    ASSERT_TRUE(writer.has_value());
    ASSERT_TRUE(writer.value()
                    .write(1'234'567'890ULL, frame.data(), frame.size(), frame.size(), "ANOMALY broadcast-storm")
                    .has_value());
    ASSERT_TRUE(writer.value().flush().has_value());
    EXPECT_EQ(writer.value().written_packets(), 1U);
  }

  const auto bytes = read_file(file.path());
  const auto section_length = read_u32(bytes, 4);
  const auto interface_length = read_u32(bytes, section_length + 4);
  const size_t packet_start = section_length + interface_length;
  ASSERT_LT(packet_start + 32, bytes.size());
  EXPECT_EQ(read_u32(bytes, packet_start), 0x00000006U);
  EXPECT_EQ(read_u32(bytes, packet_start + 8), 0U);
  // The 64-bit nanosecond timestamp is stored as high word then low word.
  EXPECT_EQ(read_u32(bytes, packet_start + 12), 0U);
  EXPECT_EQ(read_u32(bytes, packet_start + 16), 1'234'567'890U);
  EXPECT_EQ(read_u32(bytes, packet_start + 20), frame.size());
  EXPECT_EQ(read_u32(bytes, packet_start + 24), frame.size());

  const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  EXPECT_NE(text.find("ANOMALY broadcast-storm"), std::string::npos);
  EXPECT_NE(text.find("WireLab"), std::string::npos);
  const auto block_length = read_u32(bytes, packet_start + 4);
  EXPECT_EQ(read_u32(bytes, packet_start + block_length - 4), block_length);
  EXPECT_EQ(block_length % 4, 0U);
}

TEST(PcapNgWriterTest, PadsFramesWhoseLengthIsNotAMultipleOfFour)
{
  TemporaryFile file("wirelab_pcapng_padding_test.pcapng");
  const auto frame = make_frame(0x77, 61);
  {
    auto writer = wirelab::PcapNgWriter::create(file.string());
    ASSERT_TRUE(writer.has_value());
    ASSERT_TRUE(writer.value().write(1, frame.data(), frame.size(), frame.size(), "").has_value());
    ASSERT_TRUE(writer.value().flush().has_value());
  }

  const auto bytes = read_file(file.path());
  EXPECT_EQ(bytes.size() % 4, 0U);
  const auto section_length = read_u32(bytes, 4);
  const auto interface_length = read_u32(bytes, section_length + 4);
  const size_t packet_start = section_length + interface_length;
  const auto block_length = read_u32(bytes, packet_start + 4);
  EXPECT_EQ(block_length % 4, 0U);
  EXPECT_EQ(read_u32(bytes, packet_start + 20), 61U);
  EXPECT_EQ(read_u32(bytes, packet_start + block_length - 4), block_length);
}

TEST(PcapNgWriterTest, ReportsAnUnwritablePath)
{
  const auto writer = wirelab::PcapNgWriter::create("/nonexistent/wirelab/out.pcapng");
  ASSERT_FALSE(writer.has_value());
  EXPECT_EQ(writer.error(), wirelab::PcapError::FileWrite);
}

TEST(PcapNgWriterTest, RejectsAnImplausiblyLargeFrame)
{
  TemporaryFile file("wirelab_pcapng_oversize_test.pcapng");
  auto writer = wirelab::PcapNgWriter::create(file.string());
  ASSERT_TRUE(writer.has_value());
  const std::vector<uint8_t> frame(16, 0);
  const auto written = writer.value().write(1, frame.data(), wirelab::PCAP_MAXIMUM_SNAPLEN + 1, 0, "");
  ASSERT_FALSE(written.has_value());
  EXPECT_EQ(written.error(), wirelab::PcapError::OversizePacket);
}

TEST(PcapNgWriterTest, NeverReportsAWireLengthBelowTheCapturedLength)
{
  TemporaryFile file("wirelab_pcapng_snap_test.pcapng");
  const auto frame = make_frame(0x64, 64);
  {
    auto writer = wirelab::PcapNgWriter::create(file.string());
    ASSERT_TRUE(writer.has_value());
    // A capture that snapped without updating the wire length would otherwise
    // produce a block Wireshark rejects as malformed.
    ASSERT_TRUE(writer.value().write(1, frame.data(), frame.size(), 4, "").has_value());
    ASSERT_TRUE(writer.value().flush().has_value());
  }

  const auto bytes = read_file(file.path());
  const auto section_length = read_u32(bytes, 4);
  const auto interface_length = read_u32(bytes, section_length + 4);
  const size_t packet_start = section_length + interface_length;
  EXPECT_EQ(read_u32(bytes, packet_start + 20), frame.size());
  EXPECT_EQ(read_u32(bytes, packet_start + 24), frame.size());
}

TEST(PcapRoundTripTest, EveryFrameSurvivesAWriteThenReadCycle)
{
  TemporaryFile file("wirelab_pcapng_roundtrip_test.pcapng");
  auto bytes = make_header(0xa1b23c4d, false);
  append_record(bytes, 3, 400, make_frame(0x01, 60), false);
  append_record(bytes, 3, 900, make_frame(0x02, 128), false);
  const auto source = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_TRUE(source.has_value());

  {
    auto writer = wirelab::PcapNgWriter::create(file.string());
    ASSERT_TRUE(writer.has_value());
    for (size_t index = 0; index < source.value().packet_count(); ++index)
    {
      const auto& record = source.value().packets()[index];
      ASSERT_TRUE(writer.value()
                      .write(
                          record.timestamp_ns,
                          source.value().frame(index),
                          record.captured_length,
                          record.original_length,
                          "WireLab: broadcast, valid")
                      .has_value());
    }
    ASSERT_TRUE(writer.value().flush().has_value());
  }

  // The written file is now re-read through the same reader, so a change that
  // breaks either side of the codec fails here.
  const auto reloaded = wirelab::PcapCapture::from_file(file.string());
  ASSERT_TRUE(reloaded.has_value());
  ASSERT_EQ(reloaded.value().packet_count(), source.value().packet_count());
  for (size_t index = 0; index < source.value().packet_count(); ++index)
  {
    const auto& original = source.value().packets()[index];
    const auto& copy = reloaded.value().packets()[index];
    EXPECT_EQ(copy.timestamp_ns, original.timestamp_ns);
    EXPECT_EQ(copy.captured_length, original.captured_length);
    EXPECT_EQ(copy.original_length, original.original_length);
    EXPECT_EQ(std::memcmp(reloaded.value().frame(index), source.value().frame(index), original.captured_length), 0);
  }
}

TEST(PcapNgReaderTest, ReadsBackEveryPacketThisProjectWrote)
{
  TemporaryFile file("wirelab_pcapng_reread_test.pcapng");
  const auto first = make_frame(0x91, 60);
  const auto second = make_frame(0x92, 128);
  {
    auto writer = wirelab::PcapNgWriter::create(file.string());
    ASSERT_TRUE(writer.has_value());
    ASSERT_TRUE(writer.value().write(3'400'000'000ULL, first.data(), first.size(), first.size(), "one").has_value());
    ASSERT_TRUE(writer.value().write(3'900'000'000ULL, second.data(), second.size(), second.size(), "").has_value());
    ASSERT_TRUE(writer.value().flush().has_value());
  }

  const auto capture = wirelab::PcapCapture::from_file(file.string());
  ASSERT_TRUE(capture.has_value());
  EXPECT_TRUE(capture.value().pcapng());
  ASSERT_EQ(capture.value().packet_count(), 2U);
  EXPECT_EQ(capture.value().packets()[0].timestamp_ns, 3'400'000'000ULL);
  EXPECT_EQ(capture.value().packets()[1].timestamp_ns, 3'900'000'000ULL);
  EXPECT_EQ(capture.value().packets()[1].captured_length, second.size());
  EXPECT_EQ(capture.value().frame(0)[first.size() - 1], 0x91);
  EXPECT_EQ(capture.value().frame(1)[second.size() - 1], 0x92);
}

TEST(PcapNgReaderTest, ScalesMicrosecondInterfacesOntoNanoseconds)
{
  // An interface without if_tsresol defaults to microseconds, so identical
  // tick counts must land a thousand-fold apart.
  auto microseconds = make_pcapng(6, false);
  append_pcapng_packet(microseconds, 1'000'000ULL, make_frame(0x21), false);
  auto nanoseconds = make_pcapng(9, false);
  append_pcapng_packet(nanoseconds, 1'000'000ULL, make_frame(0x21), false);

  const auto from_microseconds = wirelab::PcapCapture::from_bytes(std::move(microseconds));
  const auto from_nanoseconds = wirelab::PcapCapture::from_bytes(std::move(nanoseconds));
  ASSERT_TRUE(from_microseconds.has_value());
  ASSERT_TRUE(from_nanoseconds.has_value());
  // A million microsecond ticks is one second.
  EXPECT_EQ(from_microseconds.value().packets()[0].timestamp_ns, 1'000'000'000ULL);
  EXPECT_EQ(from_nanoseconds.value().packets()[0].timestamp_ns, 1'000'000ULL);
  EXPECT_FALSE(from_microseconds.value().nanosecond_resolution());
  EXPECT_TRUE(from_nanoseconds.value().nanosecond_resolution());
}

TEST(PcapNgReaderTest, ReadsABigEndianSection)
{
  auto little = make_pcapng(9, false);
  append_pcapng_packet(little, 77ULL, make_frame(0x31), false);
  auto big = make_pcapng(9, true);
  append_pcapng_packet(big, 77ULL, make_frame(0x31), true);

  const auto from_little = wirelab::PcapCapture::from_bytes(std::move(little));
  const auto from_big = wirelab::PcapCapture::from_bytes(std::move(big));
  ASSERT_TRUE(from_little.has_value());
  ASSERT_TRUE(from_big.has_value());
  ASSERT_EQ(from_big.value().packet_count(), 1U);
  EXPECT_EQ(from_big.value().packets()[0].timestamp_ns, from_little.value().packets()[0].timestamp_ns);
  EXPECT_EQ(from_big.value().packets()[0].captured_length, from_little.value().packets()[0].captured_length);
}

TEST(PcapNgReaderTest, RejectsANonEthernetInterface)
{
  auto bytes = make_pcapng(9, false, 101);
  const auto capture = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), wirelab::PcapError::UnsupportedLinkType);
}

TEST(PcapNgReaderTest, RejectsABlockWhoseTrailingLengthDisagrees)
{
  auto bytes = make_pcapng(9, false);
  append_pcapng_packet(bytes, 5ULL, make_frame(0x44), false);
  // Corrupt only the trailing length of the packet block.
  bytes[bytes.size() - 4] = 0xff;

  const auto capture = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), wirelab::PcapError::TruncatedPacket);
}

TEST(PcapNgReaderTest, RejectsAPacketNamingAnUndeclaredInterface)
{
  auto bytes = make_pcapng(9, false);
  append_pcapng_packet(bytes, 5ULL, make_frame(0x55), false, 3);

  const auto capture = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), wirelab::PcapError::TruncatedPacket);
}

TEST(PcapNgReaderTest, TreatsAFileWithoutEitherMagicAsClassicPcap)
{
  // Dispatch keys off the leading magic, so a file that is neither format is
  // reported against the classic reader rather than the pcapng walk.
  std::vector<uint8_t> bytes(64, 0);
  const auto capture = wirelab::PcapCapture::from_bytes(std::move(bytes));
  ASSERT_FALSE(capture.has_value());
  EXPECT_EQ(capture.error(), wirelab::PcapError::UnknownMagic);
}
