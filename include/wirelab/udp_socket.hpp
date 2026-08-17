#ifndef PROJECT_UDP_SOCKET_HPP_
#define PROJECT_UDP_SOCKET_HPP_

#include <chrono>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "wirelab/expected.hpp"
#include "wirelab/sys_utils.hpp"

namespace wirelab
{
  enum class UdpError
  {
    SocketCreationFailed,
    BindFailed,
    SendFailed,
    ReceiveFailed,
    InvalidEndpoint,
    AddressResolutionFailed,
    InvalidSocket
  };
  [[nodiscard]] const char* to_string(UdpError error) noexcept;
  class Endpoint
  {
   private:
    std::string address_;
    uint16_t port_;

   public:
    Endpoint() noexcept : address_(), port_(0)
    {
    }
    Endpoint(std::string address, uint16_t port) noexcept : address_(std::move(address)), port_(port)
    {
    }
    [[nodiscard]] const std::string& address() const noexcept
    {
      return address_;
    }
    [[nodiscard]] uint16_t port() const noexcept
    {
      return port_;
    }
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] bool is_valid() const noexcept
    {
      return !address_.empty() && port_ != 0;
    }
    bool operator==(const Endpoint& other) const noexcept
    {
      return address_ == other.address_ && port_ == other.port_;
    }
    bool operator!=(const Endpoint& other) const noexcept
    {
      return !(*this == other);
    }
  };

  std::ostream& operator<<(std::ostream& os, const Endpoint& endpoint);

  class UdpSocket
  {
   private:
    SocketHandle socket_;
    Endpoint local_endpoint_;

   public:
    UdpSocket() = default;
    [[nodiscard]] static expected<UdpSocket, UdpError> create();
    UdpSocket(UdpSocket&& other) noexcept = default;
    UdpSocket& operator=(UdpSocket&& other) noexcept = default;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    ~UdpSocket() = default;
    [[nodiscard]] expected<void, UdpError> bind(std::string_view address, uint16_t port);
    [[nodiscard]] expected<size_t, UdpError> send_to(const std::vector<uint8_t>& data, const Endpoint& endpoint);
    [[nodiscard]] expected<size_t, UdpError> send_to(const uint8_t* data, size_t size, const Endpoint& endpoint);
    [[nodiscard]] expected<std::pair<std::vector<uint8_t>, Endpoint>, UdpError> receive_from();
    [[nodiscard]] expected<std::pair<std::vector<uint8_t>, Endpoint>, UdpError> receive_from(size_t max_size);
    // Blocks until a datagram is queued or the timeout lapses; false means the
    // timeout won. Lets a single-threaded receive loop keep its own deadlines
    // instead of parking forever inside recvfrom.
    [[nodiscard]] expected<bool, UdpError> wait_readable(std::chrono::milliseconds timeout) const;

    [[nodiscard]] bool is_valid() const noexcept
    {
      return socket_.is_valid();
    }

    [[nodiscard]] const Endpoint& local_endpoint() const noexcept
    {
      return local_endpoint_;
    }
    [[nodiscard]] native_socket_handle get_fd() const noexcept
    {
      return socket_.get();
    }
    void close() noexcept
    {
      socket_.close();
      local_endpoint_ = Endpoint{};
    }

    explicit operator bool() const noexcept
    {
      return is_valid();
    }

   private:
    explicit UdpSocket(SocketHandle socket) : socket_(std::move(socket))
    {
    }
  };
}  // namespace wirelab

#endif
