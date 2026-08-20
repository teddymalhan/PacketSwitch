#include "wirelab/session.hpp"

#include <string>
#include <utility>

#include "session_detail.hpp"

namespace wirelab
{
  namespace
  {
    // The Qt form trimmed a rule name before storing it, so a name typed with a
    // stray leading or trailing space never became a second, near-identical rule.
    [[nodiscard]] std::string trimmed(const std::string& text)
    {
      constexpr const char* whitespace = " \t\n\v\f\r";
      const auto first = text.find_first_not_of(whitespace);
      if (first == std::string::npos)
        return {};
      const auto last = text.find_last_not_of(whitespace);
      return text.substr(first, last - first + 1);
    }
  }  // namespace

  void Session::add_policy(
      const std::string& name,
      const std::string& anomaly_type,
      const std::string& action,
      uint64_t rate_limit_pps)
  {
    PolicyRule rule;
    rule.name = trimmed(name);
    rule.rate_limit_packets_per_second = rate_limit_pps;
    if (!anomaly_type_from_display_name(anomaly_type, rule.anomaly_type)
        || !policy_action_from_display_name(action, rule.action))
    {
      set_status("Unknown anomaly type or policy action.");
      return;
    }
    const auto result = analysis_pipeline_.policies().add_rule(std::move(rule));
    if (!result)
    {
      set_status(std::string("Policy rejected: ") + to_string(result.error()));
      return;
    }
    // Reports the name as typed, not as stored, matching the Qt frontend.
    set_status("Policy " + name + " added.");
    rebuild_policy_rows();
  }

  void Session::remove_policy(const std::string& name)
  {
    if (!analysis_pipeline_.policies().remove_rule(name))
    {
      set_status("No policy named " + name + ".");
      return;
    }
    set_status("Policy " + name + " removed.");
    rebuild_policy_rows();
  }

  void Session::set_policy_enabled(const std::string& name, bool enabled)
  {
    if (!analysis_pipeline_.policies().set_enabled(name, enabled))
    {
      set_status("No policy named " + name + ".");
      return;
    }
    set_status("Policy " + name + " " + (enabled ? "enabled" : "disabled") + ".");
    rebuild_policy_rows();
  }

  void Session::release_enforcement(const std::string& port_id)
  {
    if (!analysis_pipeline_.release(port_id))
    {
      set_status("Port " + port_id + " is not under enforcement.");
      return;
    }
    set_status("Released enforcement on " + port_id + ".");
    // Releasing a lease restores the operator's own fault on the port, so both
    // the fault view and the enforcement view are stale until rebuilt.
    rebuild_fault_rows();
    rebuild_policy_rows();
    mark(SessionDirty::Telemetry);
  }

  void Session::rebuild_policy_rows()
  {
    policy_rules_.clear();
    for (const auto& rule : analysis_pipeline_.policies().rules())
    {
      policy_rules_.push_back(PolicyRow{ rule.name,
                                         rule.anomaly_type,
                                         rule.action,
                                         rule.enabled,
                                         rule.rate_limit_packets_per_second,
                                         analysis_pipeline_.policies().hit_count(rule.name) });
    }
    enforced_ports_.clear();
    for (const auto& action : analysis_pipeline_.enforcer().active())
    {
      enforced_ports_.push_back(
          EnforcedPortRow{ action.port_id, action.rule_name, action.kind, session_detail::enforcement_summary(action) });
    }
    mark(SessionDirty::Policies);
  }
}  // namespace wirelab
