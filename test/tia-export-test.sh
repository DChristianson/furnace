#!/bin/bash
# exports all files in test/export/ for testing.
# requires GNU parallel.

set -u

timestamp=$(date +%Y%m%d%H%M%S)

FURNACE_ROOT=".."
testDir="$FURNACE_ROOT/test"
sourceDir="$FURNACE_ROOT/demos"
templateDir="$FURNACE_ROOT/src/asm/6502/a2600"
declare -a summaryRows=()

add_summary_row() {
  local test_name="$1"
  local source_name="$2"
  local status="$3"
  local phase="$4"
  local norm_dist="$5"
  local details="$6"
  summaryRows+=("$test_name"$'\t'"$source_name"$'\t'"$status"$'\t'"$phase"$'\t'"$norm_dist"$'\t'"$details")
}

extract_normalized_distance() {
  local report_file="$1"
  local value
  if [[ ! -e "$report_file" ]]; then
    return 1
  fi
  value=$(grep -Eo 'normalized_edit_distance=[0-9.]+' "$report_file" | tail -n 1 | cut -d= -f2)
  if [[ -n "$value" ]]; then
    echo "$value"
    return 0
  fi
  return 1
}

repeat_char() {
  local count="$1"
  local ch="$2"
  if [[ "$count" -le 0 ]]; then
    echo -n ""
    return
  fi
  printf "%${count}s" "" | tr ' ' "$ch"
}

print_summary_table() {
  local h1="TEST"
  local h2="SOURCE"
  local h3="STATUS"
  local h4="PHASE"
  local h5="NORM_EDIT_DIST"
  local h6="DETAILS"
  local w1=${#h1}
  local w2=${#h2}
  local w3=${#h3}
  local w4=${#h4}
  local w5=${#h5}
  local w6=${#h6}

  local row
  for row in "${summaryRows[@]}"; do
    IFS=$'\t' read -r c1 c2 c3 c4 c5 c6 <<< "$row"
    [[ ${#c1} -gt $w1 ]] && w1=${#c1}
    [[ ${#c2} -gt $w2 ]] && w2=${#c2}
    [[ ${#c3} -gt $w3 ]] && w3=${#c3}
    [[ ${#c4} -gt $w4 ]] && w4=${#c4}
    [[ ${#c5} -gt $w5 ]] && w5=${#c5}
    [[ ${#c6} -gt $w6 ]] && w6=${#c6}
  done

  local sep="+"
  sep+="$(repeat_char $((w1+2)) '-')+"
  sep+="$(repeat_char $((w2+2)) '-')+"
  sep+="$(repeat_char $((w3+2)) '-')+"
  sep+="$(repeat_char $((w4+2)) '-')+"
  sep+="$(repeat_char $((w5+2)) '-')+"
  sep+="$(repeat_char $((w6+2)) '-')+"

  echo "$sep"
  printf "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n" \
    "$w1" "$h1" "$w2" "$h2" "$w3" "$h3" "$w4" "$h4" "$w5" "$h5" "$w6" "$h6"
  echo "$sep"

  for row in "${summaryRows[@]}"; do
    IFS=$'\t' read -r c1 c2 c3 c4 c5 c6 <<< "$row"
    printf "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n" \
      "$w1" "$c1" "$w2" "$c2" "$w3" "$c3" "$w4" "$c4" "$w5" "$c5" "$w6" "$c6"
  done
  echo "$sep"
}

is_furnace_crash() {
    local log_file="$1"
    local exit_code="$2"
    if [[ "$exit_code" -ge 128 ]]; then
        return 0
    fi
    if grep -Eiq 'segmentation fault|core dumped|stack trace|libc\+\+abi: terminating|abort trap|terminating due to|assertion.*failed' "$log_file"; then
        return 0
    fi
    return 1
}

has_export_data() {
    local target_dir="$1"
    [[ -s "$target_dir/RegisterDump.txt" ]]
}

make_error_hint() {
    local log_file="$1"
    local hint
    hint=$(grep -Ei '(^|: )(error|fatal|undefined|unresolved)|dasm' "$log_file" | head -n 1)
    if [[ -n "$hint" ]]; then
        echo "$hint"
    fi
}

while read -r line || [ -n "$line" ]; do
    [[ -z "$line" ]] && continue
    [[ "$line" =~ ^[[:space:]]*# ]] && continue

    testargs=($line)
    testname=${testargs[0]}
    sourceFile=${testargs[1]}
    romConf=${testargs[@]:2} 
    filename=$(find "$sourceDir" -name "$sourceFile")
    targetDir="$testDir/output/$timestamp/$testname"
    furnaceLog="$targetDir/furnace_export.log"
    makeLog="$targetDir/make.log"
    echo "processing $sourceFile -> $targetDir"
    if [[ ! -e "$filename" ]]; then 
      add_summary_row "$testname" "$sourceFile" "FAILED" "PREP" "n/a" "source file not found"
      continue;
    fi
    mkdir -p "$targetDir"
    cp -r "$templateDir"/* "$targetDir"
    "$FURNACE_ROOT"/build/Debug/furnace --romconf debug=true $romConf --romout "$targetDir" "$filename" > "$furnaceLog" 2>&1
    furnaceExit=$?
    (cd "$targetDir" && make) > "$makeLog" 2>&1
    makeExit=$?
    romFile=$targetDir/roms/MiniPlayer_NTSC.a26
    if [[ ! -e "$romFile" ]]; then 
      if is_furnace_crash "$furnaceLog" "$furnaceExit"; then
        add_summary_row "$testname" "$sourceFile" "FAILED" "EXPORT" "n/a" "furnace crash/corrupt output (see $furnaceLog)"
      elif has_export_data "$targetDir"; then
        if [[ "$makeExit" -ne 0 ]]; then
          hint=$(make_error_hint "$makeLog")
          if [[ -n "$hint" ]]; then
            add_summary_row "$testname" "$sourceFile" "FAILED" "COMPILE" "n/a" "DASM compile failed: $hint [log: $makeLog]"
          else
            add_summary_row "$testname" "$sourceFile" "FAILED" "COMPILE" "n/a" "DASM compile failed (see $makeLog)"
          fi
        else
          add_summary_row "$testname" "$sourceFile" "FAILED" "COMPILE" "n/a" "ROM missing unexpectedly (see $makeLog)"
        fi
      else
        add_summary_row "$testname" "$sourceFile" "FAILED" "EXPORT" "n/a" "no RegisterDump.txt (likely export/write failure) [log: $furnaceLog]"
      fi
      continue;
    fi
    stella -loglevel 2 -logtoconsole 1 -userdir . -debug "$targetDir"/roms/MiniPlayer_NTSC.a26 > "$targetDir"/stella.log.out
    python3 "$testDir"/diff_stella_log.py "$targetDir" > "$targetDir"/test.out
    if [ $? -ne 0 ]; then 
      normDist="n/a"
      parsedDist=$(extract_normalized_distance "$targetDir"/test.out)
      if [[ -n "$parsedDist" ]]; then
        normDist="$parsedDist"
      fi
      add_summary_row "$testname" "$sourceFile" "FAILED" "STELLA" "$normDist" "register sequence mismatch (see $targetDir/test.out)"
      continue;
    fi
    normDist="0.000000"
    parsedDist=$(extract_normalized_distance "$targetDir"/test.out)
    if [[ -n "$parsedDist" ]]; then
      normDist="$parsedDist"
    fi
    add_summary_row "$testname" "$sourceFile" "SUCCESS" "STELLA" "$normDist" "ok"
done < $testDir/tests.conf

print_summary_table
