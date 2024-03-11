clean:
	clear
	rm -rf obj
	mkdir obj

lib-run:
	make clean
	rm -f lib/debug.h
	touch lib/debug.h
	gcc -Wall -Ilib/ -o obj/dbll.o -c lib/dbll.c
	rm -f lib/debug.h

lib-run-debug:
	make clean
	rm -f lib/debug.h
	touch lib/debug.h
	echo '#define DBLL_DEBUG' > lib/debug.h
	gcc -Wall -Ilib/ -o obj/dbll.o -c lib/dbll.c
	rm -f lib/debug.h

test-run: 
	make lib-run-debug
	gcc -Wall -Ilib/ -Itest/ -o obj/test.o -c test/test.c
	gcc -Wall -Ilib/ -Itest/ -o obj/test-main obj/dbll.o obj/test.o test/main.c
	clear
	cd test && ../obj/test-main
