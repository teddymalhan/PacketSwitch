/**
 * @file sys_utils.hpp
 * @brief System utilities with RAII wrappers and exception handling
 *
 * Provides modern C++ wrappers for system resources like file descriptors
 * and sockets, along with a custom exception hierarchy for error handling.
 */

#ifndef PROJECT_SYS_UTILS_HPP_
#define PROJECT_SYS_UTILS_HPP_

#include <stdexcept>
#include <string>
#include <system_error>

namespace project
{
  /**
   * @brief Base exception class for system-level errors
   */
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

    /**
     * @brief Get the system error code
     * @return The error code (typically errno)
     */
    [[nodiscard]] int error_code() const noexcept
    {
      return error_code_;
    }
  };

  /**
   * @brief Exception class for network-related errors
   */
  class NetworkException : public SystemException
  {
   public:
    explicit NetworkException(const std::string& message, int error_code = 0) : SystemException(message, error_code)
    {
    }
  };

  /**
   * @brief Exception class for file I/O errors
   */
  class FileException : public SystemException
  {
   public:
    explicit FileException(const std::string& message, int error_code = 0) : SystemException(message, error_code)
    {
    }
  };

  /**
   * @brief RAII wrapper for POSIX file descriptors
   *
   * Automatically closes the file descriptor when the object is destroyed.
   * This class is move-only to prevent double-close errors.
   *
   * Example:
   * @code
   * FileDescriptor fd(open("/path/to/file", O_RDONLY));
   * if (!fd.is_valid()) {
   *   // handle error
   * }
   * // File automatically closed when fd goes out of scope
   * @endcode
   */
  class FileDescriptor
  {
   private:
    int fd_;

   public:
    /**
     * @brief Default constructor - creates an invalid file descriptor
     */
    FileDescriptor() noexcept : fd_(-1)
    {
    }

    /**
     * @brief Constructs from an existing file descriptor
     * @param fd The file descriptor to manage (can be -1 for invalid)
     */
    explicit FileDescriptor(int fd) noexcept : fd_(fd)
    {
    }

    /**
     * @brief Move constructor
     */
    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_)
    {
      other.fd_ = -1;
    }

    /**
     * @brief Move assignment operator
     */
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

    /**
     * @brief Deleted copy constructor - file descriptors cannot be copied
     */
    FileDescriptor(const FileDescriptor&) = delete;

    /**
     * @brief Deleted copy assignment - file descriptors cannot be copied
     */
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    /**
     * @brief Destructor - automatically closes the file descriptor
     */
    ~FileDescriptor();

    /**
     * @brief Close the file descriptor
     *
     * Safe to call multiple times. After closing, the descriptor is set to -1.
     */
    void close() noexcept;

    /**
     * @brief Check if the file descriptor is valid
     * @return true if the descriptor is valid (>= 0), false otherwise
     */
    [[nodiscard]] bool is_valid() const noexcept
    {
      return fd_ >= 0;
    }

    /**
     * @brief Get the raw file descriptor
     * @return The file descriptor value
     */
    [[nodiscard]] int get() const noexcept
    {
      return fd_;
    }

    /**
     * @brief Release ownership of the file descriptor
     *
     * Returns the file descriptor and sets the internal value to -1.
     * The caller is responsible for closing the descriptor.
     *
     * @return The file descriptor value
     */
    [[nodiscard]] int release() noexcept
    {
      int fd = fd_;
      fd_ = -1;
      return fd;
    }

    /**
     * @brief Reset to a new file descriptor
     *
     * Closes the current descriptor (if valid) and takes ownership of the new one.
     *
     * @param fd The new file descriptor to manage
     */
    void reset(int fd = -1) noexcept
    {
      close();
      fd_ = fd;
    }

    /**
     * @brief Explicit conversion to bool for validity checking
     */
    explicit operator bool() const noexcept
    {
      return is_valid();
    }
  };

  /**
   * @brief RAII wrapper for socket file descriptors
   *
   * Similar to FileDescriptor but specifically for sockets.
   * Provides socket-specific operations and cleanup.
   */
  class SocketHandle
  {
   private:
    FileDescriptor fd_;

   public:
    /**
     * @brief Default constructor - creates an invalid socket
     */
    SocketHandle() noexcept = default;

    /**
     * @brief Constructs from an existing socket descriptor
     * @param sockfd The socket descriptor to manage
     */
    explicit SocketHandle(int sockfd) noexcept : fd_(sockfd)
    {
    }

    /**
     * @brief Move constructor
     */
    SocketHandle(SocketHandle&& other) noexcept = default;

    /**
     * @brief Move assignment operator
     */
    SocketHandle& operator=(SocketHandle&& other) noexcept = default;

    /**
     * @brief Deleted copy constructor
     */
    SocketHandle(const SocketHandle&) = delete;

    /**
     * @brief Deleted copy assignment
     */
    SocketHandle& operator=(const SocketHandle&) = delete;

    /**
     * @brief Destructor - closes the socket
     */
    ~SocketHandle() = default;

    /**
     * @brief Close the socket
     */
    void close() noexcept
    {
      fd_.close();
    }

    /**
     * @brief Check if the socket is valid
     * @return true if the socket is valid, false otherwise
     */
    [[nodiscard]] bool is_valid() const noexcept
    {
      return fd_.is_valid();
    }

    /**
     * @brief Get the raw socket descriptor
     * @return The socket descriptor value
     */
    [[nodiscard]] int get() const noexcept
    {
      return fd_.get();
    }

    /**
     * @brief Release ownership of the socket descriptor
     * @return The socket descriptor value
     */
    [[nodiscard]] int release() noexcept
    {
      return fd_.release();
    }

    /**
     * @brief Reset to a new socket descriptor
     * @param sockfd The new socket descriptor to manage
     */
    void reset(int sockfd = -1) noexcept
    {
      fd_.reset(sockfd);
    }

    /**
     * @brief Explicit conversion to bool for validity checking
     */
    explicit operator bool() const noexcept
    {
      return is_valid();
    }
  };

}  // namespace project

#endif  // PROJECT_SYS_UTILS_HPP_
