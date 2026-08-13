/**
 * @file udp_socket.hpp
 * @brief Modern C++ wrapper for UDP sockets
 *
 * Provides RAII management of UDP sockets with type-safe endpoint handling.
 */

#ifndef PROJECT_UDP_SOCKET_HPP_
#define PROJECT_UDP_SOCKET_HPP_

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "project/expected.hpp"
#include "project/sys_utils.hpp"

namespace project
{
  /**
   * @brief Error codes for UDP socket operations
   */
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

  /**
   * @brief Convert UdpError to string representation
   * @param error The error code
   * @return String description of the error
   */
  [[nodiscard]] const char* to_string(UdpError error) noexcept;

  /**
   * @brief Represents a network endpoint (IP address + port)
   *
   * An endpoint is an address that can be used for UDP communication.
   * Internally stores a sockaddr_in structure for IPv4 addresses.
   */
  class Endpoint
  {
   private:
    std::string address_;
    uint16_t port_;

   public:
    /**
     * @brief Default constructor - creates an invalid endpoint
     */
    Endpoint() noexcept : address_(), port_(0)
    {
    }

    /**
     * @brief Construct an endpoint from address and port
     * @param address IP address (e.g., "127.0.0.1" or "0.0.0.0")
     * @param port Port number
     */
    Endpoint(std::string address, uint16_t port) noexcept : address_(std::move(address)), port_(port)
    {
    }

    /**
     * @brief Get the IP address
     * @return The IP address string
     */
    [[nodiscard]] const std::string& address() const noexcept
    {
      return address_;
    }

    /**
     * @brief Get the port number
     * @return The port number
     */
    [[nodiscard]] uint16_t port() const noexcept
    {
      return port_;
    }

    /**
     * @brief Convert to string representation (e.g., "127.0.0.1:8080")
     * @return String representation
     */
    [[nodiscard]] std::string to_string() const;

    /**
     * @brief Check if the endpoint is valid
     * @return true if the endpoint has a non-empty address and non-zero port
     */
    [[nodiscard]] bool is_valid() const noexcept
    {
      return !address_.empty() && port_ != 0;
    }

    /**
     * @brief Equality comparison
     */
    bool operator==(const Endpoint& other) const noexcept
    {
      return address_ == other.address_ && port_ == other.port_;
    }

    /**
     * @brief Inequality comparison
     */
    bool operator!=(const Endpoint& other) const noexcept
    {
      return !(*this == other);
    }
  };

  /**
   * @brief Output stream operator for Endpoint
   */
  std::ostream& operator<<(std::ostream& os, const Endpoint& endpoint);

  /**
   * @brief RAII wrapper for UDP sockets
   *
   * Provides a modern C++ interface for UDP socket operations with
   * automatic resource management. The socket is automatically closed
   * when the object is destroyed.
   *
   * This class is move-only to prevent resource duplication.
   *
   * Example:
   * @code
   * // Create a UDP socket
   * auto socket_result = UdpSocket::create();
   * if (!socket_result) {
   *   // handle error
   *   return;
   * }
   * UdpSocket socket = std::move(*socket_result);
   *
   * // Bind to a port
   * if (auto bind_result = socket.bind("0.0.0.0", 8080); !bind_result) {
   *   // handle bind error
   * }
   *
   * // Send data
   * std::vector<uint8_t> data = {1, 2, 3, 4};
   * Endpoint dest("127.0.0.1", 9000);
   * socket.send_to(data, dest);
   * @endcode
   */
  class UdpSocket
  {
   private:
    SocketHandle socket_;
    Endpoint local_endpoint_;

   public:
    /**
     * @brief Default constructor - creates an invalid socket
     */
    UdpSocket() = default;

    /**
     * @brief Create a UDP socket
     * @return expected<UdpSocket, UdpError> The created socket or an error
     */
    [[nodiscard]] static expected<UdpSocket, UdpError> create();

    /**
     * @brief Move constructor
     */
    UdpSocket(UdpSocket&& other) noexcept = default;

    /**
     * @brief Move assignment operator
     */
    UdpSocket& operator=(UdpSocket&& other) noexcept = default;

    /**
     * @brief Deleted copy constructor
     */
    UdpSocket(const UdpSocket&) = delete;

    /**
     * @brief Deleted copy assignment
     */
    UdpSocket& operator=(const UdpSocket&) = delete;

    /**
     * @brief Destructor - closes the socket
     */
    ~UdpSocket() = default;

    /**
     * @brief Bind the socket to a local address and port
     *
     * @param address Local IP address (e.g., "0.0.0.0" for all interfaces)
     * @param port Local port number
     * @return expected<void, UdpError> Success or error
     */
    [[nodiscard]] expected<void, UdpError> bind(std::string_view address, uint16_t port);

    /**
     * @brief Send data to a remote endpoint
     *
     * @param data The data to send
     * @param endpoint The destination endpoint
     * @return expected<size_t, UdpError> Number of bytes sent or error
     */
    [[nodiscard]] expected<size_t, UdpError> send_to(const std::vector<uint8_t>& data, const Endpoint& endpoint);

    /**
     * @brief Send data to a remote endpoint
     *
     * @param data Pointer to data buffer
     * @param size Size of data buffer
     * @param endpoint The destination endpoint
     * @return expected<size_t, UdpError> Number of bytes sent or error
     */
    [[nodiscard]] expected<size_t, UdpError> send_to(const uint8_t* data, size_t size, const Endpoint& endpoint);

    /**
     * @brief Receive data from any remote endpoint
     *
     * @return expected<std::pair<std::vector<uint8_t>, Endpoint>, UdpError>
     *         Received data and sender endpoint, or error
     */
    [[nodiscard]] expected<std::pair<std::vector<uint8_t>, Endpoint>, UdpError> receive_from();

    /**
     * @brief Receive data from any remote endpoint with maximum size
     *
     * @param max_size Maximum number of bytes to receive
     * @return expected<std::pair<std::vector<uint8_t>, Endpoint>, UdpError>
     *         Received data and sender endpoint, or error
     */
    [[nodiscard]] expected<std::pair<std::vector<uint8_t>, Endpoint>, UdpError> receive_from(size_t max_size);

    /**
     * @brief Check if the socket is valid
     * @return true if the socket is valid and open
     */
    [[nodiscard]] bool is_valid() const noexcept
    {
      return socket_.is_valid();
    }

    /**
     * @brief Get the local endpoint (after binding)
     * @return The local endpoint
     */
    [[nodiscard]] const Endpoint& local_endpoint() const noexcept
    {
      return local_endpoint_;
    }

    /**
     * @brief Get the socket file descriptor
     * @return The socket descriptor
     */
    [[nodiscard]] int get_fd() const noexcept
    {
      return socket_.get();
    }

    /**
     * @brief Close the socket
     */
    void close() noexcept
    {
      socket_.close();
      local_endpoint_ = Endpoint{};
    }

    /**
     * @brief Explicit conversion to bool for validity checking
     */
    explicit operator bool() const noexcept
    {
      return is_valid();
    }

   private:
    /**
     * @brief Private constructor for create()
     */
    explicit UdpSocket(SocketHandle socket) : socket_(std::move(socket))
    {
    }
  };

}  // namespace project

#endif  // PROJECT_UDP_SOCKET_HPP_
