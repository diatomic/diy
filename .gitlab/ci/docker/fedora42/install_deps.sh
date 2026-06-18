#!/bin/sh

# Install build requirements.
dnf install --setopt=install_weak_deps=False -y \
    libdwarf-devel elfutils-devel binutils-devel spdlog-devel \
    mpich-devel openmpi-devel

# Install development tools
dnf install --setopt=install_weak_deps=False -y \
    clang-tools-extra \
    gcc-c++ \
    git-core

# Install memcheck tools
dnf install --setopt=install_weak_deps=False -y \
    libasan \
    libubsan

dnf clean all
