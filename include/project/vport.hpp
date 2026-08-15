#ifndef PROJECT_VPORT_HPP_
#define PROJECT_VPORT_HPP_

#include <atomic>
#include <string>
#include <string_view>

#include "project/ethernet_frame.hpp"
#include "project/expected.hpp"
#include "project/joining_thread.hpp"
#include "project/tap_device.hpp"
#include "project/udp_socket.hpp"

namespace project
{
  enum class VPortError
  {
    TapDeviceCreationFailed,
    SocketCreationFailed,
    InvalidVSwitchEndpoint,
    AlreadyRunning,
    NotRunning
  };
  [[nodiscard]] const char* to_string(VPortError error) noexcept;

  class VPort
  {
   private:
    TapDevice tap_device_;
    UdpSocket udp_socket_;
    Endpoint vswitch_endpoint_;
    std::string device_name_;

    std::atomic<bool> running_;
    joining_thread tap_to_switch_thread_;
    joining_thread switch_to_tap_thread_;

       VPort(TapDevice tap_device, UdpSocket udp_socket, Endpoint vswitch_endpoint, std::string device_name);

   public:
      [[nodiscard]] static expected<VPort, VPortError>
    create(std::string_view device_name, std::string_view vswitch_address, uint16_t vswitch_port);
    VPort(VPort&& other) noexcept;
    VPort& operator=(VPort&& other) noexcept;
    VPort(const VPort&) = delete;
    VPort& operator=(const VPort&) = delete;
    ~VPort();

   [[nodiscard]] expected<void, VPortError> start();
   void stop() noexcept;
   [[nodiscard]] bool is_running() const noexcept
    {
      return running_.load();
    }

    [[nodiscard]] const std::string& device_name() const noexcept
    {
      return device_name_;
    }

    [[nodiscard]] const Endpoint& vswitch_endpoint() const noexcept
    {
      return vswitch_endpoint_;
    }

   private:
    void forward_tap_to_switch();
    void forward_switch_to_tap();
    void log_frame(std::string_view direction, const EthernetFrame& frame) const;
  };

}  

#endif  
