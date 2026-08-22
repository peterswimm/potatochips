FLAGS += \
	-DTEST \
	-Wno-unused-local-typedefs

SOURCES += $(wildcard src/dsp/*.cpp)
SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += $(wildcard LICENSE*) res presets

.PHONY: publication-check

publication-check:
	@test -f .spelwork/project.yaml
	@test -f NOTICE.md
	@test -f PROVENANCE.md
	@test -f AI-NUTRITION.md
	@test -f ai-nutrition.json
	@test -f THIRD_PARTY.md
	@test -f sbom.spdx.json
	@python3 -m json.tool ai-nutrition.json > /dev/null
	@python3 -m json.tool sbom.spdx.json > /dev/null
	@! rg -n 'TODO|TBD' .spelwork NOTICE.md PROVENANCE.md AI-NUTRITION.md THIRD_PARTY.md RELEASE.md sbom.spdx.json
	@printf 'publication metadata check passed\n'

RACK_DIR ?= ../..
ifneq ($(MAKECMDGOALS),publication-check)
include $(RACK_DIR)/plugin.mk
endif
