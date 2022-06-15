Pabian: Pabian.o av.o capture.o callback.o setup.o util.o commands.o
	gcc -g Pabian.o av.o capture.o callback.o setup.o util.o commands.o -o Pabian -lavformat -lavutil -lpthread -lavcodec -lswscale -ltoxcore -lz -lm `sdl-config --cflags --libs`
Pabian.o: Pabian.c av.h Pabian.h callback.h capture.h setup.h util.h commands.h
	gcc -c Pabian.c -o Pabian.o -lavformat -lavutil -lpthread -lavcodec -lswscale -ltoxcore -lz -lm `sdl-config --cflags --libs`
av.o: av.c av.h
	gcc -c av.c -o av.o -lavformat -lavutil -lpthread -lavcodec -lswscale -ltoxcore -lz -lm `sdl-config --cflags --libs`
capture.o: capture.c capture.h Pabian.h av.h
	gcc -c capture.c -o capture.o -lavformat -lavutil -lpthread -lavcodec -lswscale -ltoxcore -lz -lm `sdl-config --cflags --libs`
util.o: util.c util.h Pabian.h av.h
	gcc -c util.c -o util.o -lavformat -lavutil -lpthread -lavcodec -lswscale -ltoxcore -lz -lm `sdl-config --cflags --libs`
callback.o: callback.c callback.h Pabian.h util.h av.h
	gcc -c callback.c -o callback.o -lavformat -lavutil -lpthread -lavcodec -lswscale -ltoxcore -lz -lm `sdl-config --cflags --libs`
setup.o: setup.c setup.h Pabian.h av.h callback.h util.h
	gcc -c setup.c -o setup.o -lavformat -lavutil -lpthread -lavcodec -lswscale -ltoxcore -lz -lm `sdl-config --cflags --libs`
commands.o: commands.c commands.h Pabian.h util.h av.h capture.h
	gcc -c commands.c -o commands.o -lavformat -lavutil -lpthread -lavcodec -lswscale -ltoxcore -lz -lm `sdl-config --cflags --libs`
clean:
	-rm -f Pabian
