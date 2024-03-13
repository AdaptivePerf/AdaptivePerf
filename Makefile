.PHONY: all install uninstall clean

ifndef prefix
prefix = /usr/local
endif

all: adaptiveperf

adaptiveperf: src/bashly.yml src/root_command.sh
	bashly generate

install: all
	install -D adaptiveperf $(prefix)/bin

uninstall:
	rm $(prefix)/bin/adaptiveperf

clean:
	rm adaptiveperf
