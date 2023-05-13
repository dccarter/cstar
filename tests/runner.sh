#!/usr/bin/env bash
# shellcheck disable=SC2181
# shellcheck disable=SC2231

#    a simple color library written in bash - themis
#    Copyright (C) 2021 lazypwny751
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.

# Reset
reset='\033[0m'           # Text Reset

# Regular Colors
black='\033[0;30m'        # Black
red='\033[0;31m'          # Red
green='\033[0;32m'        # Green
yellow='\033[0;33m'       # Yellow
blue='\033[0;34m'         # Blue
purple='\033[0;35m'       # Purple
cyan='\033[0;36m'         # Cyan
white='\033[0;37m'        # White

# Bold
Bblack='\033[1;30m'       # Black
Bred='\033[1;31m'         # Red
Bgreen='\033[1;32m'       # Green
Byellow='\033[1;33m'      # Yellow
Bblue='\033[1;34m'        # Blue
Bpurple='\033[1;35m'      # Purple
Bcyan='\033[1;36m'        # Cyan
Bwhite='\033[1;37m'       # White

# Underline
Ublack='\033[4;30m'       # Black
Ured='\033[4;31m'         # Red
Ugreen='\033[4;32m'       # Green
Uyellow='\033[4;33m'      # Yellow
Ublue='\033[4;34m'        # Blue
Upurple='\033[4;35m'      # Purple
Ucyan='\033[4;36m'        # Cyan
Uwhite='\033[4;37m'       # White

# Background
BGblack='\033[40m'        # Black
BGred='\033[41m'          # Red
BGgreen='\033[42m'        # Green
BGyellow='\033[43m'       # Yellow
BGblue='\033[44m'         # Blue
BGpurple='\033[45m'       # Purple
BGcyan='\033[46m'         # Cyan
BGwhite='\033[47m'        # White

# High Intensity
Iblack='\033[0;90m'       # Black
Ired='\033[0;91m'         # Red
Igreen='\033[0;92m'       # Green
Iyellow='\033[0;93m'      # Yellow
Iblue='\033[0;94m'        # Blue
Ipurple='\033[0;95m'      # Purple
Icyan='\033[0;96m'        # Cyan
Iwhite='\033[0;97m'       # White

# Bold High Intensity
BIblack='\033[1;90m'      # Black
BIred='\033[1;91m'        # Red
BIgreen='\033[1;92m'      # Green
BIyellow='\033[1;93m'     # Yellow
BIblue='\033[1;94m'       # Blue
BIpurple='\033[1;95m'     # Purple
BIcyan='\033[1;96m'       # Cyan
BIwhite='\033[1;97m'      # White

# High Intensity backgrounds
BGIblack='\033[0;100m'   # Black
BGIred='\033[0;101m'     # Red
BGIgreen='\033[0;102m'   # Green
BGIyellow='\033[0;103m'  # Yellow
BGIblue='\033[0;104m'    # Blue
BGIpurple='\033[0;105m'  # Purple
BGIcyan='\033[0;106m'    # Cyan
BGIwhite='\033[0;107m'   # White

# lolbash is a function that offers random colors similar to lolcat
# Usage: echo -e "$(randomcolor) hello${reset}"

randomcolor() {
    case ${1} in
        simple)
            echo -ne "\e[3$(( $RANDOM * 6 / 32767 + 1 ))m"
        ;;
        cool)
            local bold=$(( $RANDOM % 2 ))
            local code=$(( 30 + $RANDOM % 8 ))
            printf "%d;%d\n" $bold $code
        ;;
    esac
}

# lolbash is a function that offers random colors similar to lolcat for every character(s)
# Usage: echo -e "$(lolbash) hello${reset}"

