CC= g++
CGLAG=  -ggdb -Wall

mirror_client: mirror_client.o
	$(CC) $(CFLAG) -o mirror_client mirror_client.o

mirror_client.o: mirror_client.cpp
	$(CC) -c mirror_client.cpp

.PHONY: clean
clean:
	rm -f mirror_client.o mirror_client
