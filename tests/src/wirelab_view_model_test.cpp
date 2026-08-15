#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "project/wirelab_view_model.hpp"

namespace project
{
  namespace
  {
    std::string scenario_path()
    {
      return (std::filesystem::path(PROJECT_SOURCE_DIRECTORY) / "scenarios" / "security-lab.yaml").string();
    }
  }

  TEST(WireLabViewModelTest, ExercisesTopologyTrafficTelemetryAndFaultWorkflow)
  {
    WireLabViewModel model;
    model.openTopology(QString::fromStdString(scenario_path()));

    ASSERT_TRUE(model.hasTopology());
    EXPECT_EQ(model.topologyNodes().size(), 4);
    EXPECT_EQ(model.topologyLinks().size(), 3);

    model.selectNode(QStringLiteral("client-a"));
    EXPECT_EQ(model.selectedType(), QStringLiteral("node"));
    model.applySelectedFault(25, 0.0, false);
    ASSERT_EQ(model.activeFaults().size(), 1);

    model.startTraffic(QStringLiteral("mixed-traffic"), 64, 128, 42, QStringLiteral("CPU"));
    ASSERT_TRUE(model.trafficRunning());
    model.runTrafficStep();
    model.stopTraffic();

    ASSERT_EQ(model.metricsHistory().size(), 1);
    EXPECT_FALSE(model.packetRows().isEmpty());
    EXPECT_FALSE(model.macTable().isEmpty());
    ASSERT_EQ(model.portStates().size(), 3);
    EXPECT_TRUE(model.trafficResult().contains(QStringLiteral("64 packets generated")));

    model.clearFault(QStringLiteral("client-a"), QString());
    EXPECT_TRUE(model.activeFaults().isEmpty());
  }

  TEST(WireLabViewModelTest, EditsValidTopologyAndRejectsInvalidChanges)
  {
    WireLabViewModel model;
    model.openTopology(QString::fromStdString(scenario_path()));

    model.addNode(QStringLiteral("client-c"), QStringLiteral("host"));
    ASSERT_EQ(model.topologyNodes().size(), 5);
    model.addLink(QStringLiteral("client-c"), QStringLiteral("core-switch"), 3);
    EXPECT_EQ(model.topologyLinks().size(), 4);

    model.addNode(QStringLiteral("core-switch"), QStringLiteral("switch"));
    EXPECT_EQ(model.topologyNodes().size(), 5);
    EXPECT_TRUE(model.statusMessage().contains(QStringLiteral("validation failed"), Qt::CaseInsensitive));

    model.selectNode(QStringLiteral("client-c"));
    model.removeSelected();
    EXPECT_EQ(model.topologyNodes().size(), 4);
    EXPECT_EQ(model.topologyLinks().size(), 3);
  }
}
