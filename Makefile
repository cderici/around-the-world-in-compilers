LANGS := athens berlin cairo

.PHONY: all $(LANGS) clean clean-%

all: $(LANGS)

$(LANGS):
	$(MAKE) -C langs/$@
	cp langs/$@/$@ $@

clean: $(addprefix clean-,$(LANGS))

clean-%:
	$(MAKE) -C langs/$* clean || true
	rm -f $*
