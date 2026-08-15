#include "project/tap_device.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <algorithm>
#include <cwctype>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/if.h>
#include <linux/if_tun.h>
#elif __APPLE__
#include <net/if.h>
#include <sys/kern_control.h>
#include <sys/kern_event.h>
#include <sys/socket.h>
#include <sys/sys_domain.h>
#ifndef UTUN_OPT_IFNAME
#define UTUN_OPT_IFNAME 2
#endif
#endif
#endif

#include <cstring>
#include <cctype>
#include <iterator>
#include <limits>
#include <vector>

#ifdef _WIN32
namespace
{
  constexpr DWORD tap_control_code(DWORD request)
  {
    return CTL_CODE(FILE_DEVICE_UNKNOWN, request, METHOD_BUFFERED, FILE_ANY_ACCESS);
  }
  constexpr DWORD TAP_SET_MEDIA_STATUS = tap_control_code(6);
  constexpr wchar_t ADAPTER_KEY[] =
      L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}";
  constexpr wchar_t NETWORK_KEY[] =
      L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}";

  bool query_string(HKEY key, const wchar_t* name, std::wstring& value)
  {
    DWORD type = 0;
    DWORD bytes = 0;
    if (::RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS || type != REG_SZ)
      return false;
    value.resize(bytes / sizeof(wchar_t));
    if (::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes) != ERROR_SUCCESS)
      return false;
    if (!value.empty() && value.back() == L'\0') value.pop_back();
    return true;
  }

  std::string lowercase_ascii(std::string value)
  {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
  }

  std::string utf8(const std::wstring& value)
  {
    if (value.empty()) return {};
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                            nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                          result.data(), size, nullptr, nullptr);
    return result;
  }

  std::wstring lowercase(std::wstring value)
  {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return std::towlower(c); });
    return value;
  }

  struct TapCandidate { std::wstring guid; std::wstring name; };

  std::vector<TapCandidate> enumerate_taps()
  {
    std::vector<TapCandidate> result;
    HKEY adapters = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, ADAPTER_KEY, 0, KEY_READ, &adapters) != ERROR_SUCCESS) return result;
    for (DWORD index = 0;; ++index)
    {
      wchar_t subkey[256]{};
      DWORD length = static_cast<DWORD>(std::size(subkey));
      const LONG status = ::RegEnumKeyExW(adapters, index, subkey, &length, nullptr, nullptr, nullptr, nullptr);
      if (status == ERROR_NO_MORE_ITEMS) break;
      if (status != ERROR_SUCCESS) continue;
      HKEY adapter = nullptr;
      if (::RegOpenKeyExW(adapters, subkey, 0, KEY_READ, &adapter) != ERROR_SUCCESS) continue;
      std::wstring component;
      std::wstring guid;
      const bool valid = query_string(adapter, L"ComponentId", component)
                         && query_string(adapter, L"NetCfgInstanceId", guid)
                         && (lowercase(component) == L"tap0901" || lowercase(component) == L"root\\tap0901");
      ::RegCloseKey(adapter);
      if (!valid) continue;
      std::wstring connection_path = guid + L"\\Connection";
      HKEY network_root = nullptr;
      HKEY connection = nullptr;
      std::wstring name = guid;
      if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, NETWORK_KEY, 0, KEY_READ, &network_root) == ERROR_SUCCESS)
      {
        if (::RegOpenKeyExW(network_root, connection_path.c_str(), 0, KEY_READ, &connection) == ERROR_SUCCESS)
        {
          query_string(connection, L"Name", name);
          ::RegCloseKey(connection);
        }
        ::RegCloseKey(network_root);
      }
      result.push_back({std::move(guid), std::move(name)});
    }
    ::RegCloseKey(adapters);
    return result;
  }
}
#endif

namespace project
{
  const char* to_string(TapError error) noexcept
  {
    switch (error)
    {
      case TapError::DeviceOpenFailed: return "Failed to open /dev/net/tun";
      case TapError::IoctlFailed: return "ioctl(TUNSETIFF) failed";
      case TapError::ReadFailed: return "Failed to read from TAP device";
      case TapError::WriteFailed: return "Failed to write to TAP device";
      case TapError::InvalidDevice: return "Invalid TAP device";
      case TapError::PartialWrite: return "Partial write to TAP device";
      default: return "Unknown TAP error";
    }
  }

  TapDevice::TapDevice(FileDescriptor fd, std::string device_name) : fd_(std::move(fd)), device_name_(std::move(device_name))
  {
  }

