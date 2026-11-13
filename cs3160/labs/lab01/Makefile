# CS3160 Lab 1
#
#   make          build every program in programs/asms/
#   make test     run the tests against your programs
#   make clean    remove everything that was generated
#
# `make test` is the checker used for grading. It awards no marks; it reports
# which cases each program passes.

.PHONY: all test clean tests

all:
	$(MAKE) -C programs

tests: test

test:
	@python3 tests/run_tests.py || true

clean:
	$(MAKE) -C programs clean
	rm -f tests/result.json
	rm -rf tests/__pycache__
