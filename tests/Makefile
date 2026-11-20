# Pruebas en el PC (sin PSP): logica con las soluciones oficiales y capturas del juego entero
#   make -C tests check DATA=../data     juega las 266 soluciones oficiales
#   make -C tests shots DATA=../data     capturas en tests/out/*.png
DATA ?= ../data
CFLAGS = -O1 -g -Wall -DDEVLOG=0 -I../src
SRC = ../src

all: host_test host_shot

host_test: host_test.c $(SRC)/level.c $(SRC)/logic.c $(SRC)/file.c
	$(CC) $(CFLAGS) -o $@ $^ -lz

host_shot: host_main.c gfx_host.c $(SRC)/gfx.c $(SRC)/level.c $(SRC)/logic.c $(SRC)/game.c $(SRC)/play.c \
           $(SRC)/ui.c $(SRC)/classic.c $(SRC)/save.c $(SRC)/lang.c $(SRC)/file.c
	$(CC) $(CFLAGS) -o $@ $^ -lm -lz

check: host_test
	./host_test $(DATA)/levels.bin

shots: host_shot
	rm -rf out && mkdir -p out/work out && ln -sfn $(abspath $(DATA)) out/work/data && mkdir -p out/work/out
	./host_shot out/work
	python3 to_png.py out/work/out out

clean:
	rm -rf host_test host_shot out
