#!/bin/bash

set -e
set -u
set -o pipefail

export PATH=/data/fengdahu/npusim/release/cmake-3.31.3-linux-x86_64/bin:$PATH

SCRIPT_NAME=$(basename "$0")
SCRIPT_DIR_ABS=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

LLM_TEST_DIR_ABS="${SCRIPT_DIR_ABS}"
BUILD_DIR_ABS=$(cd "${SCRIPT_DIR_ABS}/../../build" && pwd)

NPUSIM_STDOUT_TMP_BASENAME="npusim_output.tmp.$$"
TEMP_JSON_CONFIG_BASENAME="temp_config.json"
BUILD_LOG_BASENAME="build.log"

NPUSIM_STDOUT_TMP_FULL_PATH="${BUILD_DIR_ABS}/${NPUSIM_STDOUT_TMP_BASENAME}"
TEMP_JSON_CONFIG_FULL_PATH="${LLM_TEST_DIR_ABS}/${TEMP_JSON_CONFIG_BASENAME}"
BUILD_LOG_FULL_PATH="${BUILD_DIR_ABS}/${BUILD_LOG_BASENAME}"

cleanup() {
    echo "INFO: Cleaning up temporary files..."
    rm -f "${NPUSIM_STDOUT_TMP_FULL_PATH}"
    rm -f "${TEMP_JSON_CONFIG_FULL_PATH}"
    rm -f "${BUILD_LOG_FULL_PATH}"
}

trap cleanup EXIT INT TERM

if [ "$#" -ne 1 ]; then
    echo "Usage: ./${SCRIPT_NAME} <test_batch_file>"
    echo "Example: ./${SCRIPT_NAME} ./test_batch.txt"
    exit 1
fi

TEST_BATCH_FILE="$1"
OUTPUT_BATCH_FILE="${LLM_TEST_DIR_ABS}/${TEST_BATCH_FILE}.results"

if [ ! -f "${TEST_BATCH_FILE}" ]; then
    echo "ERROR: Test batch file '${TEST_BATCH_FILE}' not found."
    exit 1
fi

LINE_NUM=0
# 逐行读取
while IFS= read -r line || [[ -n "$line" ]]; do
    LINE_NUM=$((LINE_NUM + 1))
    echo ""
    echo "INFO: Processing line ${LINE_NUM}: ${line}"

    IFS_ORIG="$IFS"
    IFS=$'\t'

    unset IFS
    read -r NPUSIM_MAIN_CONFIG_FILE JSON_X JSON_CORE_ID JSON_EXU_X JSON_EXU_Y JSON_SFU_X JSON_SRAM_BITWIDTH <<<"$line"
    IFS="$IFS_ORIG"

    if [ -z "${NPUSIM_MAIN_CONFIG_FILE}" ] ||
        [ -z "${JSON_X}" ] ||
        [ -z "${JSON_CORE_ID}" ] ||
        [ -z "${JSON_EXU_X}" ] ||
        [ -z "${JSON_EXU_Y}" ] ||
        [ -z "${JSON_SFU_X}" ] ||
        [ -z "${JSON_SRAM_BITWIDTH}" ]; then
        echo "ERROR: Line ${LINE_NUM} does not contain 7 parameters. Content: '${line}'"
        echo -e "${line}\tERROR: Invalid parameter count" >>"${OUTPUT_BATCH_FILE}"
        continue
    fi

    CONFIG_CONTENT=$(
        cat <<EOF
{
    "x": ${JSON_X},
    "cores": [
        {
            "id": ${JSON_CORE_ID},
            "exu_x": ${JSON_EXU_X},
            "exu_y": ${JSON_EXU_Y},
            "sfu_x": ${JSON_SFU_X},
            "sram_bitwidth": ${JSON_SRAM_BITWIDTH}
        }
    ]
}
EOF
    )

    echo "$CONFIG_CONTENT" >"${TEMP_JSON_CONFIG_FULL_PATH}"
    echo "INFO: ${TEMP_JSON_CONFIG_FULL_PATH} created successfully for line ${LINE_NUM}."

    NPUSIM_MAIN_CONFIG_PATH_ARG="../llm/test/${NPUSIM_MAIN_CONFIG_FILE}"
    NPUSIM_CORE_CONFIG_PATH_ARG="../llm/test/${TEMP_JSON_CONFIG_BASENAME}"

    ORIGINAL_PWD=$(pwd)
    cd "${BUILD_DIR_ABS}"

    echo "INFO: Running npusim for line ${LINE_NUM}..."
    ./npusim --config-file="${NPUSIM_MAIN_CONFIG_PATH_ARG}" \
        --core-config-file="${NPUSIM_CORE_CONFIG_PATH_ARG}" \
        >"${NPUSIM_STDOUT_TMP_BASENAME}"

    RESULT_STRING=""
    echo "INFO: npusim ran successfully for line ${LINE_NUM}."

    RECORD_FOUND=0
    while IFS= read -r line_catch; do
        if [[ "$line_catch" == "[CATCH TEST]"* ]]; then
            # 提取 "[CATCH TEST]" 後面的剩余內容，去除空格
            remaining_content="${line_catch#"[CATCH TEST]"}"
            remaining_content="${remaining_content##+([ \t])}"

            RESULT_STRING="${RESULT_STRING}${RESULT_STRING:+, }${remaining_content}"
            RECORD_FOUND=1
            break
        fi
    done < <(tac "${NPUSIM_STDOUT_TMP_BASENAME}")

    if [ $RECORD_FOUND -eq 0 ]; then
        echo "WARNING: No [CATCH TEST] string found in npusim output for line ${LINE_NUM}."
    fi

    echo -e "${line}\t${RESULT_STRING}" >>"${OUTPUT_BATCH_FILE}"
    cd "${ORIGINAL_PWD}"
done <"${TEST_BATCH_FILE}"

echo ""
echo "INFO: Batch processing complete. Results are in ${OUTPUT_BATCH_FILE}"
exit 0
