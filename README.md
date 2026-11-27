# Multithreaded HTTP Proxy Web Server with LRU Cache

A high-performance multithreaded HTTP forward proxy server in C with an in-memory Least Recently Used (LRU) cache, concurrency controls, and POSIX thread synchronization.

**Author:** [Naguru Sai Manoj Bhargav](https://github.com/ManojBhargav77)  
**Repository:** [github.com/ManojBhargav77/Multithreaded_proxy_web_server](https://github.com/ManojBhargav77/Multithreaded_proxy_web_server)

---

## 👥 Credits and My Changes

This repository builds upon an open-source multithreaded HTTP proxy server and request parser implementation.

### My Contributions & Bug Fixes (Naguru Sai Manoj Bhargav):
1. **Resolved Cache Self-Deadlock:** Fixed a critical recursive lock failure in dd_cache_element(). The original code called 
emove_cache_element() (which locked lock a second time on the same thread) while already holding lock, permanently hanging the proxy once cache capacity reached 200 MB. Re-architected eviction with an internal lock-free helper (
emove_cache_element_locked()).
2. **Eliminated Use-After-Free Race Condition:** In the original code, ind() returned a pointer to an element after releasing the mutex, allowing a concurrent eviction to free the element while another thread was transmitting 	emp->data. Replaced with get_cache_copy() to safely clone response bytes under the mutex before transmission.
3. **Binary-Safe Response Caching:** Replaced strlen and strcpy with 	emp_buffer_index and memcpy so binary payloads (gzip, images, PDFs) are cached without truncation on null bytes.
4. **Added HTTP 501 Responses:** Returned standard 501 Not Implemented status codes for non-GET methods instead of dropping connections silently.

---

## 🌟 Key Features

- **Multithreaded Concurrency:** Spawns worker threads (`pthread_create`) to handle simultaneous client connections, regulated by a counting semaphore (`sem_wait` / `sem_post`) to cap maximum concurrent connections at 400.
- **LRU Cache Implementation:** In-memory caching with eviction based on timestamps (`lru_time_track`), supporting up to 200 MB total cache budget and 10 MB per individual cache element.
- **Thread Safety & Race Elimination:**
  - **Deadlock Prevention:** Internal lock-free eviction helper (`remove_cache_element_locked`) ensures that eviction triggered inside `add_cache_element()` does not re-acquire the non-recursive mutex `lock`.
  - **Use-After-Free Elimination:** Cache lookups make an isolated memory copy of the payload under the lock before returning to the calling thread (`get_cache_copy`), ensuring that concurrent evictions cannot free buffer memory during transmission.
- **Binary-Safe Caching:** Allocates exact byte buffers and copies responses using `memcpy` rather than null-terminated string functions (`strcpy`), preserving compressed assets (gzip), images, and binaries without truncation.
- **HTTP/1.0 and HTTP/1.1 Parsing:** Origin-form request rewriting, header extraction, and 501 Not Implemented response generation for unsupported HTTP verbs.
- **Cross-Platform Compatibility:** Supports Linux/macOS and Windows (via Winsock2 socket layer abstraction).

---

## 🛠️ Build and Run

### On Linux / WSL:
```bash
gcc -o proxy proxy_server_with_cache.c proxy_parse.c -lpthread
./proxy 8080
```

### On Windows (MinGW/MSYS2):
```bash
gcc -o proxy.exe proxy_server_with_cache.c proxy_parse.c -lpthread -lws2_32
./proxy.exe 8080
```

### Testing with curl:
```bash
# First request: Cache miss (fetches from origin and stores in cache)
curl -x http://localhost:8080 http://example.com/

# Second request: Cache hit (served directly from in-memory LRU cache)
curl -x http://localhost:8080 http://example.com/
```

---

## 🏗️ Architecture & Request Lifecycle

```
Client (curl / browser)
       │ (1) GET http://example.com/
       ▼
Proxy Listener (:8080)
       │ accept() connection
       ▼
Worker Thread (thread_fn) ── sem_wait()
       │
       ├── Cache Lookup: get_cache_copy(req)
       │      ├── HIT: Stream cached bytes in 4KB chunks ──► Client
       │      └── MISS:
       │            ├── Parse and validate HTTP headers (proxy_parse)
       │            ├── DNS resolution (gethostbyname) & TCP connect
       │            ├── Stream response back to client
       │            └── Store in LRU cache (add_cache_element)
       │
       └── Cleanup: shutdown(), close(), sem_post()
```

---

## 📜 License
This project is licensed under the [MIT License](LICENSE).
