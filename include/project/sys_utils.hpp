#ifndef PROJECT_SYS_UTILS_HPP_
#define PROJECT_SYS_UTILS_HPP_

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
  class FileDescriptor
  {
   private:
    int fd_;

   public:
    FileDescriptor() noexcept : fd_(-1)
    {
    }
    explicit FileDescriptor(int fd) noexcept : fd_(fd)
    {
    }
    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_)
    {
      other.fd_ = -1;
    }
    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
      if (this != &other)
      {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
      }
      return *this;
    }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    ~FileDescriptor();
    void close() noexcept;
    [[nodiscard]] bool is_valid() const noexcept
    {
      return fd_ >= 0;
    }
    [[nodiscard]] int get() const noexcept
    {
      return fd_;
    }
    [[nodiscard]] int release() noexcept
    {
      int fd = fd_;
      fd_ = -1;
      return fd;
    }
    void reset(int fd = -1) noexcept
    {
      close();
      fd_ = fd;
    }
    explicit operator bool() const noexcept
    {
      return is_valid();
    }
  };
  class SocketHandle
  {
   private:
    FileDescriptor fd_;

   public:
      SocketHandle() noexcept = default;
    explicit SocketHandle(int sockfd) noexcept : fd_(sockfd)
    {
    }
    SocketHandle(SocketHandle&& other) noexcept = default;
    SocketHandle& operator=(SocketHandle&& other) noexcept = default;
    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;
    ~SocketHandle() = default;
    void close() noexcept
    {
      fd_.close();
    }
    [[nodiscard]] bool is_valid() const noexcept
    {
      return fd_.is_valid();
    }
    [[nodiscard]] int get() const noexcept
    {
      return fd_.get();
    }
    [[nodiscard]] int release() noexcept
    {
      return fd_.release();
    }
    void reset(int sockfd = -1) noexcept
    {
      fd_.reset(sockfd);
    }
    explicit operator bool() const noexcept
    {
      return is_valid();
    }
  };
}  

#endif  
