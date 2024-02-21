.PHONY: all clean install test uninstall

ifndef prefix
prefix = /usr/local
endif

all: adaptiveperf

adaptiveperf: src/bashly.yml src/root_command.sh
	bashly generate

install: all
	install -D adaptiveperf* $(prefix)/bin

test: all
	./test.sh

uninstall:
	rm -f $(prefix)/bin/adaptiveperf*

clean:
	rm -f adaptiveperf*
