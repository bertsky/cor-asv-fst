# BEGIN-EVAL makefile-parser --make-help Makefile

# Directory to install to ('$(PREFIX)')
PREFIX ?= $(if $(VIRTUAL_ENV),$(VIRTUAL_ENV),$(CURDIR)/.local)

SHELL = /bin/bash
PYTHON ?= python
PIP ?= pip

help:
	@echo ""
	@echo "  Targets"
	@echo ""
	@echo "    deps-ubuntu  (install required system packages)"
	@echo "    deps         (install required Python packages)"
	#@echo "    deps-test  pip install -r requirements_test.txt"
	@echo ""
	@echo "    install    (install this Python package)"
	#@echo "    test       python -m pytest test"

# END-EVAL

PKG_CONFIG_PATH := $(PREFIX)/lib/pkgconfig
export PKG_CONFIG_PATH
deps: $(PREFIX)/lib/libfst.so.17 pynini-2.1.2/setup.py
	$(PIP) install -U pip
	$(PIP) install Cython
	CFLAGS="-I$(PREFIX)/include" LDFLAGS="-L$(PREFIX)/lib" $(PIP) install ./pynini-2.1.2
	CFLAGS="-I$(PREFIX)/include" LDFLAGS="-L$(PREFIX)/lib" $(PIP) install -r requirements.txt

#deps-test:
#	$(PIP) install -r requirements_test.txt

# Dependencies for deployment in an ubuntu/debian linux
# we need libstdc++ > 5.0 (for codecvt, std::make_unique etc)
# since pynini 2.0.9, we need libfst-dev > 1.7
deps-ubuntu:
	apt-get install -y \
		python3 python3-dev python3-pip python3-venv \
		g++ libfst-dev \
		wget tar gzip

$(PREFIX)/lib/libfst.so.17: openfst-1.7.9.tar.gz
	tar --no-same-permissions --no-same-owner -zxvf $<
	cd openfst-1.7.9 && ./configure --enable-grm --enable-python --prefix=$(PREFIX) && $(MAKE) install

openfst-1.7.9.tar.gz:
	wget -nv http://www.openfst.org/twiki/pub/FST/FstDownload/openfst-1.7.9.tar.gz

pynini-2.1.2/setup.py: pynini-2.1.2.tar.gz
	tar --no-same-permissions --no-same-owner -zxvf $<

pynini-2.1.2.tar.gz:
	wget -nv http://www.opengrm.org/twiki/pub/GRM/PyniniDownload/pynini-2.1.2.tar.gz

install: deps
	$(PIP) install .

#test: test/assets
#	test -f model_dta_test.h5 || keraslm-rate train -m model_dta_test.h5 test/assets/*.txt
#	keraslm-rate test -m model_dta_test.h5 test/assets/*.txt
#	$(PYTHON) -m pytest test $(PYTEST_ARGS)

#test/assets:
#	test/prepare_gt.bash $@

.PHONY: help deps-ubuntu deps deps-test install test
