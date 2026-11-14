# Run from repository root: make -C games/totk/third_party/d2r_gfx/gfx/nvn/tests run
OUT ?= $(CURDIR)/.build
BIN := $(OUT)/t_present_handoff
HDR_FIXTURE := $(CURDIR)/fixtures/botw_compose_synthetic.metal
HDR_SYNTHETIC_TWIN := $(OUT)/botw_hdr_synthetic_twin.metal
HDR_GAME_TWIN := $(OUT)/botw_hdr_twin.metal
FW := -framework Metal -framework MetalFX -framework Foundation -framework QuartzCore -framework ImageIO -framework CoreGraphics -framework UniformTypeIdentifiers
.PHONY: all run clean hdr
all: $(BIN) $(WD)
# Force compilation: the backend includes headers beyond this directory.
.PHONY: FORCE
$(BIN): FORCE
	@mkdir -p $(OUT)
	TMPDIR=$(OUT) xcrun clang -O1 -g -Wall -Wextra -Werror -fobjc-arc -I../include t_present_handoff.m t_present_host.c unused_render_stubs.S -Wl,-dead_strip $(FW) -o $@
WD := $(OUT)/t_gpu_watchdog
$(WD): t_gpu_watchdog.c ../gpu_watchdog_rule.h
	@mkdir -p $(OUT)
	TMPDIR=$(OUT) xcrun clang -O1 -g -Wall -Wextra -Werror t_gpu_watchdog.c -o $@
run: $(BIN) $(WD) hdr
	@$(WD)
	@set -e; for c in stale delegate rescue holds power clock winding; do DM_PRESENT_LINK=1 $(BIN) $$c; done
	@set -e; for mode in 0 1 2; do DM_PRESENT_LINK=$$mode $(BIN) configured; done
hdr:
	@mkdir -p $(OUT)
	TMPDIR=$(OUT) xcrun clang -O1 -Wall -Wextra -Werror -Wno-unused-function -fobjc-arc -DGAME_BOTW=1 -DGAME_BOTW190=1 t_botw_hdr.m -framework Foundation -o $(OUT)/t_botw_hdr
	@set -e; for v in 0 1 3 2; do echo "TOTK_HDR_COMPOSE=$$v"; \
		TOTK_HDR_COMPOSE=$$v $(OUT)/t_botw_hdr "$(HDR_FIXTURE)" "$(HDR_SYNTHETIC_TWIN)" "$(HDR_GAME_TWIN)"; \
		TMPDIR=$(OUT) python3 check_botw_hdr_metal.py "$(HDR_FIXTURE)" "$(HDR_SYNTHETIC_TWIN)" "$(HDR_GAME_TWIN)"; done
	$(OUT)/t_botw_hdr "$(HDR_FIXTURE)" "$(HDR_SYNTHETIC_TWIN)" "$(HDR_GAME_TWIN)"
	@set -e; for cfg in 'TOTK_GAME_V143 1' 'GAME_BOTW190 1' 'GAME_BOTW160 0' 'GAME_BOTW 0'; do \
		set -- $$cfg; case $$1 in GAME_BOTW*) defs=-DGAME_BOTW=1;; *) defs=;; esac; \
		TMPDIR=$(OUT) xcrun clang -Wall -Wextra -Werror $$defs -D$$1=1 -DEXPECT_HDR=$$2 t_hdr_gates.c -o $(OUT)/t_hdr_gates; \
		TOTK_HDR= TOTK_HDR_CONTROL=1 $(OUT)/t_hdr_gates; \
		TOTK_HDR=1 TOTK_HDR_CONTROL=1 $(OUT)/t_hdr_gates; \
		TOTK_HDR=0 TOTK_HDR_CONTROL=0 $(OUT)/t_hdr_gates; \
		TMPDIR=$(OUT) xcrun clang -Wall -Wextra -Werror $$defs -D$$1=1 -DEXPECT_HDR=$$2 -DTEST_HDR_RUNTIME=1 t_hdr_gates.c -o $(OUT)/t_hdr_gates; \
		TOTK_HDR=1 TOTK_HDR_CONTROL=1 $(OUT)/t_hdr_gates; \
		TOTK_HDR=0 TOTK_HDR_CONTROL=0 $(OUT)/t_hdr_gates; \
	done
	TMPDIR=$(OUT) xcrun clang -Wall -Wextra -Werror -DGAME_BOTW=1 -DGAME_BOTW190=1 -DTEST_HDR_NONMAC=1 -DEXPECT_HDR=0 t_hdr_gates.c -o $(OUT)/t_hdr_gates
	TOTK_HDR=1 TOTK_HDR_CONTROL=1 $(OUT)/t_hdr_gates
clean:
	rm -f $(BIN) $(WD) $(OUT)/t_botw_hdr $(OUT)/t_hdr_gates $(OUT)/botw_hdr_twin.metal $(OUT)/botw_hdr_twin.air
	rm -f $(HDR_SYNTHETIC_TWIN) $(OUT)/botw_hdr_synthetic_twin.air $(OUT)/botw_compose_synthetic.air
	rm -rf $(OUT)/metal-module-cache
