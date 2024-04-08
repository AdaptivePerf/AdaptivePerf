.PHONY: all install uninstall clean

ifndef prefix
prefix := /usr/local
endif

ifndef version
version := "1.0.dev+$(shell git rev-parse --short HEAD)"
endif

all: adaptiveperf

adaptiveperf: src/bashly_template.yml src/root_command.sh
	APERF_VERSION=$(version) envsubst < src/bashly_template.yml > src/bashly.yml
	bashly generate
	rm src/bashly.yml

install: all
	install -D adaptiveperf $(prefix)/bin

uninstall:
	rm -f $(prefix)/bin/adaptiveperf

clean:
	rm -f adaptiveperf
