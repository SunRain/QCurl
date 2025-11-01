#!/bin/bash
#
# run_benchmark_regression.sh - 本地运行性能回归检测
#
# 使用方法:
#   ./scripts/run_benchmark_regression.sh [baseline_branch] [iterations]
#
# 参数:
#   baseline_branch: 基线分支名（默认: master）
#   iterations: 基准测试迭代次数（默认: 100）
#
# 示例:
#   ./scripts/run_benchmark_regression.sh                # 与 master 比较
#   ./scripts/run_benchmark_regression.sh develop 500    # 与 develop 比较，500 次迭代
#

set -e

# 配置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
RESULTS_DIR="$PROJECT_ROOT/benchmark-results"

BASELINE_BRANCH="${1:-master}"
ITERATIONS="${2:-100}"
THRESHOLD="${3:-10}"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查依赖
check_dependencies() {
    log_info "检查依赖..."

    if ! command -v python3 &> /dev/null; then
        log_error "需要 Python 3"
        exit 1
    fi

    if ! command -v cmake &> /dev/null; then
        log_error "需要 CMake"
        exit 1
    fi
}

# 编译基准测试
build_benchmarks() {
    log_info "编译基准测试..."

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -DBUILD_BENCHMARKS=ON \
        -DBUILD_EXAMPLES=OFF

    cmake --build . --target all --parallel

    cd "$PROJECT_ROOT"
}

# 运行基准测试
run_benchmarks() {
    local output_dir="$1"
    log_info "运行基准测试（迭代次数: $ITERATIONS）..."

    mkdir -p "$output_dir"

    for bench in "$BUILD_DIR"/benchmarks/benchmark_*; do
        if [ -x "$bench" ]; then
            local name=$(basename "$bench")
            echo -n "  运行 $name..."

            if timeout 300 "$bench" -iterations "$ITERATIONS" > "$output_dir/${name}.txt" 2>&1; then
                echo -e " ${GREEN}✓${NC}"
            else
                echo -e " ${YELLOW}⚠${NC} (超时或失败)"
            fi
        fi
    done
}

# 解析结果
parse_results() {
    local results_dir="$1"
    local output_file="$2"

    log_info "解析结果..."

    python3 "$SCRIPT_DIR/parse_benchmark_results.py" \
        "$results_dir" \
        --output "$output_file"
}

# 比较结果
compare_results() {
    local baseline_file="$1"
    local current_file="$2"
    local output_file="$3"

    log_info "比较结果（阈值: ${THRESHOLD}%）..."

    python3 "$SCRIPT_DIR/compare_benchmarks.py" \
        "$baseline_file" \
        "$current_file" \
        --threshold "$THRESHOLD" \
        --output "$output_file" \
        --fail-on-regression || return 1

    return 0
}

main() {
    echo "=============================================="
    echo "QCurl 性能回归检测"
    echo "=============================================="
    echo ""
    echo "基线分支: $BASELINE_BRANCH"
    echo "迭代次数: $ITERATIONS"
    echo "阈值: ${THRESHOLD}%"
    echo ""

    check_dependencies

    # 创建结果目录
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    CURRENT_RESULTS="$RESULTS_DIR/current_$TIMESTAMP"
    BASELINE_RESULTS="$RESULTS_DIR/baseline_$TIMESTAMP"

    mkdir -p "$CURRENT_RESULTS" "$BASELINE_RESULTS"

    # 保存当前分支
    CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
    CURRENT_COMMIT=$(git rev-parse --short HEAD)

    log_info "当前分支: $CURRENT_BRANCH ($CURRENT_COMMIT)"

    # 检查是否有未提交的更改
    if ! git diff --quiet || ! git diff --staged --quiet; then
        log_warning "有未提交的更改，将暂存..."
        git stash push -m "benchmark-regression-$TIMESTAMP"
        STASHED=true
    else
        STASHED=false
    fi

    # === 运行当前版本的基准测试 ===
    log_info "====== 测试当前版本 ======"
    build_benchmarks
    run_benchmarks "$CURRENT_RESULTS"
    parse_results "$CURRENT_RESULTS" "$CURRENT_RESULTS/summary.json"

    # === 切换到基线分支并运行基准测试 ===
    log_info "====== 测试基线版本 ($BASELINE_BRANCH) ======"

    if ! git checkout "$BASELINE_BRANCH" 2>/dev/null; then
        log_error "无法切换到分支: $BASELINE_BRANCH"

        # 恢复暂存
        if [ "$STASHED" = true ]; then
            git stash pop
        fi
        exit 1
    fi

    BASELINE_COMMIT=$(git rev-parse --short HEAD)
    log_info "基线分支: $BASELINE_BRANCH ($BASELINE_COMMIT)"

    build_benchmarks
    run_benchmarks "$BASELINE_RESULTS"
    parse_results "$BASELINE_RESULTS" "$BASELINE_RESULTS/summary.json"

    # === 切换回原分支 ===
    log_info "切换回原分支: $CURRENT_BRANCH"
    git checkout "$CURRENT_BRANCH"

    # 恢复暂存
    if [ "$STASHED" = true ]; then
        log_info "恢复暂存的更改..."
        git stash pop
    fi

    # === 比较结果 ===
    log_info "====== 比较结果 ======"

    COMPARISON_FILE="$RESULTS_DIR/comparison_$TIMESTAMP.md"

    if compare_results \
        "$BASELINE_RESULTS/summary.json" \
        "$CURRENT_RESULTS/summary.json" \
        "$COMPARISON_FILE"; then

        log_success "未检测到性能回归！"
        RESULT=0
    else
        log_error "检测到性能回归！"
        RESULT=1
    fi

    # 显示报告
    echo ""
    echo "=============================================="
    echo "比较报告"
    echo "=============================================="
    cat "$COMPARISON_FILE"

    echo ""
    echo "=============================================="
    echo "文件位置:"
    echo "  当前结果: $CURRENT_RESULTS/summary.json"
    echo "  基线结果: $BASELINE_RESULTS/summary.json"
    echo "  比较报告: $COMPARISON_FILE"
    echo "=============================================="

    exit $RESULT
}

main "$@"
