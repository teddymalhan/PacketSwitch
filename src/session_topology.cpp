#include "wirelab/session.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <utility>

#include "session_detail.hpp"
#include "wirelab/topology.hpp"

namespace wirelab
{
  namespace
  {
    // Node and link ids come straight out of a text field, so a stray space a
    // person typed must never become part of the id.
    [[nodiscard]] std::string trimmed(std::string_view text)
    {
      const auto is_space = [](char character) noexcept
      {
        return character == ' ' || character == '\t' || character == '\n' || character == '\v' || character == '\f' ||
               character == '\r';
      };
      size_t begin = 0;
      while (begin < text.size() && is_space(text[begin]))
        ++begin;
      size_t end = text.size();
      while (end > begin && is_space(text[end - 1]))
        --end;
      return std::string(text.substr(begin, end - begin));
    }

    // The node type arrives from a form, so "Switch" and "switch" have to mean
    // the same thing.
    [[nodiscard]] bool equals_ignore_case(std::string_view left, std::string_view right) noexcept
    {
      return left.size() == right.size() &&
             std::equal(
                 left.begin(),
                 left.end(),
                 right.begin(),
                 [](char first, char second) noexcept {
                   return std::tolower(static_cast<unsigned char>(first)) ==
                          std::tolower(static_cast<unsigned char>(second));
                 });
    }
  }  // namespace

  bool Session::commit_topology(TopologyConfiguration configuration, const std::string& success_message)
  {
    auto topology = Topology::create(configuration);
    if (!topology)
    {
      set_status(std::string("Topology validation failed: ") + to_string(topology.error()));
      return false;
    }
    stop_traffic();
    topology_configuration_ = std::move(configuration);
    topology_controller_.load(std::move(topology.value()));
    clear_selection();
    reset_simulation();
    rebuild_topology_rows();
    rebuild_fault_rows();
    set_status(success_message);
    mark(SessionDirty::Topology);
    return true;
  }

  void Session::open_topology(const std::string& path)
  {
    const std::string local_path = session_detail::strip_file_url(path);
    const auto configuration = topology_configuration_from_yaml_file(local_path);
    if (!configuration)
    {
      set_status(std::string("Open topology failed: ") + to_string(configuration.error()));
      return;
    }
    topology_path_ = local_path;
    (void)commit_topology(configuration.value(), "Loaded " + configuration.value().name);
  }

  void Session::save_topology(const std::string& path)
  {
    if (!has_topology())
    {
      set_status("There is no topology to save.");
      return;
    }
    const std::string local_path = session_detail::strip_file_url(path);
    std::ofstream output(local_path, std::ios::trunc);
    if (!output)
    {
      // The path as the caller spelled it, which is what they recognise.
      set_status("Could not open " + path + " for writing.");
      return;
    }
    output << "network:\n  name: " << session_detail::yaml_quote(topology_configuration_.name) << "\n\nnodes:\n";
    for (const auto& node : topology_configuration_.nodes)
      output << "  - { id: " << session_detail::yaml_quote(node.id)
             << ", type: " << (node.type == TopologyNodeType::Switch ? "switch" : "host") << " }\n";
    output << "\nlinks:\n";
    for (const auto& link : topology_configuration_.links)
      output << "  - { from: " << session_detail::yaml_quote(link.from) << ", to: " << session_detail::yaml_quote(link.to)
             << ", latency_ms: " << link.latency.count() << " }\n";
    if (!output)
    {
      set_status("Writing " + local_path + " failed.");
      return;
    }
    topology_path_ = local_path;
    set_status("Saved topology to " + session_detail::file_name_of(local_path));
  }

  void Session::rebuild_topology_rows()
  {
    topology_nodes_.clear();
    topology_links_.clear();
    size_t host_index = 0;
    const auto host_count = static_cast<size_t>(std::count_if(
        topology_configuration_.nodes.begin(),
        topology_configuration_.nodes.end(),
        [](const TopologyNode& node) { return node.type == TopologyNodeType::Host; }));
    constexpr double pi = 3.14159265358979323846;
    for (const auto& node : topology_configuration_.nodes)
    {
      // The switch sits at the centre; hosts ring it, starting at twelve
      // o'clock so a two-host lab reads left-to-right.
      double x = 0.5;
      double y = 0.5;
      if (node.type == TopologyNodeType::Host && host_count != 0)
      {
        const double angle = (2.0 * pi * static_cast<double>(host_index) / static_cast<double>(host_count)) - pi / 2.0;
        x = 0.5 + 0.38 * std::cos(angle);
        y = 0.5 + 0.38 * std::sin(angle);
        ++host_index;
      }
      topology_nodes_.push_back(NodeRow{ node.id, node.type, x, y });
    }
    for (const auto& link : topology_configuration_.links)
      topology_links_.push_back(LinkRow{ link.from, link.to, static_cast<int64_t>(link.latency.count()) });
  }

  void Session::select_node(const std::string& id)
  {
    const auto found = std::find_if(
        topology_configuration_.nodes.begin(),
        topology_configuration_.nodes.end(),
        [&id](const TopologyNode& node) { return node.id == id; });
    if (found == topology_configuration_.nodes.end())
      return;
    selection_kind_ = SelectionKind::Node;
    selected_id_ = id;
    selected_first_ = id;
    selected_second_.clear();
    const auto connected = std::count_if(
        topology_configuration_.links.begin(),
        topology_configuration_.links.end(),
        [&found](const TopologyLink& link) { return link.from == found->id || link.to == found->id; });
    selected_summary_ = std::string(to_string(found->type)) + " node · " + std::to_string(connected) + " connected link(s)";
    mark(SessionDirty::Selection);
  }

