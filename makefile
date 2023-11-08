CC = gcc

# Source files
CSRCS = client.c
KSRCS = server.c

# Default rule
all: 
	$(CC) -o tftp-client $(CSRCS)
	$(CC) -o tftp-server $(KSRCS)

# Clean rule
clean:
	rm -f tftp-client
	rm -f tftp-server 