all: app
	./bin/app

app: app.c
	$(CC) -g app.c -o ./bin/app -Wall -Wextra -pedantic -std=c99

clean: 
	rm ./bin/app