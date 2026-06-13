# Common Zephyr recipes. These are PLAIN commands meant to run inside the build
# environment -- this Makefile knows nothing about podman or dev.sh. Prepend
# dev.sh to run a target in the container, e.g.:
#
#   ./dev.sh make update      # fetch the Zephyr workspace (run once, first)
#   ./dev.sh make build
#   ./dev.sh make pristine
#   ./dev.sh make menuconfig
#
# (`make clean` is just an rm and works on the host too.)
#
# Override the board on the command line: ./dev.sh make build BOARD=hifive1
.DEFAULT_GOAL := help

BOARD ?= hifive1_revb

.PHONY: help update check-workspace build pristine clean menuconfig boards format

help: ## Show this help
	@grep -hE '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) \
	  | awk -F':.*?## ' '{printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}'

update: ## Fetch/refresh the Zephyr workspace into the repo (run once, and after west.yml bumps)
	@if [ ! -e .west ]; then \
	  echo ">> initializing west workspace (.manifest + .west)"; \
	  mkdir -p .manifest && cp west.yml .manifest/west.yml; \
	  git -C .manifest init -q && git -C .manifest add west.yml; \
	  git -C .manifest -c user.email=build@local -c user.name=build commit -qm pin; \
	  west init -l .manifest; \
	else \
	  cp west.yml .manifest/west.yml && git -C .manifest add west.yml; \
	  git -C .manifest diff --cached --quiet \
	    || git -C .manifest -c user.email=build@local -c user.name=build commit -qm pin; \
	fi
	west update

# west build/boards are Zephyr extension commands; they only exist once the
# workspace has been fetched. Fail with a pointer instead of a cryptic error.
check-workspace:
	@[ -d zephyr ] || { echo "error: no Zephyr workspace yet -- run './dev.sh make update' first" >&2; exit 1; }

build: check-workspace ## Incremental build of app/ for the board
	west build -b $(BOARD) app -d build

pristine: check-workspace ## Clean (pristine) build of app/ for the board
	west build -b $(BOARD) app -d build -p always

clean: ## Remove the build directory
	rm -rf build

menuconfig: check-workspace ## Interactive Kconfig editor for the current build
	west build -b $(BOARD) app -d build -t menuconfig

boards: check-workspace ## List the boards west knows about
	west boards

format: ## Format the repo's sources: Markdown (Prettier) + app/ C (clang-format)
	./format.sh
