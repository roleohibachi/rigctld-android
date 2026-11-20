FROM ubuntu:24.04 AS native
RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf automake build-essential ca-certificates cmake git libtool pkg-config python3 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace

FROM native AS android
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl openjdk-17-jdk-headless unzip && rm -rf /var/lib/apt/lists/*
ENV ANDROID_HOME=/opt/android-sdk
ENV PATH=${ANDROID_HOME}/cmdline-tools/latest/bin:${PATH}
RUN mkdir -p "${ANDROID_HOME}/cmdline-tools" \
    && curl -fsSL https://dl.google.com/android/repository/commandlinetools-linux-15859902_latest.zip -o /tmp/sdk-tools.zip \
    && echo '4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583  /tmp/sdk-tools.zip' | sha256sum -c \
    && unzip -q /tmp/sdk-tools.zip -d "${ANDROID_HOME}/cmdline-tools" \
    && mv "${ANDROID_HOME}/cmdline-tools/cmdline-tools" "${ANDROID_HOME}/cmdline-tools/latest" \
    && rm /tmp/sdk-tools.zip \
    && yes | sdkmanager --licenses \
    && sdkmanager "platforms;android-35" "build-tools;35.0.0" "ndk;27.2.12479018" "cmake;3.22.1"
