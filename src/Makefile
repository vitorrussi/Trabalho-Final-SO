GCC=/usr/bin/g++
CPPFLAGS = -std=c++14 -Wall
simplefs: shell.o fs.o disk.o
	$(GCC) shell.o fs.o disk.o -o simplefs $(CPPFLAGS)

shell.o: shell.cpp
	$(GCC) -Wall shell.cpp -c -o shell.o -g $(CPPFLAGS)

fs.o: fs.cpp fs.h
	$(GCC) -Wall fs.cpp -c -o fs.o -g $(CPPFLAGS)

disk.o: disk.cpp disk.h
	$(GCC) -Wall disk.cpp -c -o disk.o -g $(CPPFLAGS)

clean:
	rm simplefs disk.o fs.o shell.o
