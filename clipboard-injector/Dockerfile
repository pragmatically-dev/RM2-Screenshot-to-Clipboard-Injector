FROM eeems/remarkable-toolchain:latest-rm2

WORKDIR /src
COPY . .

# Environment setup is required by the eeems toolchain before running qmake6
RUN apt-get update && apt-get install -y git python3 && \
    git clone https://github.com/asivery/xovi /tmp/xovi && \
    bash -c "export XOVI_REPO=/tmp/xovi && . /opt/codex/*/*/environment-setup-* && \
    qmake6 && \
    make"
