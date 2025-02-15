FROM ubuntu:latest AS builder

# Install ZSH
RUN apt-get -y update && apt-get -y upgrade
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y cmake sqlite3 libsqlite3-dev \
                       libpq-dev postgresql postgresql-server-dev-all   \
                       uuid-dev openssl libssl-dev build-essential \
                       autoconf automake libtool curl make unzip wget \
                       openssh-server nano rsync gdb bsdmainutils net-tools \
                       libzmq3-dev git gcc g++ zsh sudo cppcheck clang libclang-dev jq clang-format \
                       ruby

# Add user and home directory
RUN useradd -rm -d /home/dev -s /bin/zsh -g root -G sudo -u 1001 dev
RUN echo "dev:dev123" | chpasswd
USER dev
WORKDIR /home/dev

RUN sh -c "$(wget https://raw.githubusercontent.com/robbyrussell/oh-my-zsh/master/tools/install.sh -O -)"