lolbash() {
	sentence="$*"
	for (( i=0; i<${#sentence}; i++ )); do
	    printf "\e[%sm%c" "$(random_colour)" "${sentence:i:1}"
	done
	echo -e '\e[0m'
}

[ $(echo $BASH_VERSION | egrep -Eo '[[:digit:]]' | head -n1) -lt 4 ] && {
  echo -e "${Bred}error${reset}: unsupported bash version, please install at least version 4"
  if [[ "${OSTYPE}" =~ "darwin" ]]
  then
    echo -e "${Bblue}note:${reset} install bash by running 'brew install bash'"
  fi
  exit 1
}

timestamp() {
  if [[ "${OSTYPE}" =~ "darwin" ]]
  then
    if hash gdate 2>/dev/null; then
        gdate +%s%N
    else
        date +%s
    fi
  else
    date +%s%N
  fi
}

[ -z "${TESTS_DIR}" ] && TESTS_DIR=../tests
[ -z "${CXY_COMPILER}" ] && CXY_COMPILER=./cxy
[ -z "${CXY_STDLIB}" ] && CXY_STDLIB=./stdlib

POSITIONAL_ARGS=()
while [[ $# -gt 0 ]]; do
  case $1 in
    -u|--update-snapshot)
      UPDATE_SNAPSHOT=1
      shift # past argument
      ;;
    -f|--test-filter)
      TEST_FILTER="$2"
      shift # past argument
      shift # past value
      ;;
    --default)
      DEFAULT=YES
      shift # past argument
      ;;
    -*|--*)
      echo "Unknown option $1"
      exit 1
      ;;
    *)
      POSITIONAL_ARGS+=("$1") # save positional arg
      shift # past argument
      ;;
  esac
done

run_test () {
  expected="${1%.cxy}.expected"
  test_case="${2}"

  echo -e "Running test case ${test_case}"
  output=$(${CXY_COMPILER} dev "${1}" --print-ast --no-color --clean-ast)
  [ $? -ne 0 ] && {
      echo -e "  ${Bred}FAILED${reset}: Compilation failed for test case ${test_case}"
      echo -e "${output}"
      return 1
  }
  if [ -n "${UPDATE_SNAPSHOT}" ]
  then
    # Update snapshots
    echo "${output}" > ${expected}
  fi

  if [ -f $expected ] ; then
    match=$(diff <(echo "${output}") <(cat "${expected}") -U1 --label "${1}" --label "${expected}")
    [ $? -ne 0 ] && {
        echo -e "  ${Bred}FAILED${reset}: AST output does not match expected output"
        echo -e "${match}"
        return 1
    }
  fi

  return 0
}

LANG_TESTS=${TESTS_DIR}/lang
TESTS_START=$(timestamp)
FAILED_TESTS=0
PASSED_TESTS=0
declare -A TEST_RESULTS
declare -A TEST_DURATIONS
for test in ${LANG_TESTS}/*.cxy ; do
  if [ -n ${TEST_FILTER} ] && ! echo $test | grep -Eq "${TEST_FILTER}"
  then
    continue
  fi

  test_case=$(basename -- "${test}")
  test_case=${test_case%.cxy}
  run_test "${test}" "${test_case}"
  status=$?
  test_start=$(timestamp)
  if [ $status -ne 0 ]
  then
    FAILED_TESTS=$((FAILED_TESTS + 1))
    TEST_RESULTS+=(["${test_case}"]="FAILED")
  else
    PASSED_TESTS=$((PASSED_TESTS + 1))
    TEST_RESULTS+=(["${test_case}"]="PASSED")
  fi
  test_end=$(timestamp)
  TEST_DURATIONS+=(["${test_case}"]=$(((test_end - test_start)/1000000)))
done
TESTS_DONE=$(timestamp)

echo -e "----------------------------------------------------------"
echo -en "Results: "
if [ ${FAILED_TESTS} -eq 0 ]; then
  echo -e "${Bgreen}PASSED${reset}"
else
  echo -e "${Bred}FAILED${reset}"
fi

for test in "${!TEST_RESULTS[@]}"; do
  status=${TEST_RESULTS[$test]}
  duration=${TEST_DURATIONS[$test]}
  if [ "${status}" = "PASSED" ]; then
    echo -ne "${Bgreen}"
    printf "  \xE2\x9C\x94 "
    echo -e "${reset}  ${test} ... ${Bgreen}${status}${reset} (${duration} ms)"
  else
    echo -ne "${Bred}"
    printf "  \xE2\x9C\x98 "
    echo -e "${reset}  ${test} ... ${Bred}${status}${reset} (${duration} ms)"
  fi
done

duration=$(((TESTS_DONE - TESTS_START)/1000000))
echo
echo -e "Summary"
echo -en "  Passed: ${Bgreen}${PASSED_TESTS}${reset} Failed: "
[ "${FAILED_TESTS}" -ne 0 ] && echo -en "${Bred}"
echo -e "${FAILED_TESTS}${reset}"
echo -e "  Elapsed ${duration} ms"
echo -e "----------------------------------------------------------"
[ "${FAILED_TESTS}" -ne 0 ] && exit 1
exit 0

