# Contributing to WireLab

Thank you for helping improve WireLab. Bug reports, feature suggestions,
documentation fixes, tests, and code changes are welcome.

## Before you start

Search the [existing issues](https://github.com/teddymalhan/WireLab/issues)
before opening a new one. For a larger change, open an issue first so the
approach can be discussed before implementation.

Good places to start are issues labeled
[`good first issue`](https://github.com/teddymalhan/WireLab/labels/good%20first%20issue)
or [`help wanted`](https://github.com/teddymalhan/WireLab/labels/help%20wanted).
Leave a comment on the issue you intend to address to avoid duplicated work.

## Reporting a bug

Use the [bug report template](https://github.com/teddymalhan/WireLab/issues/new?template=bug_report.md).
Include:

- your operating system, compiler, and CMake versions;
- the command you ran;
- the expected and actual behavior;
- the smallest reliable reproduction;
- relevant logs or error output.

Do not include credentials, private network details, or other sensitive data.

## Suggesting a feature

Use the [feature request template](https://github.com/teddymalhan/WireLab/issues/new?template=feature_request.md).
Describe the networking problem or use case, the proposed behavior, and any
alternatives you considered.

## Development setup

WireLab requires CMake 3.15 or newer and a C++17 compiler. Linux provides
native TAP support. macOS uses `utun`; tests that require Linux networking
should be run through Docker.

Clone and configure a development build:

```bash
git clone https://github.com/teddymalhan/WireLab.git
cd WireLab
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWireLab_ENABLE_UNIT_TESTING=ON
cmake --build build --parallel
```

See the [README](README.md) for installation and usage instructions and
[`tests/TESTING.md`](tests/TESTING.md) for the complete testing guide.

## Making a change

1. Create a focused branch from `main`.
2. Keep the change limited to one issue or one root cause.
3. Match the style and abstractions in the surrounding code.
4. Add or update tests for changed behavior.
5. Update user-facing documentation when commands or behavior change.
6. Format and test the change before opening a pull request.

Prefer modern C++17 and RAII. Avoid introducing a new dependency when the
standard library or an existing project abstraction is sufficient. Follow the
[C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
where they do not conflict with established project conventions.

## Formatting

The repository's `.clang-format` file is the source of truth. Format all project
sources with:

```bash
make format
```

To format only files you changed, run `clang-format -i` on each affected
`.cpp` or `.hpp` file.

## Testing

Build and run the unit and integration tests:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWireLab_ENABLE_UNIT_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For memory-safety checks, use AddressSanitizer:

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWireLab_ENABLE_UNIT_TESTING=ON \
  -DWireLab_ENABLE_ASAN=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

For the Docker end-to-end scenario:

```bash
./tests/test_in_docker.sh
```

Run the checks relevant to your change. If a check cannot be run locally, state
that explicitly in the pull request.

## Pull requests

Open a pull request against `main` and use the
[pull request template](.github/PULL_REQUEST_TEMPLATE.md). A pull request
should:

- link its issue, using `Fixes #123` when it should close that issue;
- explain the problem and why the chosen solution is appropriate;
- list the exact validation commands and results;
- remain small enough to review as one coherent change;
- pass the relevant GitHub Actions checks;
- contain no unrelated formatting or refactoring.

Maintainers may request changes. Keep review responses and follow-up commits
focused on the feedback. Pull requests are normally squash-merged after
approval.

## License

By contributing, you agree that your contribution is licensed under the
project's [MIT License](LICENSE).
