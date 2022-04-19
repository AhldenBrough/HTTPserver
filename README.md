How to Run Httpproxy
To run the code, run the makefile by typing "make" in the command line.
Then type "./httpproxy" followed by a valid port number, the port numbers of the associated servers that will be performing the work, -N 'number of parallel connections' (optional), -R 'how often a healthcheck will be performed on the servers' (optional), -m 'the size of cached files' (optional), and -s 'how many files the cache can store' (optional), in any order.
An example would be ./httpserver 1234 8080 8081 -R 5 -N 5 -m 100000 -s 3, which will create a server running at localhost:1234 using servers running at localhost:8080 and localhost:8081, supporting 5 parallel connections, caching 3 files at a time with size 100000 bytes, and performing a healthcheck on its servers every 5 requests.
You can then send requests to the server in a separate terminal window using curl.

Limitations
Httpproxy does not have a working cache.
