clean:
	clear
	rm -rf obj
	mkdir obj

lib-build:
	make clean
	gcc -Wall -Ilib/ -o obj/dbll.o -c lib/dbll.c

test: 
	make lib-build
	gcc -Wall -Ilib/ -Itest/ -o obj/test.o -c test/test.c
	gcc -Wall -Ilib/ -Itest/ -o obj/test-main obj/dbll.o obj/test.o test/main.c
	obj/test-main
