server.o: 
	g++ -Wall   -o  server  server.cpp
clean:
	rm -rf server.o
	rm -rf server
