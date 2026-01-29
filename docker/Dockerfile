FROM debian:bookworm-slim

# 1. System dependencies
RUN apt-get update && apt-get install -y \
    curl wget python3 python3-pip python3-serial ca-certificates unzip \
    && rm -rf /var/lib/apt/lists/*

# 2. Filesystem Tools
RUN wget https://github.com/earlephilhower/mklittlefs/releases/download/4.1.0/x86_64-linux-gnu-mklittlefs-42acb97.tar.gz && \
    tar -xvf x86_64-linux-gnu-mklittlefs-42acb97.tar.gz && \
    find . -name mklittlefs -type f -exec mv {} /usr/local/bin/ \; && \
    rm -rf mklittlefs* x86_64-linux-gnu-mklittlefs*

RUN wget https://github.com/igrr/mkspiffs/releases/download/0.2.3/mkspiffs-0.2.3-arduino-esp32-linux64.tar.gz && \
    tar -xvf mkspiffs-0.2.3-arduino-esp32-linux64.tar.gz && \
    find . -name mkspiffs -type f -exec mv {} /usr/local/bin/ \; && \
    rm -rf mkspiffs*

# 3. Arduino Setup
RUN curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh -s 1.1.1
RUN arduino-cli config init && \
    arduino-cli config set board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json && \
    arduino-cli core update-index && \
    arduino-cli core install esp32:esp32@3.1.0
