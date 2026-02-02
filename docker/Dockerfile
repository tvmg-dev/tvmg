FROM debian:bookworm-slim

# 1. System dependencies
RUN apt-get update && apt-get install -y \
    curl wget python3 python3-pip python3-serial ca-certificates unzip patch \
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
    arduino-cli core install esp32:esp32@3.1.0 && \
    rm -rf /root/.arduino15/staging/packages && \
    chmod 711 /root && chmod -R a+rX /root/.arduino15

# 4. Apply Patches
# Using the framework root path verified in our test
COPY patches/ /tmp/patches/

RUN cd /root/.arduino15/packages/esp32/hardware/esp32/3.1.0 && \
    patch -p0 < /tmp/patches/esp-asyncudp.patch && \
    patch -p0 < /tmp/patches/esp-network.patch && \
    rm -rf /tmp/patches

RUN mkdir -p /working && chmod 777 /working
WORKDIR /working
