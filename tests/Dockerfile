FROM alpine:3.22

RUN apk add --no-cache \
    bash \
    build-base \
    coreutils \
    cmake \
    iproute2 \
    iputils \
    linux-headers

WORKDIR /app

COPY CMakeLists.txt Makefile .clang-format .cursorrules ./
COPY cmake/ ./cmake/
COPY include/ ./include/
COPY src/ ./src/

RUN cmake -S . -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DProject_ENABLE_UNIT_TESTING=OFF && \
    cmake --build build --parallel && \
    test -x build/vport && \
    test -x build/vswitch

CMD ["/bin/sh"]
