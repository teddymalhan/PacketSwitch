# Alpine Linux Docker Test

## Quick Test Script

Since building in Docker is complex, here's how to test manually:

### Option 1: Copy executables to Alpine container

1. Build your executables on macOS:
```bash
cd /Users/teddymalhan/Documents/vpn/wirelab/build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

2. Run Alpine container with TAP support:
```bash
docker run -it --rm \
  --privileged \
  --cap-add=NET_ADMIN \
  --cap-add=SYS_ADMIN \
  alpine:latest sh
```

3. Install dependencies in Alpine:
```apk add --no-cache iproute2 tcpdump```

### Option 2: Build directly in Alpine

In Alpine container:
```bash
apk add --no-cache build-base cmake git linux-headers iproute2

# Mount your source code
docker run -it --rm -v /Users/teddymalhan/Documents/vpn/wirelab:/app \
  --privileged --cap-add=NET_ADMIN alpine:latest sh

cd /app
apk add --no-cache build-base cmake git linux-headers iproute2
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Then test VPort ↔ VSwitch!

