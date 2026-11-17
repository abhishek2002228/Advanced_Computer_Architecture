SECTIONS = superscalar_pipeline out_of_order_execution cache_memory_system matrix_computing/systolic_array
.PHONY: all clean reports check
all:
	@set -e; for section in $(SECTIONS); do $(MAKE) -C $$section/src; done
clean:
	@set -e; for section in $(SECTIONS); do $(MAKE) -C $$section/src clean; done
reports:
	python3 scripts/build_reports.py
check: all
	python3 scripts/verify.py
