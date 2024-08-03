kilo: app.c
	$(CC) app.c -o app -Wall -Wextra -pedantic -std=c99

clean: 
	rm app