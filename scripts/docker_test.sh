#!/bin/bash
set -e

# Help / Usage
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Run unit tests, integration tests, and benchmarks inside a Docker container."
    echo ""
    echo "Options:"
    echo "  -c, --cuda      Use NVIDIA CUDA base image and enable CUDA support."
    echo "  -s, --serve     Start a local Python HTTP server to view the report."
    echo "  -p, --port PORT Port for the HTTP server (default: 8080)."
    echo "  -n, --name NAME Container name (default: realtime-subtitler-test)."
    echo "  -x, --clean     Remove previous coverage directory."
    echo "  -h, --help      Show this help message."
    exit 1
}

# Defaults
CUDA=false
SERVE=false
PORT=8080
CONTAINER_NAME="realtime-subtitler-test"
CLEAN=false
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Parse Arguments
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -c|--cuda) CUDA=true ;;
        -s|--serve) SERVE=true ;;
        -p|--port) PORT="$2"; shift ;;
        -n|--name) CONTAINER_NAME="$2"; shift ;;
        -x|--clean) CLEAN=true ;;
        -h|--help) usage ;;
        *) echo "Unknown parameter passed: $1"; usage ;;
    esac
    shift
done

cd "$REPO_ROOT"

# Config
COVERAGE_DIR="coverage"
RESULTS_DIR="test_results"
BASE_IMAGE="ubuntu:24.04"
GGML_CUDA="OFF"
DOCKER_RUN_OPTS=()

# Force Legacy Builder
export DOCKER_BUILDKIT=0

# CUDA Setup
if [ "$CUDA" = true ]; then
    echo "[INFO] Enabling CUDA support..."
    BASE_IMAGE="nvidia/cuda:12.4.1-devel-ubuntu22.04"
    GGML_CUDA="ON"
    DOCKER_RUN_OPTS+=("--gpus" "all")
fi

# Cleanup
if [ "$CLEAN" = true ] && [ -d "$COVERAGE_DIR" ]; then
    echo "[INFO] Cleaning up coverage directory..."
    rm -rf "$COVERAGE_DIR"
fi

# 1. Build Docker Image
echo ""
echo "[STEP 1] Building Docker Image ($CONTAINER_NAME)..."
echo "         Base Image: $BASE_IMAGE"
echo "         CUDA:       $GGML_CUDA"

docker build -t "$CONTAINER_NAME" . \
    --target builder \
    --build-arg BASE_IMAGE="$BASE_IMAGE" \
    --build-arg BUILD_TYPE=Debug \
    --build-arg ENABLE_COVERAGE=ON \
    --build-arg GGML_CUDA="$GGML_CUDA"

# 2. Run Tests
echo ""
echo "[STEP 2] Running Tests & Benchmarks..."

CONTAINER_ID="${CONTAINER_NAME}-runner"

# Cleanup old container if exists
if docker ps -a --format '{{.Names}}' | grep -q "^${CONTAINER_ID}$"; then
    echo "[INFO] Removing existing container $CONTAINER_ID..."
    docker rm -f "$CONTAINER_ID" > /dev/null
fi

# Command to run inside container
TEST_COMMAND=$(cat <<EOF
    echo '--- Running Unit Tests ---';
    ./build/unit_tests --gtest_output=xml:unit_tests_report.xml > unit_tests_output.txt 2>&1
    if [ \$? -ne 0 ]; then cat unit_tests_output.txt; exit 1; fi
    cat unit_tests_output.txt
    
    echo '--- Running Integration Tests ---';
    ./build/integration_tests --gtest_output=xml:integration_tests_report.xml > integration_tests_output.txt 2>&1
    if [ \$? -ne 0 ]; then cat integration_tests_output.txt; exit 1; fi
    cat integration_tests_output.txt
    
    echo '--- Running Benchmarks ---';
    ./build/benchmarks > benchmark_results.txt
    cat benchmark_results.txt || exit 1;
    
    echo '--- Generating Coverage Report ---';
    mkdir -p coverage_out;
    lcov --capture --directory . --output-file coverage.info --no-external --ignore-errors mismatch,empty,negative,gcov --rc geninfo_unexecuted_blocks=1;
    lcov --remove coverage.info '/usr/*' '*/_deps/*' '*/tests/*' '*/build/*' --output-file coverage_filtered.info --ignore-errors unused,negative;
    genhtml coverage_filtered.info --output-directory coverage_out --ignore-errors source;
EOF
)

