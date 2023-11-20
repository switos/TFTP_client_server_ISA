CC = gcc

# Source files
CSRCS = client.c
KSRCS = server.c

# Default rule
all: 
	$(CC) -Wall -o tftp-client $(CSRCS)
	$(CC) -Wall -o tftp-server $(KSRCS)

# Clean rule
clean:
	rm -f tftp-client
	rm -f tftp-server 