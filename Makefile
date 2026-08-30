.PHONY: host-test python-test samples

host-test:
	cmake -S . -B build/host -G Ninja
	cmake --build build/host
	ctest --test-dir build/host --output-on-failure

python-test:
	python3 -m unittest discover -s tests/python -v

samples:
	python3 tools/build_samples.py
