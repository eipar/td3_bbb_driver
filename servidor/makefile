CC 	:= gcc

CFLAGS := -Wall

server:
	@echo "* Compiling Project *"
	$(CC) -c -o main.o main.c $(CFLAGS)
	$(CC) -o server main.o ../driver/BMP180.o

copy:
	@echo "* Copying to BBB *"
	sudo scp -r /home/euge/Documents/repolocal/r5051-Ipar-Eugenia/02_cuat/servidor/ debian@192.168.6.2:./td3/

clean:
	@echo "* Cleaning Project*"
	rm -f *.o
	rm -f ./server
