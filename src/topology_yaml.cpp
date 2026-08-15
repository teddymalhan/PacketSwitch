#include "project/topology.hpp"

#include <charconv>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace project
{
  namespace
  {
    enum class Section
    {
      None,
      Network,
      Nodes,
      Links
    };

    std::string trim(std::string value)
    {
      const auto first = value.find_first_not_of(" \t\r");
      if (first == std::string::npos)
      {
        return {};
      }
      const auto last = value.find_last_not_of(" \t\r");
      return value.substr(first, last - first + 1);
    }

    std::string without_comment(const std::string& line)
    {
      char quote = '\0';
      for (size_t index = 0; index < line.size(); ++index)
      {
        const char character = line[index];
        if ((character == '\'' || character == '"') && (index == 0 || line[index - 1] != '\\'))
        {
          quote = quote == '\0' ? character : (quote == character ? '\0' : quote);
        }
        else if (character == '#' && quote == '\0')
        {
          return line.substr(0, index);
        }
      }
      return line;
    }

    bool split_field(std::string text, std::string& key, std::string& value)
    {
      const auto separator = text.find(':');
      if (separator == std::string::npos)
      {
        return false;
      }
      key = trim(text.substr(0, separator));
      value = trim(text.substr(separator + 1));
      if (key.empty() || value.empty())
      {
        return false;
      }
      if ((value.front() == '\'' || value.front() == '"'))
      {
        if (value.size() < 2 || value.back() != value.front())
        {
          return false;
        }
        value = value.substr(1, value.size() - 2);
      }
      return !value.empty();
    }

    bool parse_flow_mapping(const std::string& input, std::unordered_map<std::string, std::string>& fields)
    {
      if (input.size() < 2 || input.front() != '{' || input.back() != '}')
      {
        return false;
      }

      std::stringstream mapping(input.substr(1, input.size() - 2));
      std::string field;
      while (std::getline(mapping, field, ','))
      {
        std::string key;
        std::string value;
        if (!split_field(field, key, value) || !fields.emplace(std::move(key), std::move(value)).second)
        {
          return false;
        }
      }
      return !fields.empty();
    }

    bool parse_latency(const std::string& text, std::chrono::milliseconds& latency)
    {
      uint64_t milliseconds = 0;
      const auto result = std::from_chars(text.data(), text.data() + text.size(), milliseconds);
      if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
      {
        return false;
      }
      latency = std::chrono::milliseconds(milliseconds);
      return true;
    }

    class TopologyYamlParser
    {
     public:
      explicit TopologyYamlParser(std::string_view yaml) : yaml_(yaml)
      {
      }

      expected<TopologyConfiguration, TopologyYamlError> parse()
      {
        std::istringstream stream{ std::string(yaml_) };
        std::string raw_line;
        while (std::getline(stream, raw_line))
        {
          const std::string line = without_comment(raw_line);
          if (trim(line).empty())
          {
            continue;
          }

          const size_t indent = line.find_first_not_of(" \t");
          if (indent == std::string::npos || line.substr(0, indent).find('\t') != std::string::npos)
          {
            return unexpected(TopologyYamlError::MalformedDocument);
          }
          const std::string content = line.substr(indent);

          if (indent == 0)
          {
            if (!flush_entry())
            {
              return unexpected(error_);
            }
            if (content == "network:")
            {
              if (has_network_)
              {
                return unexpected(TopologyYamlError::MalformedDocument);
              }
              section_ = Section::Network;
              has_network_ = true;
            }
            else if (content == "nodes:")
            {
              if (has_nodes_)
              {
                return unexpected(TopologyYamlError::MalformedDocument);
              }
              section_ = Section::Nodes;
              has_nodes_ = true;
            }
            else if (content == "links:")
            {
              if (has_links_)
              {
                return unexpected(TopologyYamlError::MalformedDocument);
              }
              section_ = Section::Links;
              has_links_ = true;
            }
            else
            {
              return unexpected(TopologyYamlError::InvalidTopLevelKey);
            }
            continue;
          }

          if (section_ == Section::Network)
          {
            std::string key;
            std::string value;
            if (indent != 2 || !split_field(content, key, value) || key != "name" || has_name_)
            {
              return unexpected(TopologyYamlError::InvalidNetworkField);
            }
            configuration_.name = std::move(value);
            has_name_ = true;
            continue;
          }

          if (section_ != Section::Nodes && section_ != Section::Links)
          {
            return unexpected(TopologyYamlError::MalformedDocument);
          }

          if (indent == 2 && content.size() >= 1 && content.front() == '-')
          {
            if (!flush_entry())
            {
              return unexpected(error_);
            }
            entry_section_ = section_;
            const std::string entry = trim(content.substr(1));
            if (!entry.empty() && !parse_flow_mapping(entry, entry_fields_))
            {
              return unexpected(section_ == Section::Nodes ? TopologyYamlError::InvalidNodeField : TopologyYamlError::InvalidLinkField);
            }
            has_entry_ = true;
            continue;
          }

          std::string key;
          std::string value;
          if (indent != 4 || !has_entry_ || !split_field(content, key, value) || !entry_fields_.emplace(std::move(key), std::move(value)).second)
          {
            return unexpected(section_ == Section::Nodes ? TopologyYamlError::InvalidNodeField : TopologyYamlError::InvalidLinkField);
          }
        }

        if (!flush_entry())
        {
          return unexpected(error_);
        }
        if (!has_network_)
        {
          return unexpected(TopologyYamlError::MissingNetwork);
        }
        if (!has_name_)
        {
          return unexpected(TopologyYamlError::MissingNetworkName);
        }
        if (!has_nodes_)
        {
          return unexpected(TopologyYamlError::MissingNodes);
        }
        if (!has_links_)
        {
          return unexpected(TopologyYamlError::MissingLinks);
        }
        return std::move(configuration_);
      }

     private:
      bool flush_entry()
      {
        if (!has_entry_)
        {
          return true;
        }
        has_entry_ = false;
        if (entry_section_ == Section::Nodes)
        {
          const auto id = entry_fields_.find("id");
          const auto type = entry_fields_.find("type");
          if (entry_fields_.size() != 2 || id == entry_fields_.end() || type == entry_fields_.end())
          {
            error_ = TopologyYamlError::InvalidNodeField;
            return false;
          }
          TopologyNodeType node_type;
          if (type->second == "host")
          {
            node_type = TopologyNodeType::Host;
          }
          else if (type->second == "switch")
          {
            node_type = TopologyNodeType::Switch;
          }
          else
          {
            error_ = TopologyYamlError::InvalidNodeType;
            return false;
          }
          configuration_.nodes.push_back({ id->second, node_type });
        }
        else
        {
          const auto from = entry_fields_.find("from");
          const auto to = entry_fields_.find("to");
          if (from == entry_fields_.end() || to == entry_fields_.end() || entry_fields_.size() > 3)
          {
            error_ = TopologyYamlError::InvalidLinkField;
            return false;
          }
          std::chrono::milliseconds latency{ 0 };
          const auto latency_ms = entry_fields_.find("latency_ms");
          if (latency_ms != entry_fields_.end() && !parse_latency(latency_ms->second, latency))
          {
            error_ = TopologyYamlError::InvalidLatency;
            return false;
          }
          configuration_.links.push_back({ from->second, to->second, latency });
        }
        entry_fields_.clear();
        return true;
      }

      std::string_view yaml_;
      TopologyConfiguration configuration_;
      Section section_ = Section::None;
      Section entry_section_ = Section::None;
      std::unordered_map<std::string, std::string> entry_fields_;
      TopologyYamlError error_ = TopologyYamlError::MalformedDocument;
      bool has_network_ = false;
      bool has_name_ = false;
      bool has_nodes_ = false;
      bool has_links_ = false;
      bool has_entry_ = false;
    };
  }

  const char* to_string(TopologyYamlError error) noexcept
  {
    switch (error)
    {
      case TopologyYamlError::FileRead: return "file_read";
      case TopologyYamlError::MalformedDocument: return "malformed_document";
      case TopologyYamlError::MissingNetwork: return "missing_network";
      case TopologyYamlError::MissingNetworkName: return "missing_network_name";
      case TopologyYamlError::MissingNodes: return "missing_nodes";
      case TopologyYamlError::MissingLinks: return "missing_links";
      case TopologyYamlError::InvalidTopLevelKey: return "invalid_top_level_key";
      case TopologyYamlError::InvalidNetworkField: return "invalid_network_field";
      case TopologyYamlError::InvalidNodeField: return "invalid_node_field";
      case TopologyYamlError::InvalidLinkField: return "invalid_link_field";
      case TopologyYamlError::InvalidNodeType: return "invalid_node_type";
      case TopologyYamlError::InvalidLatency: return "invalid_latency";
    }
    return "unknown";
  }

  expected<TopologyConfiguration, TopologyYamlError> topology_configuration_from_yaml(std::string_view yaml)
  {
    return TopologyYamlParser(yaml).parse();
  }

  expected<TopologyConfiguration, TopologyYamlError> topology_configuration_from_yaml_file(const std::string& path)
  {
    std::ifstream file(path);
    if (!file)
    {
      return unexpected(TopologyYamlError::FileRead);
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (!file.good() && !file.eof())
    {
      return unexpected(TopologyYamlError::FileRead);
    }
    return topology_configuration_from_yaml(contents.str());
  }
}
