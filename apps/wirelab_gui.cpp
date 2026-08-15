#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "project/wirelab_view_model.hpp"

int main(int argc, char* argv[])
{
  QGuiApplication application(argc, argv);
  QQmlApplicationEngine engine;
  project::WireLabViewModel viewModel;
  engine.rootContext()->setContextProperty("wirelab", &viewModel);
  engine.loadFromModule("WireLab", "Main");
  if (engine.rootObjects().isEmpty()) return 1;
  return application.exec();
}
