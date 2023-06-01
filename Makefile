build: src/server.c src/client.c
	gcc src/server.c -o bin/pp-server -libverbs
	gcc src/client.c -o bin/pp-client -libverbs