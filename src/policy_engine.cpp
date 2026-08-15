#include "project/policy_engine.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace project
{
  const char* to_string(PolicyAction action) noexcept
  {
    switch (action)
    {
      case PolicyAction::Allow:
        return "allow";
      case PolicyAction::Drop:
        return "drop";
      case PolicyAction::Mirror:
        return "mirror";
      case PolicyAction::RateLimit:
        return "rate_limit";
      case PolicyAction::Quarantine:
        return "quarantine";
      case PolicyAction::AlertOnly:
        return "alert_only";
    }
    return "unknown";
  }

  const char* to_string(PolicyConfigurationError error) noexcept
  {
    switch (error)
    {
      case PolicyConfigurationError::EmptyName:
        return "policy name must not be empty";
      case PolicyConfigurationError::DuplicateName:
        return "policy names must be unique";
      case PolicyConfigurationError::MissingRateLimit:
        return "rate-limit policies require a packet rate";
    }
    return "unknown policy configuration error";
  }

  expected<void, PolicyConfigurationError> PolicyEngine::set_rules(std::vector<PolicyRule> rules)
  {
    std::unordered_set<std::string> names;
    for (const auto& rule : rules)
    {
      const auto validation = validate(rule);
      if (!validation)
      {
        return unexpected{ validation.error() };
      }
      if (!names.insert(rule.name).second)
      {
        return unexpected{ PolicyConfigurationError::DuplicateName };
      }
    }

    rules_ = std::move(rules);
    hit_counts_.clear();
    return {};
  }

  expected<void, PolicyConfigurationError> PolicyEngine::add_rule(PolicyRule rule)
  {
    const auto validation = validate(rule);
    if (!validation)
    {
      return unexpected{ validation.error() };
    }
    if (std::any_of(rules_.begin(), rules_.end(), [&rule](const PolicyRule& existing) {
          return existing.name == rule.name;
        }))
    {
      return unexpected{ PolicyConfigurationError::DuplicateName };
    }

    rules_.push_back(std::move(rule));
    return {};
  }

  bool PolicyEngine::remove_rule(std::string_view name)
  {
    const auto iterator = std::find_if(rules_.begin(), rules_.end(), [name](const PolicyRule& rule) {
      return rule.name == name;
    });
    if (iterator == rules_.end())
    {
      return false;
    }

    hit_counts_.erase(iterator->name);
    rules_.erase(iterator);
    return true;
  }

  bool PolicyEngine::set_enabled(std::string_view name, bool enabled)
  {
    const auto iterator = std::find_if(rules_.begin(), rules_.end(), [name](const PolicyRule& rule) {
      return rule.name == name;
    });
    if (iterator == rules_.end())
    {
      return false;
    }

    iterator->enabled = enabled;
    return true;
  }

  std::vector<PolicyRule> PolicyEngine::rules() const
  {
    return rules_;
  }

  std::vector<PolicyDecision> PolicyEngine::evaluate(const std::vector<AnomalyEvent>& anomalies)
  {
    std::vector<PolicyDecision> decisions;
    decisions.reserve(anomalies.size());
    for (const auto& anomaly : anomalies)
    {
      const auto iterator = std::find_if(rules_.begin(), rules_.end(), [&anomaly](const PolicyRule& rule) {
        return rule.enabled && rule.anomaly_type == anomaly.type;
      });
      if (iterator == rules_.end())
      {
        continue;
      }

      const uint64_t hit_count = ++hit_counts_[iterator->name];
      decisions.push_back(
          { iterator->name, iterator->action, anomaly, hit_count, iterator->rate_limit_packets_per_second });
    }
    return decisions;
  }

  uint64_t PolicyEngine::hit_count(std::string_view name) const noexcept
  {
    const auto iterator = hit_counts_.find(std::string(name));
    return iterator == hit_counts_.end() ? 0 : iterator->second;
  }

  void PolicyEngine::reset() noexcept
  {
    hit_counts_.clear();
  }

  expected<void, PolicyConfigurationError> PolicyEngine::validate(const PolicyRule& rule) noexcept
  {
    if (rule.name.empty())
    {
      return unexpected{ PolicyConfigurationError::EmptyName };
    }
    if (rule.action == PolicyAction::RateLimit && rule.rate_limit_packets_per_second == 0)
    {
      return unexpected{ PolicyConfigurationError::MissingRateLimit };
    }
    return {};
  }
}
