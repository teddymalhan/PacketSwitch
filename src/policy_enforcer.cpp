#include "wirelab/policy_enforcer.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

namespace wirelab
{
  const char* to_string(EnforcementKind kind) noexcept
  {
    switch (kind)
    {
      case EnforcementKind::None: return "none";
      case EnforcementKind::RateLimit: return "rate-limit";
      case EnforcementKind::Blackhole: return "blackhole";
      case EnforcementKind::Isolate: return "isolate";
    }
    return "unknown";
  }

  const char* to_string(EnforcementOutcome outcome) noexcept
  {
    switch (outcome)
    {
      case EnforcementOutcome::Applied: return "applied";
      case EnforcementOutcome::Extended: return "extended";
      case EnforcementOutcome::Released: return "released";
      case EnforcementOutcome::Skipped: return "skipped";
      case EnforcementOutcome::UnknownPort: return "unknown-port";
      case EnforcementOutcome::Rejected: return "rejected";
    }
    return "unknown";
  }

  PolicyEnforcer::PolicyEnforcer(PolicyEnforcerConfig config) noexcept : config_(config)
  {
  }

  EnforcementKind PolicyEnforcer::kind_for(PolicyAction action) noexcept
  {
    switch (action)
    {
      case PolicyAction::Drop: return EnforcementKind::Blackhole;
      case PolicyAction::Quarantine: return EnforcementKind::Isolate;
      case PolicyAction::RateLimit: return EnforcementKind::RateLimit;
      case PolicyAction::Allow:
      case PolicyAction::Mirror:
      case PolicyAction::AlertOnly: return EnforcementKind::None;
    }
    return EnforcementKind::None;
  }

  unsigned PolicyEnforcer::severity(EnforcementKind kind) noexcept
  {
    switch (kind)
    {
      case EnforcementKind::None: return 0;
      case EnforcementKind::RateLimit: return 1;
      case EnforcementKind::Blackhole: return 2;
      case EnforcementKind::Isolate: return 3;
    }
    return 0;
  }

  FaultConfiguration
  PolicyEnforcer::overlay(FaultConfiguration base, EnforcementKind kind, uint64_t rate_limit_bits_per_second) noexcept
  {
    switch (kind)
    {
      case EnforcementKind::RateLimit: base.bandwidth_bits_per_second = rate_limit_bits_per_second; break;
      case EnforcementKind::Blackhole: base.blackhole = true; break;
      case EnforcementKind::Isolate: base.isolated = true; break;
      case EnforcementKind::None: break;
    }
    return base;
  }

  uint64_t PolicyEnforcer::rate_limit_bits(const PolicyDecision& decision) const noexcept
  {
    if (decision.rate_limit_packets_per_second == 0)
    {
      return 0;
    }
    const uint64_t mean_frame_bytes = decision.anomaly.observed_packets == 0
                                          ? config_.assumed_frame_bytes
                                          : decision.anomaly.observed_bytes / decision.anomaly.observed_packets;
    const uint64_t frame_bytes = mean_frame_bytes == 0 ? config_.assumed_frame_bytes : mean_frame_bytes;
    return decision.rate_limit_packets_per_second * frame_bytes * 8U;
  }

  std::chrono::nanoseconds PolicyEnforcer::lease_for(EnforcementKind kind) const noexcept
  {
    switch (kind)
    {
      case EnforcementKind::RateLimit: return config_.rate_limit_lease;
      case EnforcementKind::Blackhole: return config_.drop_lease;
      case EnforcementKind::Isolate: return config_.quarantine_lease;
      case EnforcementKind::None: break;
    }
    return std::chrono::nanoseconds{ 0 };
  }

