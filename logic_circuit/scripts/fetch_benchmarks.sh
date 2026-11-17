#!/usr/bin/env bash
#
# Fetch or generate ISCAS-85 / ISCAS-89 benchmark netlists
#

set -euo pipefail

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../benchmarks" && pwd)"
echo "Storing benchmark circuits in ${BENCH_DIR}"

BASE_URL="https://raw.githubusercontent.com/lsils/benchmarks/master/iscas85/bench"

BENCHMARKS=("c432.bench" "c880.bench" "c1355.bench")

for b in "${BENCHMARKS[@]}"; do
    TARGET="${BENCH_DIR}/${b}"
    if [ ! -f "${TARGET}" ]; then
        echo "Fetching ${b}..."
        curl -sSL --fail "${BASE_URL}/${b}" -o "${TARGET}" || {
            echo "Warning: failed to download ${b} from primary mirror. Trying secondary..."
            # Alternate fallback
            curl -sSL --fail "https://web.archive.org/web/20210125023927/http://www.eecs.umich.edu/~jhayes/iscas/${b}" -o "${TARGET}" || {
                echo "Could not download ${b}, skipping."
                rm -f "${TARGET}"
            }
        }
    else
        echo "Benchmark ${b} already present."
    fi
done

echo "Benchmark setup completed."
