#!/usr/bin/awk -f

BEGIN {
  reset = "\033[0m"
  dim = "\033[2m"
  bold = "\033[1m"
  red = "\033[1;31m"
  green = "\033[1;32m"
  yellow = "\033[1;33m"
  blue = "\033[1;34m"
  magenta = "\033[1;35m"
  cyan = "\033[1;36m"
}

function paint(pattern, color) {
  if (match($0, pattern)) {
    $0 = substr($0, 1, RSTART - 1) color substr($0, RSTART, RLENGTH) reset substr($0, RSTART + RLENGTH)
  }
}

{
  if ($0 ~ /Error:|error:|FAILED|Destination Host Unreachable|100% packet loss/) {
    print red $0 reset
    fflush()
    next
  }

  if ($0 ~ /\[Learn\]/) {
    paint("\\[Learn\\]", magenta)
  } else if ($0 ~ /\[Broadcasted to\]/) {
    paint("\\[Broadcasted to\\]", yellow)
  } else if ($0 ~ /\[Forwarded to\]/) {
    paint("\\[Forwarded to\\]", green)
  } else if ($0 ~ /\[Discarded\]/) {
    paint("\\[Discarded\\]", red)
  } else if ($0 ~ /Received frame|Sent to VSwitch|Forward to TAP device/) {
    paint("Received frame|Sent to VSwitch|Forward to TAP device", cyan)
  } else if ($0 ~ /Ready|Started|running/) {
    $0 = green $0 reset
  } else if ($0 ~ /^PING|^64 bytes/) {
    $0 = green $0 reset
  } else if ($0 ~ /^---|statistics|packets transmitted|^rtt /) {
    $0 = blue $0 reset
  } else if ($0 ~ /^\[VSwitch\]|^\[VPort\]/) {
    paint("^\\[(VSwitch|VPort)\\]", bold blue)
  } else {
    $0 = dim $0 reset
  }

  paint("[0-9a-f][0-9a-f](:[0-9a-f][0-9a-f]){5}", magenta)
  paint("10\\.1\\.1\\.(101|102)", cyan)
  paint("172\\.30\\.0\\.[0-9]+:[0-9]+", blue)

  print
  fflush()
}