  expected<TapDevice, TapError> TapDevice::create(std::string_view device_name)
  {
#ifdef _WIN32
    const auto candidates = enumerate_taps();
    const TapCandidate* selected = nullptr;
    if (device_name.empty())
    {
      if (candidates.size() != 1) return unexpected(TapError::DeviceOpenFailed);
      selected = &candidates.front();
    }
    else
    {
      const auto selector = lowercase_ascii(std::string(device_name));
      for (const auto& candidate : candidates)
      {
        if (lowercase_ascii(utf8(candidate.name)) == selector || lowercase_ascii(utf8(candidate.guid)) == selector)
        {
          selected = &candidate;
          break;
        }
      }
      if (selected == nullptr) return unexpected(TapError::DeviceOpenFailed);
    }
    const std::wstring path = L"\\\\.\\Global\\" + selected->guid + L".tap";
    HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_SYSTEM, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return unexpected(TapError::DeviceOpenFailed);
    ULONG connected = 1;
    DWORD returned = 0;
    if (!::DeviceIoControl(handle, TAP_SET_MEDIA_STATUS, &connected, sizeof(connected), nullptr, 0, &returned, nullptr))
    {
      ::CloseHandle(handle);
      return unexpected(TapError::IoctlFailed);
    }
    return TapDevice(FileDescriptor(reinterpret_cast<native_file_handle>(handle)), utf8(selected->name));
#elif defined(__linux__)
    
    int fd = ::open("/dev/net/tun", O_RDWR);
    if (fd < 0)
    {
      return unexpected(TapError::DeviceOpenFailed);
    }

    
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;  

    
    if (!device_name.empty())
    {
      if (device_name.size() >= IFNAMSIZ)
      {
        ::close(fd);
        return unexpected(TapError::IoctlFailed);
      }
      std::strncpy(ifr.ifr_name, device_name.data(), IFNAMSIZ - 1);
    }

    
    if (::ioctl(fd, TUNSETIFF, &ifr) < 0)
    {
      ::close(fd);
      return unexpected(TapError::IoctlFailed);
    }

    
    std::string actual_name(ifr.ifr_name);

    return TapDevice(FileDescriptor(fd), std::move(actual_name));

#elif __APPLE__
    
    
    

    
    int fd = ::socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0)
    {
      return unexpected(TapError::DeviceOpenFailed);
    }

  
    struct ctl_info ctl_info;
    std::memset(&ctl_info, 0, sizeof(ctl_info));
    std::strncpy(ctl_info.ctl_name, "com.apple.net.utun_control", sizeof(ctl_info.ctl_name));

    if (::ioctl(fd, CTLIOCGINFO, &ctl_info) < 0)
    {
      ::close(fd);
      return unexpected(TapError::IoctlFailed);
    }

    struct sockaddr_ctl sc;
    std::memset(&sc, 0, sizeof(sc));
    sc.sc_id = ctl_info.ctl_id;
    sc.sc_len = sizeof(sc);
    sc.sc_family = AF_SYSTEM;
    sc.ss_sysaddr = AF_SYS_CONTROL;
    sc.sc_unit = 0;  

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&sc), sizeof(sc)) < 0)
    {
      ::close(fd);
      return unexpected(TapError::IoctlFailed);
    }

    
    char utun_name[20];
    socklen_t utun_name_len = sizeof(utun_name);
    if (::getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, utun_name, &utun_name_len) >= 0)
    {
      std::string actual_name(utun_name);
      return TapDevice(FileDescriptor(fd), std::move(actual_name));
    }
    else
    {
      
      std::string actual_name = device_name.empty() ? "utun0" : std::string(device_name);
      return TapDevice(FileDescriptor(fd), std::move(actual_name));
    }

#else
    (void)device_name;  
    return unexpected(TapError::DeviceOpenFailed);
#endif
  }

  expected<std::vector<uint8_t>, TapError> TapDevice::read_frame()
  {
    if (!is_valid())
    {
      return unexpected(TapError::InvalidDevice);
    }

    std::array<uint8_t, ETHER_MAX_LEN> buffer;
#ifdef _WIN32
    DWORD n = 0;
    if (!::ReadFile(reinterpret_cast<HANDLE>(fd_.get()), buffer.data(), static_cast<DWORD>(buffer.size()), &n, nullptr))
      return unexpected(TapError::ReadFailed);
#else
    const auto n = ::read(static_cast<int>(fd_.get()), buffer.data(), buffer.size());
    if (n < 0) return unexpected(TapError::ReadFailed);
#endif
    std::vector<uint8_t> frame(buffer.begin(), buffer.begin() + n);
    return frame;
  }

  expected<size_t, TapError> TapDevice::write_frame(const std::vector<uint8_t>& frame)
  {
    return write_frame(frame.data(), frame.size());
  }

  expected<size_t, TapError> TapDevice::write_frame(const uint8_t* data, size_t size)
  {
    if (!is_valid())
    {
      return unexpected(TapError::InvalidDevice);
    }

#ifdef _WIN32
    if (size > static_cast<size_t>(std::numeric_limits<DWORD>::max()))
      return unexpected(TapError::WriteFailed);
    DWORD n = 0;
    if (!::WriteFile(reinterpret_cast<HANDLE>(fd_.get()), data, static_cast<DWORD>(size), &n, nullptr))
      return unexpected(TapError::WriteFailed);
#else
    const auto n = ::write(static_cast<int>(fd_.get()), data, size);
    if (n < 0) return unexpected(TapError::WriteFailed);
#endif

    if (static_cast<size_t>(n) != size)
    {
      return unexpected(TapError::PartialWrite);
    }

    return static_cast<size_t>(n);
  }
}  
