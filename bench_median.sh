#!/bin/bash
# ============================================================
# bench_median.sh - 多次运行基准测试并取中位数
# 用途：VM 环境下单次性能测试波动大（纯 CPU 场景可达 ±70%），
#       本脚本运行 N 次取中位数，得到可写入文档/简历的稳健数值。
#
# 用法：
#   ./bench_median.sh              # 默认 5 次，场景 1 2 4 6（约 1 分钟）
#   ./bench_median.sh 3            # 3 次
#   ./bench_median.sh 5 1 2        # 5 次，只跑场景 1 和 2
#   ./bench_median.sh 3 6          # 3 次，只跑纯前端场景
# ============================================================
set -e
cd "$(dirname "$0")"

N="${1:-5}"
shift || true
SCENES="${@:-1 2 4 6}"

[ -x output/logger_benchmark ] || { echo "Building benchmark..."; ./build.sh benchmark; }

median() {
    sort -n | awk '{a[NR]=$1} END {
        if (NR == 0) { print "N/A"; exit }
        if (NR % 2 == 1) printf "%.2f\n", a[(NR+1)/2]
        else printf "%.2f\n", (a[NR/2] + a[NR/2+1]) / 2
    }'
}

echo "============================================================"
echo "  Median Benchmark: $N 次 × 场景 [$SCENES]"
echo "============================================================"

for scene in $SCENES; do
    case "$scene" in
        1) label="B1 单线程吞吐";  pattern="Throughput";;
        2) label="B2 10线程吞吐";  pattern="Throughput";;
        4) label="B4 50线程吞吐";  pattern="Throughput";;
        6) label="B6 纯前端吞吐";  pattern="Frontend throughput";;
        *) echo "跳过未知场景: $scene"; continue;;
    esac

    vals=""
    for i in $(seq 1 "$N"); do
        v=$(cd output && ./logger_benchmark "$scene" 2>/dev/null \
            | grep -E "$pattern" | tail -1 | grep -oE '[0-9.]+')
        vals="$vals$v"$'\n'
    done

    med=$(printf "%s" "$vals" | median)
    lo=$(printf "%s" "$vals" | sort -n | head -1)
    hi=$(printf "%s" "$vals" | sort -n | tail -1)
    printf "%-20s 中位数: %12s  范围: [%s ~ %s] logs/sec\n" "$label" "$med" "$lo" "$hi"
done

echo "============================================================"
echo "Done! 建议将中位数 + 范围写入 README（标注 VM 环境波动）"
echo "============================================================"
