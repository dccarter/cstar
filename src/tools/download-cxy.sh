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

reportError() {
  echo -e "${Bred}error${reset}: ${1}"
}

checkBackendToolChain() {
    if ! which llvm-config 2>&1 > /dev/null ; then
      reportError "'llvm-config' command not found, ensure llvm (version > 18) is installed and added to system path"
      exit -1
    fi

    llvm_version=$(llvm-config --version)
    llvm_version_major=${llvm_version%%.*}
    if [[ "${llvm_version_major}" -lt 17 ]]; then
       reportError "Installed LLVM '${llvm_version}' is likely unsupported, please upgrade to LLVM 18 or above"
      exit -1
    fi

    if ! which cmake 2>&1 > /dev/null ; then
      reportError "CMake is required to build cxy compiler binary, please install cmake"
      exit -1
    fi
}

INSTALL_DIR=${CXY_ROOT}
WORKING_DIR=`mktemp -d -t cxy-buildXXXXXX`
LOG_FILE=${WORKING_DIR}/build.log

cleanup() {
  rm -rf ${WORKING_DIR}
}

trap cleanup EXIT

buildFailure() {
    reportError $1
    cp -rf ${LOG_FILE} /tmp/cxy-build.log
    echo "Build command output saved at /tmp/cxy-build.log"
    echo "  Feel free to report an error at https://github.com/dccarter/cstar/issues with build log"
    rm -rf ${WORKING_DIR}
}

echo -e "${Bgreen}1. Checking backend toolchains...${reset}"
checkBackendToolChain

LLVM_ROOT_DIR=`llvm-config --prefix`
echo -e "${Bgreen}2. Downloading Cxy compiler sources from github...${reset}"
git clone --quiet https://github.com/dccarter/cstar.git ${WORKING_DIR}/compiler 2>&1 >> ${LOG_FILE}
result=$?
if [[ "${result}" -ne 0 ]] ; then
  buildFailure "Downloading cxy compiler failed..."
fi

echo -e "${Bgreen}3. Build downloaded compiler...${reset}"
cmake -S ${WORKING_DIR}/compiler -B ${WORKING_DIR}/build -DCMAKE_BUILD_TYPE=Debug \
  -DLLVM_ROOT_DIR=$(llvm-config --prefix) -DCMAKE_C_COMPILER=$(which clang) -DCMAKE_CXX_COMPILER=$(which clang++) \
  -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} 2>&1 >> ${LOG_FILE}
cmake --build ${WORKING_DIR}/build --config Debug --parallel `getconf _NPROCESSORS_ONLN` 2>&1 >> ${LOG_FILE}
result=$?
if [[ "${result}" -ne 0 ]] ; then
  buildFailure "Building cxy compiler failed..."
fi

echo -e "${Bgreen}3. Install cxy compiler to path: ${INSTALL_DIR}...${reset}"
cmake --install ${WORKING_DIR}/build 2>&1 >> ${LOG_FILE}
result=$?
if [[ "${result}" -ne 0 ]] ; then
  buildFailure "Installing cxy compiler failed..."
fi

cp -rf ${LOG_FILE} /tmp/cxy-build.log
rm -rf ${WORKING_DIR}
echo -e "${Bgreen}Done! Compiler successfully installed${reset}"
echo -e "${Bwhite}If not already done, consider adding cxy to system path as follows${reset}"
echo    " # Cxy configuration "
echo    " export CXY_ROOT=${INSTALL_DIR} "
echo    " export PATH=\"\${CXY_ROOT}/bin:\$PATH\""
echo    " "

