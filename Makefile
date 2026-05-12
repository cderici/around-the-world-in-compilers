LANGS := athens berlin cairo dublin

.PHONY: all $(LANGS) clean clean-% clean-build

all: $(LANGS)

$(LANGS):
	$(MAKE) -C langs/$@

clean: $(addprefix clean-,$(LANGS)) clean-build

clean-%:
	$(MAKE) -C langs/$* clean || true

clean-build:
	rm -rf build
