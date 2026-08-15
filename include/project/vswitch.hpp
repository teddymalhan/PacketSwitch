#ifndef PROJECT_VSWITCH_HPP_
#define PROJECT_VSWITCH_HPP_

#include <atomic>
#include <memory>

#include "project/ethernet_frame.hpp"
#include "project/mac_table.hpp"
#include "project/udp_socket.hpp"

namespace project
{
  enum class VSwitchError
  {
    SocketCreationFailed,
    BindFailed,
    AlreadyRunning,
    NotRunning
  };

  [[nodiscard]] const char* to_string(VSwitchError error) noexcept;

  class VSwitch
  {
   private:
    UdpSocket socket_;
    MacTable mac_table_;
    uint16_t port_;

    std::atomic<bool> running_;

   public:
      [[nodiscard]] static expected<VSwitch, VSwitchError> create(uint16_t port);
    VSwitch() = default;
    VSwitch(VSwitch&& other) noexcept;
    VSwitch& operator=(VSwitch&& other) noexcept;
    VSwitch(const VSwitch&) = delete;
    VSwitch& operator=(const VSwitch&) = delete;
    ~VSwitch();
    [[nodiscard]] expected<void, VSwitchError> start();
     void stop() noexcept;
    [[nodiscard]] bool is_running() const noexcept
    {
      return running_.load();
    }
    [[nodiscard]] uint16_t port() const noexcept
    {
      return port_;
    }
    [[nodiscard]] size_t learned_macs() const
    {
      return mac_table_.size();
    }
    [[nodiscard]] std::unordered_map<MacAddress, Endpoint> get_mac_table() const
    {
      return mac_table_.get_all_entries();
    }

   private:
      VSwitch(UdpSocket socket, uint16_t port) noexcept;
       void process_frame(const std::vector<uint8_t>& frame_data, const Endpoint& sender_endpoint);
    void log_frame(
        const EthernetFrame& frame,
        const Endpoint& sender_endpoint,
        std::string_view action,
        std::string_view details = "") const;
  };

}  

#endif  
