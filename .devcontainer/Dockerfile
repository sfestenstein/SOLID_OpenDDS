# =============================================================================
# Stage 1: pull the prebuilt OpenDDS tree (ACE + TAO + OpenDDS) out of the
# official image. We only need its /opt/OpenDDS directory — we will NOT run
# anything from this stage at runtime, because its glibc is too old for the
# VS Code Server.
# =============================================================================
FROM objectcomputing/opendds:latest AS opendds-src

# =============================================================================
# Stage 2: modern Ubuntu base that VS Code Server supports (glibc >= 2.28).
# Ubuntu 22.04 ships glibc 2.35 which comfortably exceeds VS Code's minimum.
# OpenDDS binaries built against an older glibc remain forward-compatible, so
# copying /opt/OpenDDS over works.
# =============================================================================
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Toolchain, OpenDDS runtime deps, and dev-container conveniences.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        perl \
        gdb \
        git \
        sudo \
        nano \
        less \
        curl \
        ca-certificates \
        pkg-config \
        libssl-dev \
        libxerces-c-dev \
        clangd \
    && rm -rf /var/lib/apt/lists/*

# Copy the prebuilt OpenDDS tree from the official image.
COPY --from=opendds-src /opt/OpenDDS /opt/OpenDDS

# Make the OpenDDS environment available to login + non-login shells, and to
# every CMake/IDL invocation. setenv.sh exports DDS_ROOT / ACE_ROOT / TAO_ROOT
# and prepends the IDL compilers + shared libraries to PATH/LD_LIBRARY_PATH.
RUN echo 'source /opt/OpenDDS/setenv.sh' >> /etc/bash.bashrc \
 && echo '/opt/OpenDDS/ACE_wrappers/lib' >  /etc/ld.so.conf.d/opendds.conf \
 && echo '/opt/OpenDDS/lib'              >> /etc/ld.so.conf.d/opendds.conf \
 && ldconfig

# Non-root dev user with passwordless sudo (standard dev container pattern).
ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=1000
RUN groupadd --gid ${USER_GID} ${USERNAME} \
 && useradd  --uid ${USER_UID} --gid ${USER_GID} -m -s /bin/bash ${USERNAME} \
 && echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME} \
 && chmod 0440 /etc/sudoers.d/${USERNAME}

USER ${USERNAME}
WORKDIR /workspaces/DreadWithOpenDDS
run alias ll='ls -alhF --color=auto' && echo "alias ll='ls -alhF --color=auto'" >> ~/.bashrc
run alias lt='ls -alhorF --color=auto' && echo "alias lt='ls -alhorF --color=auto'" >> ~/.bashrc
