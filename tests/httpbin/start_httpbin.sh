#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
用法：
  ./tests/httpbin/start_httpbin.sh [--port <host_port|0>] [--write-env <path>] [--name <container>] [--image-ref <ref>]

说明：
  - 默认使用动态端口（--port 0），避免固定 8935 冲突
  - 镜像默认从 tests/httpbin/httpbin.lock 读取（image@sha256:<digest>）
  - 启动后会执行健康检查（GET /get 返回 200 且为 JSON）
  - 成功后会写出 env 文件（默认 build/test-env/httpbin.env），其中包含 QCURL_HTTPBIN_URL

示例：
  ./tests/httpbin/start_httpbin.sh --write-env build/test-env/httpbin.env
  source build/test-env/httpbin.env
USAGE
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

write_env="${REPO_ROOT}/build/test-env/httpbin.env"
host_port="0"

image_ref=""
container_name=""

if [[ -f "${SCRIPT_DIR}/httpbin.lock" ]]; then
  # shellcheck disable=SC1091
  source "${SCRIPT_DIR}/httpbin.lock"
  image_ref="${QCURL_HTTPBIN_IMAGE_REF:-}"
  container_name="${QCURL_HTTPBIN_CONTAINER_NAME:-}"
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --write-env)
      write_env="$2"
      shift 2
      ;;
    --port)
      host_port="$2"
      shift 2
      ;;
    --name)
      container_name="$2"
      shift 2
      ;;
    --image-ref)
      image_ref="$2"
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
if [[ -z "${image_ref}" ]]; then
  echo "[httpbin] missing image ref: please set QCURL_HTTPBIN_IMAGE_REF in tests/httpbin/httpbin.lock or pass --image-ref" >&2
  exit 2
fi

if ! [[ "${host_port}" =~ ^[0-9]+$ ]] || (( host_port < 0 || host_port > 65535 )); then
  echo "[httpbin] invalid --port: ${host_port}" >&2
  exit 2
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "[httpbin] docker not found" >&2
  exit 127
fi
if ! command -v curl >/dev/null 2>&1; then
  echo "[httpbin] curl not found (required for health check)" >&2
  exit 127
fi

echo "[httpbin] starting container: name=${container_name} image=${image_ref} host_port=${host_port}"
docker rm -f "${container_name}" >/dev/null 2>&1 || true

docker run -d \
  --name "${container_name}" \
  -p "127.0.0.1:${host_port}:80" \
  "${image_ref}" >/dev/null

mapped="$(docker port "${container_name}" 80/tcp | head -n 1 || true)"
resolved_port="$(echo "${mapped}" | grep -Eo '[0-9]+$' || true)"
if [[ -z "${resolved_port}" ]]; then
  echo "[httpbin] failed to resolve published port from: ${mapped}" >&2
  docker logs "${container_name}" || true
  docker rm -f "${container_name}" >/dev/null 2>&1 || true
  exit 1
fi

base_url="http://127.0.0.1:${resolved_port}"
echo "[httpbin] resolved QCURL_HTTPBIN_URL=${base_url}"

echo "[httpbin] health check: GET ${base_url}/get"
ok="0"
last_body=""
for _ in $(seq 1 60); do
  set +e
  last_body="$(curl -fsS --max-time 2 "${base_url}/get" 2>/dev/null)"
  rc=$?
  set -e
  if [[ ${rc} -eq 0 ]] && python3 - <<PY <<<"${last_body}" >/dev/null 2>&1
import json, sys
json.loads(sys.stdin.read())
PY
  then
    ok="1"
    break
  fi
  sleep 0.2
done

if [[ "${ok}" != "1" ]]; then
  echo "[httpbin] health check failed" >&2
  if [[ -n "${last_body}" ]]; then
    echo "[httpbin] last response body (truncated):" >&2
    echo "${last_body}" | head -c 2000 >&2 || true
    echo >&2
  fi
  docker logs "${container_name}" || true
  docker rm -f "${container_name}" >/dev/null 2>&1 || true
  exit 1
fi

mkdir -p "$(dirname "${write_env}")"
cat > "${write_env}" <<EOF
export QCURL_HTTPBIN_URL="${base_url}"
export QCURL_HTTPBIN_CONTAINER_NAME="${container_name}"
export QCURL_HTTPBIN_IMAGE_REF="${image_ref}"
EOF

echo "[httpbin] env written: ${write_env}"
echo "[httpbin] next: source ${write_env}"
