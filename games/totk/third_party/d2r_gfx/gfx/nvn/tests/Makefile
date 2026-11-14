# Run from repository root: make -C games/totk/third_party/d2r_gfx/gfx/nvn/tests run
OUT ?= $(CURDIR)/.build
BIN := $(OUT)/t_present_handoff
FW := -framework Metal -framework MetalFX -framework Foundation -framework QuartzCore -framework ImageIO -framework CoreGraphics -framework UniformTypeIdentifiers
.PHONY: all run clean
all: $(BIN)
# Force compilation: the backend includes headers beyond this directory.
.PHONY: FORCE
$(BIN): FORCE
	@mkdir -p $(OUT)
	TMPDIR=$(OUT) xcrun clang -O1 -g -Wall -Wextra -Werror -fobjc-arc -I../include t_present_handoff.m t_present_host.c unused_render_stubs.S -Wl,-dead_strip $(FW) -o $@
run: $(BIN)
	@set -e; for c in stale delegate rescue holds power clock winding; do DM_PRESENT_LINK=1 $(BIN) $$c; done
	@set -e; for mode in 0 1 2; do DM_PRESENT_LINK=$$mode $(BIN) configured; done
clean:
	rm -f $(BIN)
