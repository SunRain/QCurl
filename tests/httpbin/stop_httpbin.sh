#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
用法：
  ./tests/httpbin/stop_httpbin.sh [--name <container>]

默认容器名从 tests/httpbin/httpbin.lock 读取（QCURL_HTTPBIN_CONTAINER_NAME），否则使用 qcurl-httpbin。
USAGE
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

container_name=""
if [[ -f "${SCRIPT_DIR}/httpbin.lock" ]]; then
  # shellcheck disable=SC1091
  source "${SCRIPT_DIR}/httpbin.lock"
  container_name="${QCURL_HTTPBIN_CONTAINER_NAME:-}"
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --name)
      container_name="$2"
      shift 2
      ;;
    *)
      echo "[httpbin] unknown arg: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z "${container_name}" ]]; then
  container_name="qcurl-httpbin"
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "[httpbin] docker not found" >&2
  exit 127
fi

echo "[httpbin] stopping container: ${container_name}"
docker rm -f "${container_name}" >/dev/null 2>&1 || true
