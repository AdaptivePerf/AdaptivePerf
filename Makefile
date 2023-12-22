.PHONY: all clean install test uninstall

ifndef prefix
prefix = /usr/local
endif

all: adaptiveperf adaptiveperf-merge adaptiveperf-split-ids adaptiveperf-perf-get-callchain.py adaptiveperf-split-report adaptiveperf-misc-funcs.sh

adaptiveperf: src/bashly.yml src/root_command.sh
	bashly generate

adaptiveperf-merge: src/adaptiveperf-merge
	cp src/adaptiveperf-merge .

adaptiveperf-split-ids: src/adaptiveperf-split-ids
	cp src/adaptiveperf-split-ids .

adaptiveperf-perf-get-callchain.py: src/adaptiveperf-perf-get-callchain.py
	cp src/adaptiveperf-perf-get-callchain.py .

adaptiveperf-split-report: src/adaptiveperf-split-report
	cp src/adaptiveperf-split-report .

adaptiveperf-misc-funcs.sh: src/adaptiveperf-misc-funcs.sh
	cp src/adaptiveperf-misc-funcs.sh .

install: all
	install -D adaptiveperf* $(prefix)/bin

test: all
	./test.sh

uninstall:
	rm -f $(prefix)/bin/adaptiveperf*

clean:
	rm -f adaptiveperf*
