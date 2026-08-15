#ifndef PROJECT_TAP_DEVICE_HPP_
#define PROJECT_TAP_DEVICE_HPP_

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "wirelab/expected.hpp"
#include "wirelab/sys_utils.hpp"

namespace wirelab
{
  constexpr size_t ETHER_MAX_LEN = 1518;
  enum class TapError
  {
    DeviceOpenFailed,
    IoctlFailed,
    ReadFailed,
    WriteFailed,
    InvalidDevice,
    PartialWrite
  };
  [[nodiscard]] const char* to_string(TapError error) noexcept;
  class TapDevice
  {
   private:
    FileDescriptor fd_;
    std::string device_name_;
    TapDevice(FileDescriptor fd, std::string device_name);

   public:
    [[nodiscard]] static expected<TapDevice, TapError> create(std::string_view device_name = "");
    TapDevice() = default;
    TapDevice(TapDevice&& other) noexcept = default;
    TapDevice& operator=(TapDevice&& other) noexcept = default;
    TapDevice(const TapDevice&) = delete;
    TapDevice& operator=(const TapDevice&) = delete;
    ~TapDevice() = default;
    [[nodiscard]] expected<std::vector<uint8_t>, TapError> read_frame();
    [[nodiscard]] expected<size_t, TapError> write_frame(const std::vector<uint8_t>& frame);
    [[nodiscard]] expected<size_t, TapError> write_frame(const uint8_t* data, size_t size);
    [[nodiscard]] bool is_valid() const noexcept
    {
      return fd_.is_valid();
    }
    [[nodiscard]] const std::string& device_name() const noexcept
    {
      return device_name_;
    }
    [[nodiscard]] native_file_handle get_fd() const noexcept
    {
      return fd_.get();
    }
    void close() noexcept
    {
      fd_.close();
      device_name_.clear();
    }
    explicit operator bool() const noexcept
    {
      return is_valid();
    }
  };
}  

#endif  
