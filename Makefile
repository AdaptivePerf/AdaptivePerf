.PHONY: all clean

all: adaptiveperf adaptiveperf-merge adaptiveperf-split-ids adaptiveperf-stackcollapse adaptiveperf-flamegraph adaptiveperf-perf-get-callchain.py adaptiveperf-split-report

adaptiveperf: src/bashly.yml src/root_command.sh
	bashly generate

adaptiveperf-merge: src/adaptiveperf-merge
	cp src/adaptiveperf-merge .

adaptiveperf-split-ids: src/adaptiveperf-split-ids
	cp src/adaptiveperf-split-ids .

adaptiveperf-stackcollapse: src/adaptiveperf-stackcollapse
	cp src/adaptiveperf-stackcollapse .

adaptiveperf-flamegraph: src/adaptiveperf-flamegraph
	cp src/adaptiveperf-flamegraph .

adaptiveperf-perf-get-callchain.py: src/adaptiveperf-perf-get-callchain.py
	cp src/adaptiveperf-perf-get-callchain.py .

adaptiveperf-split-report: src/adaptiveperf-split-report
	cp src/adaptiveperf-split-report .

clean:
	rm -f adaptiveperf*
