Multi-Threaded Proxy Web Server with Cache : 

This project implements a Multi-Threaded Proxy Server with Cache. The proxy acts as an intermediary between clients and web servers, improving efficiency and enabling functionali- ties such as caching, access restriction, and client anonymity. The main focus of this project is on concurrency management using multi-threading, synchronization with semaphores, and cache design using the Least Recently Used (LRU) algorithm.

commands to run :

1)gcc proxy_server_with_cache.c proxy_parse.c -o proxy_server.exe -lpthread -lws2_32

2)./proxy_server.exe 8080 (8080 is port number)

3)make any requests in browser connected to proxy (only http) [OR]

in another terminal run :

curl.exe -x http://127.0.0.1:8080 http://google.com
