## How the IXXC-Web Server Works

This small web server, written in C, makes an archive similar to a TAR file directly accessible to browsers by storing static 
frontend files or documents within a single large binary file which contains all the data the server requires.

### Linux Processes and User

The server starts with a parent root process. This initialization process creates a logger process 
as well as several worker processes. The parent process handles the initial setup, such as creating the log file and establishing permissions. 
Subsequently, it drops its root privileges and processes interrupts or signals in order to send commands to the child processes.

The worker processes communicate with web browsers. Having multiple workers allows the server to handle several requests 
at the same time, making use of multiple CPU cores. Because of this, the data often stays in the shared L3 cache, which makes responses very fast. 
Workers are not restarted automatically if they crash, to avoid undefined behavior. 
Multiple workers are there to keep response times short. The server runs under a user account with no login shell. 

### Memory Management

Frontend code, images, and documents are stored in the `rodata` segment and accessed through an offset table, which is created at compile time. 
All the data is kept in one large vector, similar to a custom file system. When the main thread starts, it calculates and allocates 
the required memory. This memory area does not change during runtime. All other dynamic memory is small and only exists on the stack. 

The workers and the logger share a memory area of 255 pages, which acts as a ring buffer. 
Workers write log messages to the next free slot, and the logger reads them and writes them to disk.
This method is fast and does not block the workers with I/O operations. Another purpose of this architecture 
is to strictly separate file access from network access. 

### Constant-time verification of file paths for hiding endpoints

Every request is copied into a fixed-size buffer, with a maximum size of one page (**0x1000** bytes).
Before that, the page is overwritten once more with 0x0. Both empty URLs and URLs that are too long (**0x1000** bytes or more) 
are padded or cut to the same length. The server compares this fixed-size buffer against every allowed endpoint using XOR operations over the whole block. 
This is why the server uses low-level architecture. For some systems, the server does not use the standard C library, 
so the assembly code remains easy to understand.
