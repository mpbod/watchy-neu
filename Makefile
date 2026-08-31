.PHONY: host-test python-test samples first-party factory-seed factory-flash gallery-test

FACTORY_FLASH_PYTHON ?= python3

host-test:
	cmake -S . -B build/host -G Ninja
	cmake --build build/host
	ctest --test-dir build/host --output-on-failure

python-test:
	python3 -m unittest discover -s tests/python -v

samples:
	python3 tools/build_samples.py

first-party:
	python3 tools/build_first_party.py --reproducible

factory-seed: first-party
	python3 tools/build_factory_seed.py --reproducible

factory-flash: factory-seed
	@test -n "$(PORT)" || { echo "PORT is required (for example: make factory-flash PORT=/dev/ttyUSB0)" >&2; exit 2; }
	$(FACTORY_FLASH_PYTHON) tools/flash_factory.py --port "$(PORT)"

gallery-test: host-test python-test samples first-party factory-seed
	platformio run -e watchy_v2
