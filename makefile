clean:
	clear
	rm -rf obj
	mkdir obj

lib-run:
	clear
	make clean
	rm -f lib/debug.h
	touch lib/debug.h
	gcc -Wall -Ilib/ -o obj/dbll.o -c lib/dbll.c
	rm -f lib/debug.h

lib-debug-run:
	clear
	make clean
	rm -f lib/debug.h
	touch lib/debug.h
	echo '#define DBLL_DEBUG' > lib/debug.h
	gcc \
		-Wall \
		-g \
		-Ilib/ -o obj/dbll.o -c lib/dbll.c

	rm -f lib/debug.h

test-run: 
	clear
	make lib-debug-run
	gcc \
		-Wall \
		-g \
		-Ilib/ -Itest/ -o obj/test.o -c test/test.c

	gcc \
		-Wall \
		-g \
		-Ilib/ -Itest/ -o obj/test-main \
		obj/dbll.o obj/test.o test/main.c

	cd test && ../obj/test-main
