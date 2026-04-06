PYTHON := python3

all:
	$(PYTHON) upsgiconfig.py --build $(PROFILE)

clean:
	$(PYTHON) upsgiconfig.py --clean
	cd unittest && make clean

check:
	$(PYTHON) upsgiconfig.py --check

plugin.%:
	$(PYTHON) upsgiconfig.py --plugin plugins/$* $(PROFILE)

unittests:
	$(PYTHON) upsgiconfig.py --build default
	cd unittest && make test

tests:
	$(PYTHON) t/runner

%:
	$(PYTHON) upsgiconfig.py --build $@

.PHONY: all clean check tests