  std::vector<EnforcementAction> PolicyEnforcer::apply(
      const std::vector<PolicyDecision>& decisions,
      TopologyController& controller,
      std::chrono::steady_clock::time_point now)
  {
    std::vector<EnforcementAction> results;
    results.reserve(decisions.size());
    for (const auto& decision : decisions)
    {
      EnforcementAction result;
      result.rule_name = decision.rule_name;
      result.anomaly_type = decision.anomaly.type;
      result.action = decision.action;
      result.kind = kind_for(decision.action);
      result.observed_packets = decision.anomaly.observed_packets;
      result.threshold = decision.anomaly.threshold;

      const auto port = controller.port_id_at(decision.anomaly.ingress_port);
      if (port)
      {
        result.port_id = *port;
      }

      if (result.kind == EnforcementKind::None)
      {
        result.outcome = EnforcementOutcome::Skipped;
        results.push_back(std::move(result));
        continue;
      }
      if (!port)
      {
        result.outcome = EnforcementOutcome::UnknownPort;
        results.push_back(std::move(result));
        continue;
      }

      result.rate_limit_bits_per_second = result.kind == EnforcementKind::RateLimit ? rate_limit_bits(decision) : 0;
      if (result.kind == EnforcementKind::RateLimit && result.rate_limit_bits_per_second == 0)
      {
        result.outcome = EnforcementOutcome::Rejected;
        results.push_back(std::move(result));
        continue;
      }

      const auto existing = leases_.find(*port);
      const bool has_lease = existing != leases_.end();
      // A weaker action never downgrades a live lease; it only extends it.
      const bool supersedes = !has_lease || severity(result.kind) >= severity(existing->second.action.kind);
      const EnforcementKind effective_kind = supersedes ? result.kind : existing->second.action.kind;
      const uint64_t effective_bits = supersedes && result.kind == EnforcementKind::RateLimit
                                          ? result.rate_limit_bits_per_second
                                          : (has_lease ? existing->second.action.rate_limit_bits_per_second : 0);

      Lease lease;
      if (has_lease)
      {
        lease = existing->second;
      }
      else
      {
        const auto previous = controller.port_fault(*port);
        lease.had_restore = previous.has_value();
        lease.restore = previous.value_or(FaultConfiguration{});
      }

      const auto configuration = overlay(lease.restore, effective_kind, effective_bits);
      if (!controller.set_port_fault(*port, configuration))
      {
        result.outcome = EnforcementOutcome::Rejected;
        results.push_back(std::move(result));
        continue;
      }

      result.kind = effective_kind;
      result.rate_limit_bits_per_second = effective_bits;
      result.expires_at = now + lease_for(effective_kind);
      // Re-triggering while a lease is live extends it rather than stacking.
      if (has_lease && result.expires_at < existing->second.action.expires_at)
      {
        result.expires_at = existing->second.action.expires_at;
      }
      result.outcome = has_lease ? EnforcementOutcome::Extended : EnforcementOutcome::Applied;

      lease.action = result;
      leases_[*port] = std::move(lease);
      results.push_back(std::move(result));
    }
    return results;
  }

  void PolicyEnforcer::restore_port(const Lease& lease, TopologyController& controller) const
  {
    if (lease.had_restore)
    {
      static_cast<void>(controller.set_port_fault(lease.action.port_id, lease.restore));
      return;
    }
    static_cast<void>(controller.clear_port_fault(lease.action.port_id));
  }

  std::vector<EnforcementAction> PolicyEnforcer::release_expired(
      TopologyController& controller,
      std::chrono::steady_clock::time_point now)
  {
    std::vector<EnforcementAction> released;
    for (auto iterator = leases_.begin(); iterator != leases_.end();)
    {
      if (iterator->second.action.expires_at > now)
      {
        ++iterator;
        continue;
      }
      restore_port(iterator->second, controller);
      auto action = iterator->second.action;
      action.outcome = EnforcementOutcome::Released;
      released.push_back(std::move(action));
      iterator = leases_.erase(iterator);
    }
    std::sort(
        released.begin(),
        released.end(),
        [](const EnforcementAction& left, const EnforcementAction& right) { return left.port_id < right.port_id; });
    return released;
  }

  bool PolicyEnforcer::release(std::string_view port_id, TopologyController& controller)
  {
    const auto iterator = leases_.find(std::string(port_id));
    if (iterator == leases_.end())
    {
      return false;
    }
    restore_port(iterator->second, controller);
    leases_.erase(iterator);
    return true;
  }

  std::vector<EnforcementAction> PolicyEnforcer::active() const
  {
    std::vector<EnforcementAction> actions;
    actions.reserve(leases_.size());
    for (const auto& [port_id, lease] : leases_)
    {
      static_cast<void>(port_id);
      actions.push_back(lease.action);
    }
    std::sort(
        actions.begin(),
        actions.end(),
        [](const EnforcementAction& left, const EnforcementAction& right)
        { return std::tie(left.port_id, left.rule_name) < std::tie(right.port_id, right.rule_name); });
    return actions;
  }

  bool PolicyEnforcer::is_enforced(std::string_view port_id) const
  {
    return leases_.find(std::string(port_id)) != leases_.end();
  }

  void PolicyEnforcer::forget() noexcept
  {
    leases_.clear();
  }
}  // namespace wirelab