  void Session::select_link(const std::string& from, const std::string& to)
  {
    const auto found = std::find_if(
        topology_configuration_.links.begin(),
        topology_configuration_.links.end(),
        [&from, &to](const TopologyLink& link)
        { return (link.from == from && link.to == to) || (link.from == to && link.to == from); });
    if (found == topology_configuration_.links.end())
      return;
    selection_kind_ = SelectionKind::Link;
    selected_id_ = from + " ↔ " + to;
    selected_first_ = found->from;
    selected_second_ = found->to;
    selected_summary_ = std::to_string(found->latency.count()) + " ms base latency";
    mark(SessionDirty::Selection);
  }

  void Session::clear_selection()
  {
    selection_kind_ = SelectionKind::None;
    selected_id_.clear();
    selected_first_.clear();
    selected_second_.clear();
    selected_summary_.clear();
    mark(SessionDirty::Selection);
  }

  void Session::add_node(const std::string& id, const std::string& type)
  {
    TopologyConfiguration edited = topology_configuration_;
    // A node added to an empty session still needs a network name to validate.
    if (edited.name.empty())
      edited.name = "untitled-lab";
    const std::string trimmed_id = trimmed(id);
    edited.nodes.push_back(
        { trimmed_id, equals_ignore_case(type, "switch") ? TopologyNodeType::Switch : TopologyNodeType::Host });
    (void)commit_topology(std::move(edited), "Added node " + trimmed_id);
  }

  void Session::add_link(const std::string& from, const std::string& to, int32_t latency_ms)
  {
    TopologyConfiguration edited = topology_configuration_;
    const std::string trimmed_from = trimmed(from);
    const std::string trimmed_to = trimmed(to);
    edited.links.push_back({ trimmed_from, trimmed_to, std::chrono::milliseconds(latency_ms) });
    (void)commit_topology(std::move(edited), "Added link " + trimmed_from + " ↔ " + trimmed_to);
  }

  void Session::remove_selected()
  {
    if (selection_kind_ == SelectionKind::None)
      return;
    TopologyConfiguration edited = topology_configuration_;
    if (selection_kind_ == SelectionKind::Node)
    {
      // A node cannot outlive its links, so they go with it.
      const auto& id = selected_first_;
      edited.nodes.erase(
          std::remove_if(
              edited.nodes.begin(), edited.nodes.end(), [&id](const TopologyNode& node) { return node.id == id; }),
          edited.nodes.end());
      edited.links.erase(
          std::remove_if(
              edited.links.begin(),
              edited.links.end(),
              [&id](const TopologyLink& link) { return link.from == id || link.to == id; }),
          edited.links.end());
    }
    else
    {
      const auto& first = selected_first_;
      const auto& second = selected_second_;
      edited.links.erase(
          std::remove_if(
              edited.links.begin(),
              edited.links.end(),
              [&first, &second](const TopologyLink& link)
              { return (link.from == first && link.to == second) || (link.from == second && link.to == first); }),
          edited.links.end());
    }
    // Built before the commit: committing clears the selection this names.
    const std::string message = "Removed " + selected_id_;
    (void)commit_topology(std::move(edited), message);
  }

  void Session::apply_selected_fault(int32_t latency_ms, double loss_percent, bool blackhole)
  {
    if (selection_kind_ == SelectionKind::None)
    {
      set_status("Select a host or link before applying a fault.");
      return;
    }
    FaultConfiguration configuration;
    configuration.latency = std::chrono::milliseconds(latency_ms);
    configuration.loss_basis_points = static_cast<uint32_t>(std::lround(loss_percent * 100.0));
    configuration.blackhole = blackhole;
    const auto result = selection_kind_ == SelectionKind::Link
                            ? topology_controller_.set_link_fault(selected_first_, selected_second_, configuration)
                            : topology_controller_.set_port_fault(selected_first_, configuration);
    set_status(
        result ? "Applied fault to " + selected_id_
               : std::string("Apply fault failed: ") + to_string(result.error()));
    rebuild_fault_rows();
  }

  void Session::clear_fault(const std::string& first_endpoint, const std::string& second_endpoint)
  {
    const bool cleared = second_endpoint.empty() ? topology_controller_.clear_port_fault(first_endpoint)
                                                 : topology_controller_.clear_link_fault(first_endpoint, second_endpoint);
    set_status(cleared ? "Cleared fault." : "The selected fault was no longer active.");
    rebuild_fault_rows();
  }

  void Session::rebuild_fault_rows()
  {
    active_faults_.clear();
    const auto faults = topology_controller_.active_faults();
    if (faults)
    {
      for (const auto& fault : faults.value())
      {
        active_faults_.push_back(FaultRow{
            fault.first_endpoint,
            fault.second_endpoint,
            static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(fault.configuration.latency).count()),
            static_cast<double>(fault.configuration.loss_basis_points) / 100.0,
            fault.configuration.blackhole });
      }
    }
    mark(SessionDirty::Faults);
  }
}  // namespace wirelab
