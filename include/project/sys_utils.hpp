#ifndef PROJECT_SYS_UTILS_HPP_
#define PROJECT_SYS_UTILS_HPP_

#include <cstdint>
#include <stdexcept>
#include <string>
#include <system_error>

namespace project
{
  class SystemException : public std::runtime_error
  {
   private:
    int error_code_;

   public:
    explicit SystemException(const std::string& message, int error_code = 0)
        : std::runtime_error(message),
          error_code_(error_code)
    {
    }
    [[nodiscard]] int error_code() const noexcept
    {
      return error_code_;
    }
  };
  class NetworkException : public SystemException
  {
   public:
    explicit NetworkException(const std::string& message, int error_code = 0) : SystemException(message, error_code)
    {
    }
  };
  class FileException : public SystemException
  {
   public:
    explicit FileException(const std::string& message, int error_code = 0) : SystemException(message, error_code)
    {
    }
  };
  using native_file_handle = std::intptr_t;
#ifdef _WIN32
  using native_socket_handle = std::uintptr_t;
#else
  using native_socket_handle = int;
#endif

  class FileDescriptor
  {
   private:
    native_file_handle fd_;

   public:
    FileDescriptor() noexcept : fd_(-1) {}
    explicit FileDescriptor(native_file_handle fd) noexcept : fd_(fd) {}
    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.release()) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
      if (this != &other) reset(other.release());
      return *this;
    }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    ~FileDescriptor();
    void close() noexcept;
    [[nodiscard]] bool is_valid() const noexcept { return fd_ != -1; }
    [[nodiscard]] native_file_handle get() const noexcept { return fd_; }
    [[nodiscard]] native_file_handle release() noexcept
    {
      const auto fd = fd_;
      fd_ = -1;
      return fd;
    }
    void reset(native_file_handle fd = -1) noexcept
    {
      close();
      fd_ = fd;
    }
    explicit operator bool() const noexcept { return is_valid(); }
  };

  class SocketHandle
  {
   private:
    native_socket_handle socket_;

   public:
    SocketHandle() noexcept;
    explicit SocketHandle(native_socket_handle socket) noexcept : socket_(socket) {}
    SocketHandle(SocketHandle&& other) noexcept : socket_(other.release()) {}
    SocketHandle& operator=(SocketHandle&& other) noexcept
    {
      if (this != &other) reset(other.release());
      return *this;
    }
    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;
    ~SocketHandle();
    void close() noexcept;
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] native_socket_handle get() const noexcept { return socket_; }
    [[nodiscard]] native_socket_handle release() noexcept;
    void reset(native_socket_handle socket) noexcept
    {
      close();
      socket_ = socket;
    }
    explicit operator bool() const noexcept { return is_valid(); }
  };
}  

#endif  
