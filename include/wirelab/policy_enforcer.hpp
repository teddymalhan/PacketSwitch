#ifndef PROJECT_POLICY_ENFORCER_HPP_
#define PROJECT_POLICY_ENFORCER_HPP_

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "wirelab/policy_engine.hpp"
#include "wirelab/topology_controller.hpp"

namespace wirelab
{
  // How a policy action is realised on the dataplane. Allow, Mirror and
  // AlertOnly are observational, so they map to None and change no forwarding.
  enum class EnforcementKind
  {
    None,
    RateLimit,
    Blackhole,
    Isolate
  };

  enum class EnforcementOutcome
  {
    Applied,
    Extended,
    Released,
    Skipped,
    UnknownPort,
    Rejected
  };

  struct EnforcementAction
  {
    std::string port_id;
    std::string rule_name;
    AnomalyType anomaly_type = AnomalyType::BroadcastStorm;
    PolicyAction action = PolicyAction::AlertOnly;
    EnforcementKind kind = EnforcementKind::None;
    EnforcementOutcome outcome = EnforcementOutcome::Skipped;
    uint64_t rate_limit_bits_per_second = 0;
    uint64_t observed_packets = 0;
    uint64_t threshold = 0;
    std::chrono::steady_clock::time_point expires_at{};
  };

  struct PolicyEnforcerConfig
  {
    std::chrono::nanoseconds rate_limit_lease{ std::chrono::seconds{ 10 } };
    std::chrono::nanoseconds drop_lease{ std::chrono::seconds{ 5 } };
    std::chrono::nanoseconds quarantine_lease{ std::chrono::seconds{ 30 } };
    // Used to convert a packets/sec limit into a bandwidth cap when an anomaly
    // reports no observed bytes to derive a mean frame size from.
    uint64_t assumed_frame_bytes = 64;
  };

  // Turns policy decisions into leased faults on the offending ingress port.
  //
  // Enforcement is always reversible: the operator's own fault configuration for
  // a port is captured before the first enforced fault is applied and restored
  // when the lease expires, so quarantining a port never silently discards a
  // manually configured latency or loss profile.
  //
  // A stronger action supersedes a weaker one on the same port; a weaker action
  // arriving while a stronger lease is live only extends that lease.
  class PolicyEnforcer
  {
   public:
    explicit PolicyEnforcer(PolicyEnforcerConfig config = {}) noexcept;

    [[nodiscard]] std::vector<EnforcementAction> apply(
        const std::vector<PolicyDecision>& decisions,
        TopologyController& controller,
        std::chrono::steady_clock::time_point now);
    [[nodiscard]] std::vector<EnforcementAction> release_expired(
        TopologyController& controller,
        std::chrono::steady_clock::time_point now);
    [[nodiscard]] bool release(std::string_view port_id, TopologyController& controller);
    [[nodiscard]] std::vector<EnforcementAction> active() const;
    [[nodiscard]] bool is_enforced(std::string_view port_id) const;
    // Drops lease bookkeeping without touching the controller. Only correct when
    // the controller's faults are being discarded too, such as a topology reload.
    void forget() noexcept;

   private:
    struct Lease
    {
      EnforcementAction action;
      FaultConfiguration restore;
      bool had_restore = false;
    };

    [[nodiscard]] static EnforcementKind kind_for(PolicyAction action) noexcept;
    [[nodiscard]] static unsigned severity(EnforcementKind kind) noexcept;
    [[nodiscard]] static FaultConfiguration
    overlay(FaultConfiguration base, EnforcementKind kind, uint64_t rate_limit_bits_per_second) noexcept;
    [[nodiscard]] uint64_t rate_limit_bits(const PolicyDecision& decision) const noexcept;
    [[nodiscard]] std::chrono::nanoseconds lease_for(EnforcementKind kind) const noexcept;
    void restore_port(const Lease& lease, TopologyController& controller) const;

    PolicyEnforcerConfig config_;
    std::unordered_map<std::string, Lease> leases_;
  };

  [[nodiscard]] const char* to_string(EnforcementKind kind) noexcept;
  [[nodiscard]] const char* to_string(EnforcementOutcome outcome) noexcept;
}  // namespace wirelab

#endif
