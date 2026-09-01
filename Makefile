.PHONY: host-test python-test samples first-party factory-seed factory-flash-preflight factory-flash gallery-test

PYTHON ?= python3
IDF_PYTHON ?= python3
FACTORY_FLASH_PYTHON ?= $(PYTHON)

host-test:
	cmake -S . -B build/host -G Ninja
	cmake --build build/host
	ctest --test-dir build/host --output-on-failure

python-test:
	$(PYTHON) -m unittest discover -s tests/python -v

samples:
	$(IDF_PYTHON) tools/build_samples.py

first-party:
	$(IDF_PYTHON) tools/build_first_party.py --reproducible

factory-seed: first-party
	$(IDF_PYTHON) tools/build_factory_seed.py --reproducible

factory-flash-preflight:
	@test -n "$(PORT)" || { echo "PORT is required (for example: make factory-flash PORT=/dev/ttyUSB0)" >&2; exit 2; }
	$(FACTORY_FLASH_PYTHON) tools/flash_factory.py --port "$(PORT)" --preflight-only

factory-flash: factory-flash-preflight
	$(MAKE) factory-seed IDF_PYTHON="$(IDF_PYTHON)"
	$(FACTORY_FLASH_PYTHON) tools/flash_factory.py --port "$(PORT)"

gallery-test: host-test python-test samples first-party factory-seed
	platformio run -e watchy_v2
