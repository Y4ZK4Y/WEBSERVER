## basics of an HTTP server
client-server model - HTTP is an application layer protocol
browser opens an TCP connection and sends HTTP-formatted data.
server listens, accepts clients(always initialtes), reads bytes until request is complete, then responds with correct status , headers, and body.
client sends and HTTP request containing a method (GET POST DELETE), a path, headers, and maybe a body.

static vs. dynamic content:
a file on a disk(static)
output of a program (CGI)

# why we use poll()
a system call that allows us to monitor multiple file descripts at once to see if tehy are ready for I/O operations without blocking.
we pass poll() an array of pollfd structurs, where each structure has: 
- a file descriptor
- the events we're interested in (POLLIN POLLOUT)
- revents field - poll fills to tell us what is ready
poll() blocks at least one fdbecomes ready or a timeout occurs.
(basically does readiness notification, not IO)

how do we remove client after send fails (in client.cpp)
there's a write in parseHTTP , is that okay?

## synchronous IO on regular disk files
make a big file:
dd if=/dev/urandom of=www/bigfile.bin bs=1M count=100 100MB file
from another terminal curl -o /dev/null http://localhost:8080/bifgile.bin

## check invalid error page
curl -i http://localhost:8080/invalido

## max body limit
curl -i -X POST -H "Content-Type: text/plain" --data "maxing body limiiiiiiiiiiittttt" http://localhost:8080/upload

## Set up a default file to serve when requesting a directory.
urls ending in / or pointing to a folder - make sure server serves the default index
this line in the config file: index index.html index.htm;
how it does:
resolves /some_directory/ to a filesystem directory - looks for files listed in index - 
serves the first one that exists - returns 200 or 404
curl -i http://localhost:8080/dir

## Set up a list of accepted methods for a specific route (e.g., try DELETE something with and without permission)
for a location that has POST
curl -i -X POST -H "Content_Type: text/plain" --data "abc" http://localhost:8080/upload/file.txt

for a location that does not have DELETE
curl -i -X DELETE http://localhost:8080/upload/file.txt


## basic checks:
delete something:
curl -i -X DELETE http://localhost:8080/uploads/readme.txt

retrieve something:
curl -i http://localhost:8080/uploads/readme.txt 

unknown request:
printf "BREW / HTTP\1.1\r\nHost: localhost\r\n\r\n" | nc 127.0.0.1 8080

## test memory usage
watch -n 1 "ps aux | grep webserv"