echo "Starting container $CONTAINER_ID..."
# Run container (without --rm so we can cp files)
docker run --name "$CONTAINER_ID" "${DOCKER_RUN_OPTS[@]}" "$CONTAINER_NAME" sh -c "$TEST_COMMAND"

if [ $? -ne 0 ]; then
    echo -e "\033[0;31m[ERROR] Tests failed inside the container.\033[0m"
    echo "Container $CONTAINER_ID kept for inspection."
    exit 1
fi

# 3. Extract Artifacts
echo ""
echo "[STEP 3] Extracting Artifacts..."

if [ -d "$COVERAGE_DIR" ]; then rm -rf "$COVERAGE_DIR"; fi
mkdir -p "$COVERAGE_DIR"

if [ -d "$RESULTS_DIR" ]; then rm -rf "$RESULTS_DIR"; fi
mkdir -p "$RESULTS_DIR"

docker cp "${CONTAINER_ID}:/app/coverage_out/." "$COVERAGE_DIR"
docker cp "${CONTAINER_ID}:/app/unit_tests_report.xml" "$RESULTS_DIR/"
docker cp "${CONTAINER_ID}:/app/integration_tests_report.xml" "$RESULTS_DIR/"
docker cp "${CONTAINER_ID}:/app/benchmark_results.txt" "$RESULTS_DIR/"
docker cp "${CONTAINER_ID}:/app/benchmark_results.xml" "$RESULTS_DIR/" 2>/dev/null || true
docker cp "${CONTAINER_ID}:/app/unit_tests_output.txt" "$RESULTS_DIR/"
docker cp "${CONTAINER_ID}:/app/integration_tests_output.txt" "$RESULTS_DIR/"

docker rm "$CONTAINER_ID" > /dev/null

echo -e "\033[0;32m[SUCCESS] Coverage report extracted to '$COVERAGE_DIR/'\033[0m"
echo -e "\033[0;32m[SUCCESS] Test results extracted to '$RESULTS_DIR/'\033[0m"

# 4. Generate Dashboard
echo ""
echo "[STEP 4] Generating Unified Dashboard..."
if [ -f "scripts/generate_dashboard.py" ]; then
    if command -v python3 &> /dev/null; then
        python3 scripts/generate_dashboard.py
    elif command -v python &> /dev/null; then
        python scripts/generate_dashboard.py
    else
        echo "[WARNING] Python not found. Skipping dashboard generation."
    fi
else
    echo "[WARNING] Dashboard script not found."
fi

# 5. Serve Report
if [ "$SERVE" = true ]; then
    echo ""
    echo -e "\033[0;36m[STEP 5] Serving Report on http://localhost:$PORT\033[0m"
    echo "Press Ctrl+C to stop the server."
    
    INNER_COVERAGE_DIR="$RESULTS_DIR/coverage"
    if [ -d "$INNER_COVERAGE_DIR" ]; then rm -rf "$INNER_COVERAGE_DIR"; fi
    cp -r "$COVERAGE_DIR" "$INNER_COVERAGE_DIR"
    
    if command -v python3 &> /dev/null; then
        python3 -m http.server "$PORT" --directory "$RESULTS_DIR"
    elif command -v python &> /dev/null; then
        python -m http.server "$PORT" --directory "$RESULTS_DIR"
    else
        echo "[WARNING] Python not found. Cannot serve report."
    fi
fi
