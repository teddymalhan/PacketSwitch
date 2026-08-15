.PHONY: install coverage test demo demo-start demo-stop demo-status demo-windows docs help
.DEFAULT_GOAL := help

define BROWSER_PYSCRIPT
import os, webbrowser, sys

try:
	from urllib import pathname2url
except:
	from urllib.request import pathname2url

webbrowser.open("file://" + pathname2url(os.path.abspath(sys.argv[1])))
endef
export BROWSER_PYSCRIPT

define MAKE_HELP_ENTRIES
test|run tests quickly with ctest
demo|open the full multi-window live demo
demo-start|start the persistent demo stack
demo-stop|stop and remove the demo stack
demo-status|show demo container status
demo-windows|open VSwitch, VPort, and traffic windows in Otty
coverage|check code coverage quickly GCC
docs|generate Doxygen HTML documentation, including API docs
install|install the package to the `INSTALL_LOCATION`
format|format the project sources
endef
export MAKE_HELP_ENTRIES

define PRINT_HELP_PYSCRIPT
import os

for entry in os.environ["MAKE_HELP_ENTRIES"].splitlines():
	target, help = entry.split("|", 1)
	print("%-20s %s" % (target, help))
endef
export PRINT_HELP_PYSCRIPT

BROWSER := python -c "$$BROWSER_PYSCRIPT"
INSTALL_LOCATION := ~/.local

help:
	@python -c "$$PRINT_HELP_PYSCRIPT" < $(MAKEFILE_LIST)

test:
	rm -rf build/
	cmake -Bbuild -DCMAKE_INSTALL_PREFIX=$(INSTALL_LOCATION) -Dmodern-cpp-template_ENABLE_UNIT_TESTING=1 -DCMAKE_BUILD_TYPE="Release"
	cmake --build build --config Release
	cd build/ && ctest -C Release -VV

demo: demo-windows

demo-start:
	./tests/demo_stack.sh start

demo-stop:
	./tests/demo_stack.sh stop

demo-status:
	./tests/demo_stack.sh status

demo-windows:
	./tests/open_demo_terminals.sh

coverage:
	rm -rf build/
	cmake -Bbuild -DCMAKE_INSTALL_PREFIX=$(INSTALL_LOCATION) -Dmodern-cpp-template_ENABLE_CODE_COVERAGE=1
	cmake --build build --config Release
	cd build/ && ctest -C Release -VV
	cd .. && (bash -c "find . -type f -name '*.gcno' -exec gcov -pb {} +" || true)

docs:
	rm -rf docs/
	rm -rf build/
	cmake -Bbuild -DCMAKE_INSTALL_PREFIX=$(INSTALL_LOCATION) -DProject_ENABLE_DOXYGEN=1
	cmake --build build --config Release
	cmake --build build --target doxygen-docs
	$(BROWSER) docs/html/index.html

install:
	rm -rf build/
	cmake -Bbuild -DCMAKE_INSTALL_PREFIX=$(INSTALL_LOCATION)
	cmake --build build --config Release
	cmake --build build --target install --config Release

format:
	rm -rf build/
	cmake -Bbuild -DCMAKE_INSTALL_PREFIX=$(INSTALL_LOCATION)
	cmake --build build --target clang-format
