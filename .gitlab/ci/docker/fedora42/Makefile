# Build and push multi-arch CI images. Sentinel files (.built-*) prevent rebuilds.
# Tested with podman only
#
# Usage:
#   make manifest DATE=20260609       # build and push manifest
#   make REGISTRY=vbolea/diy manifest # override registry
#   make DOCKER=docker manifest       # use docker instead of podman
#   make clean                        # remove sentinel files to force rebuild

DOCKER   ?= podman
REGISTRY ?= diatomic/diy
IMAGE    := ci-diy-fedora
DATE     ?= $(shell date +%Y%m%d)
ARCHS    := x86_64 arm64
TAG      := $(REGISTRY):$(IMAGE)-$(DATE)

.PHONY: push clean

.built-%: Dockerfile install_deps.sh
	$(DOCKER) build --platform linux/$* --manifest $(TAG) .
	@touch $@

push: $(addprefix .built-,$(ARCHS))
	$(DOCKER) manifest push --all $(TAG)

clean:
	rm -f .built-*
