#include "io61.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/mman.h>

#define BUFFER_SIZE 4096
#define NUM_CACHE 12

// io61.c
//    YOUR CODE HERE!


// io61_file
//    Data structure for io61 file wrappers. Add your own stuff.

/* Here is my cache_slot structure. My io61_file contains an array
 * of these structs along with other information.
 */
typedef struct cache_slot {
    unsigned char arr_buf[BUFFER_SIZE];
    unsigned char* str_buf;
    bool is_active;    
    /* This is the current position of the character we are reading
     * from buffer
     */
    size_t pos;
    /* This is the offset within the file we are seeking, using in 
     * our io61_seek function
     */
    size_t offset;
    /* Current size of our buffer. Typical this will be BUFFER_SIZE but
     * can be smaller when reading from smaller files or when reaching the
     * end of a file that's size is not a multiple of BUFFER_SIZE
     */
    size_t buff_size;
    /* This is the value of the files cache_count field when this cache_slot
     * was created. This is used to find the oldest cache during the 
     * get_free_cache function below.
     */
    int created_count;
} cache_slot;

struct io61_file {
    int fd;
    int mode;
    char* file_data;
    size_t file_offset;
    /* This is our array of cache_slots, the size of which is determined by
     * the #define statement above.
     */
    cache_slot cache[NUM_CACHE];
    int filesize;
    /* This is the index of the current cache we are working with. Defaults to
     * -1 when the file is opened until our first cache is filled.
     */
    int curr_cache;
    /* This is the total_cache counter used for determining the age of the caches
     * we create.
     */
    int cache_count;
};

/* This function looks through our array of caches and returns the first
 * inactive cache or the cache that was created earliest.
 */
cache_slot* get_free_cache(io61_file *f) {
    int oldest_cache = 0;
    int lowest_count = INT_MAX;
    for (int i = 0; i < NUM_CACHE; i++) {
        cache_slot* curr_cache = &f->cache[i];
        if (!curr_cache->is_active) {
            curr_cache->is_active = true;
            return curr_cache;
        }
        if (curr_cache->created_count < lowest_count) {
            oldest_cache = i;
            lowest_count = curr_cache->created_count;
        }
    }
    return &f->cache[oldest_cache];
}

/* This function returns our current cache determined by the index in our
 * file struct. If NULL then we have not created any caches yet.
 */
cache_slot* get_curr_cache(io61_file *f) {
    if (f->curr_cache == -1) {
        return NULL;
    } else {
        return &f->cache[f->curr_cache];
    }
}  

/* This function is used by io61_readc, io61_read and io61_seek to fill caches from data
 * located at the offset position into the file. It uses pread if the file is seekable and
 * read if the file is not.
 */
cache_slot* fill_new_cache(io61_file *f, size_t offset) {
    cache_slot* new_cache = get_free_cache(f);
    size_t chars_read;
    if (f->filesize == -1) {
        chars_read = read(f->fd, new_cache->arr_buf, BUFFER_SIZE);
    } else {
        
        //chars_read = pread(f->fd, new_cache->buffer, BUFFER_SIZE, (off_t) offset);
        if (f->filesize - offset < BUFFER_SIZE) {
            chars_read = f->filesize - offset;
        } else {
            chars_read = BUFFER_SIZE;
        }
        new_cache->str_buf = (unsigned char *) &f->file_data[offset];
    }    
    if (chars_read == 0) {
        // we must be at the EOF
        new_cache->is_active = false;
        return NULL;
    }
       
    new_cache->buff_size = chars_read;
    new_cache->pos = 0;
    new_cache->offset = offset;
    new_cache->is_active = true;
    f->cache_count++;
    new_cache->created_count = f->cache_count;
    f->curr_cache = new_cache - f->cache;
    return new_cache;
}

/* This function is used by io61_seek to check if there is currently a cache that
 * contains data located at the offset position into the file. If so then it returns
 * this cache to io61_seek and io61_seek adjusts the cache->pos accordingly before 
 * io61_read/readc are called.
 */
cache_slot* find_cache_offset(io61_file *f, size_t offset) {
    cache_slot* curr_cache = NULL;
    for (int i = 0; i < NUM_CACHE; i++) {
        curr_cache = &f->cache[i];
        // if our our desired offset in the file is contained within curr_cache's
        // bounds then data we want is in this cache
        if (curr_cache->is_active && curr_cache->offset <= offset &&
            offset < curr_cache->offset + curr_cache->buff_size) {
            
            f->curr_cache = i;
            return curr_cache;    
        }
    }
    return NULL;
}

// io61_fdopen(fd, mode)
//    Return a new io61_file that reads from and/or writes to the given
//    file descriptor `fd`. `mode` is either O_RDONLY for a read-only file
//    or O_WRONLY for a write-only file. You need not support read/write
//    files.

io61_file* io61_fdopen(int fd, int mode) {
    assert(fd >= 0);
    io61_file* f = (io61_file*) malloc(sizeof(io61_file));
    f->fd = fd;
    f->mode = mode;
    f->filesize = io61_filesize(f);
    f->curr_cache = -1;
    f->cache_count = 0;
    if (f->filesize != -1) {
        if (mode == O_RDONLY) {
            f->file_data = mmap(NULL, f->filesize, PROT_READ, MAP_SHARED, fd, 0);
        }
        f->file_offset = 0;
    }
    return f;
}


// io61_close(f)
//    Close the io61_file `f` and release all its resources, including
//    any buffers.

int io61_close(io61_file* f) {
    io61_flush(f);
    if (f->filesize != -1 && f->mode == O_RDONLY) {
        munmap(f->file_data, f->filesize);
    }
    int r = close(f->fd);
    free(f);
    return r;
}


// io61_readc(f)
//    Read a single (unsigned) character from `f` and return it. Returns EOF
//    (which is -1) on error or end-of-file.

int io61_readc(io61_file* f) {
    // If there is no curr_cache, fill cache from the begining of the file    
    if (!get_curr_cache(f)) {
        cache_slot* new_cache = fill_new_cache(f, 0);
        if (new_cache == NULL) {
            return EOF;
        }
    }
    
    cache_slot* curr_cache = get_curr_cache(f);
    // If cache->pos < cache->buff_size, we still have chars to read from buffer
    if (curr_cache->pos < curr_cache->buff_size) {
        if (f->filesize == -1) {
            return curr_cache->arr_buf[curr_cache->pos++];
        } else {
            return curr_cache->str_buf[curr_cache->pos++];
        }
    } else {
        /* If we have finished reading from the buffer, fill a new cache starting at the
         * next block of data within the file and recursively call io61_readc.
         */
        cache_slot* new_cache = fill_new_cache(f, curr_cache->offset + curr_cache->pos);
        if (new_cache == NULL) {
            return EOF;
        }       
        return io61_readc(f);
    }
}


// io61_read(f, buf, sz)
//    Read up to `sz` characters from `f` into `buf`. Returns the number of
//    characters read on success; normally this is `sz`. Returns a short
//    count if the file ended before `sz` characters could be read. Returns
//    -1 an error occurred before any characters were read.

ssize_t io61_read(io61_file* f, char* buf, size_t sz) {
    // If there is no curr_cache, fill cache from the begining of the file   
    if (!get_curr_cache(f)) {
        cache_slot* new_cache = fill_new_cache(f, 0);
        if (new_cache == NULL) {
            return 0;
        }
    }
    
    cache_slot* curr_cache = get_curr_cache(f);
    // If cache->pos < cache->buff_size, we still have chars to read from buffer
    if (curr_cache->pos < curr_cache->buff_size) {
        // nread is total bytes read from the file so far
        size_t nread = 0;
        // char_left is how many chars left to be read from buffer
        size_t char_left = 0;
        // total_char_left is how many chars left to be read from the file
        size_t total_char_left = sz;
        while (nread != sz) {
            total_char_left -= nread;
            char_left = curr_cache->buff_size - curr_cache->pos;
            /* If chars left in buffer are less than chars left in file, we are not
             * at the last block of the file yet so we just memcpy whats left of the
             * buffer into our destination buf and increment nread and cache's pos.
             */
            if (char_left < total_char_left) {
                if (f->filesize == -1) {
                    memcpy(&buf[nread], &curr_cache->arr_buf[curr_cache->pos], char_left);
                } else {
                    memcpy(&buf[nread], &curr_cache->str_buf[curr_cache->pos], char_left);
                }
                    nread += char_left;
                    curr_cache->pos += char_left;
            } else {
            /* Otherwise we just memcpy total_char_left bytes from our cache's buffer
             * into our desintation buf and increment nread and cache's pos.
             */
                if (f->filesize == -1) {
                    memcpy(&buf[nread], &curr_cache->arr_buf[curr_cache->pos], total_char_left);
                } else {
                    memcpy(&buf[nread], &curr_cache->str_buf[curr_cache->pos], total_char_left);
                }
                nread+= total_char_left;
                curr_cache->pos += total_char_left;
            }
            /* If we have finished reading from the buffer, fill a new cache starting at the
             * next block of data within the file and set curr_cache to the new cache we filled.
             */
            if (curr_cache->pos == curr_cache->buff_size) {
                cache_slot* new_cache = fill_new_cache(f, curr_cache->offset + curr_cache->pos);
                if (new_cache == NULL) {
                    return nread;
                }
                curr_cache = new_cache;
            }
        }
        return nread;
    } else {
        /* If for some reason the cache we filled at the begining didn't work we fill the next
         * cache and recursively call io61_read.  We should not reach this case though.
         */
        cache_slot* new_cache = fill_new_cache(f, curr_cache->offset + curr_cache->pos);
        if (new_cache == NULL) {
            return 0;
        }
        return io61_read(f, buf, sz);
    }
}


// io61_writec(f)
//    Write a single character `ch` to `f`. Returns 0 on success or
//    -1 on error.

int io61_writec(io61_file* f, int ch) {
    // If there is no curr_cache, set cache[0] as our current cache
    if(!get_curr_cache(f)) {
        f->curr_cache = 0;
        f->cache[0].buff_size = BUFFER_SIZE;
        f->cache[0].pos = 0;
        f->cache[0].is_active = true;
    }
    cache_slot* curr_cache = get_curr_cache(f);
    /* If there are still chars left to fill in our buffer, put ch in pos
     * and increment cache's pos.
     */
    if (curr_cache->pos < curr_cache->buff_size) {
        curr_cache->arr_buf[curr_cache->pos] = ch;
        curr_cache->pos++;
        return 0;
    } else {
        /* If the cache is full then flush buffer and recursively call io61_writec
         */
        io61_flush(f);
        return io61_writec(f, ch);
    }
}


// io61_write(f, buf, sz)
//    Write `sz` characters from `buf` to `f`. Returns the number of
//    characters written on success; normally this is `sz`. Returns -1 if
//    an error occurred before any characters were written.

ssize_t io61_write(io61_file* f, const char* buf, size_t sz) {
    // If there is no curr_cache, set cache[0] as our current cache
    if (!get_curr_cache(f)) {
        f->curr_cache = 0;
        f->cache[0].buff_size = BUFFER_SIZE;
        f->cache[0].pos = 0;
        f->cache[0].is_active = true;
    }
    
    cache_slot* curr_cache = get_curr_cache(f);
    /* If there are still chars left to fill in our buffer, put ch in pos
     * and increment cache's pos.
     */
    if (curr_cache->pos < curr_cache->buff_size) {
        // nwritten is the total number of chars written so far
        size_t nwritten = 0;
        // char_left is the number of chars left to fill in our cache
        size_t char_left = 0;
        // total_char_left is the number of chars left to write from buf
        size_t total_char_left = sz;
        while (nwritten != sz) {
            total_char_left -= nwritten;
            char_left = curr_cache->buff_size - curr_cache->pos;
            /* If chars left in buffer are less than chars left in file, we are not
             * at the last block of the file yet so we just memcpy char_left bytes
             * from buf into the buffer for our current cache and increment cache's pos
             */
            if (char_left < total_char_left) {
                memcpy(&curr_cache->arr_buf[curr_cache->pos], &buf[nwritten], char_left);
                nwritten += char_left;
                curr_cache->pos += char_left;
            } else {
            /* Otherwise we just memcpy total_char_left bytes from buf into the buffer
             * for our current cache and increment cache's pos.
             */
                memcpy(&curr_cache->arr_buf[curr_cache->pos], &buf[nwritten], total_char_left);
                nwritten += total_char_left;
                curr_cache->pos += total_char_left;
            }
            /* If we have finished writing to the buffer, flush the cache which resets buffer.
             */
            if (curr_cache->pos == curr_cache->buff_size) {
                io61_flush(f);
            }
        }
        return nwritten;
    } else {
        /* If for some reason the cache we got from the first statment didnt work we flush 
         * the cache and recursively call io61_write.  We should not reach this case though.
         */
        io61_flush(f);
        return io61_write(f, buf, sz);
    }
}


// io61_flush(f)
//    Forces a write of all buffered data written to `f`.
//    If `f` was opened read-only, io61_flush(f) may either drop all
//    data buffered for reading, or do nothing.

int io61_flush(io61_file* f) {
    if (f->mode != O_WRONLY) {
        return 0;
    }
    for (int i = 0; i < NUM_CACHE; i++) {
        cache_slot* curr_cache = &f->cache[i];
        /* Cycle through each cache slot and write cache->pos bytes to the buffer.
         * Because we increment pos for the bytes we add to the buffer, cache->pos
         * will always be the # of bytes we have in our buffer so far.  Will usually
         * be BUFFER_SIZE unless we are got our data from a buffer smaller than
         * BUFFER_SIZE.
         */
        if (curr_cache->pos > 0) {
            write(f->fd, curr_cache->arr_buf, curr_cache->pos);
            curr_cache->pos = 0;
        }
    }
    return 0;
}


// io61_seek(f, pos)
//    Change the file pointer for file `f` to `pos` bytes into the file.
//    Returns 0 on success and -1 on failure.

int io61_seek(io61_file* f, off_t pos) {
    off_t r = lseek(f->fd, (off_t) pos, SEEK_SET);    
    
    if (f->mode == O_RDONLY) {
        
        cache_slot* curr_cache = get_curr_cache(f);
        // Check if our curr_cache's offset in the file is 1 byte past our new
        // desired offset.  Then we know we are looking at the file in reverse
        // and can fill our cache more efficiently
        if (curr_cache) {
            int pos_diff = pos - (curr_cache->offset + curr_cache->pos - 1);
            if (pos_diff == -1 && !find_cache_offset(f, pos + pos_diff)) {
                cache_slot* new_cache;
                if (pos < BUFFER_SIZE) {
                    new_cache = fill_new_cache(f, 0);
                } else {
                    new_cache = fill_new_cache(f, pos - BUFFER_SIZE + 1);
                }
                if (new_cache == NULL) {
                    return 0;
                }
            }
        } 
        
        // If there is not cache which contains data within its buffer at pos
        // offset from the begining of the file, create a cache with that offset
        if (!find_cache_offset(f, pos)) {
            cache_slot* new_cache = fill_new_cache(f, pos);
            if (new_cache == NULL) {
                return 0;
            }
        }
        // Get current cache for this offset pos and adjust the pos value to the 
        // correct byte within the buffer
        curr_cache = get_curr_cache(f);
        curr_cache->pos = pos - curr_cache->offset;
    } else {
        io61_flush(f);
    }
    if (r == (off_t) pos)
        return 0;
    else
        return -1;
}


// You shouldn't need to change these functions.

// io61_open_check(filename, mode)
//    Open the file corresponding to `filename` and return its io61_file.
//    If `filename == NULL`, returns either the standard input or the
//    standard output, depending on `mode`. Exits with an error message if
//    `filename != NULL` and the named file cannot be opened.

io61_file* io61_open_check(const char* filename, int mode) {
    int fd;
    if (filename)
        fd = open(filename, mode, 0666);
    else if ((mode & O_ACCMODE) == O_RDONLY)
        fd = STDIN_FILENO;
    else
        fd = STDOUT_FILENO;
    if (fd < 0) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(1);
    }
    return io61_fdopen(fd, mode & O_ACCMODE);
}


// io61_filesize(f)
//    Return the size of `f` in bytes. Returns -1 if `f` does not have a
//    well-defined size (for instance, if it is a pipe).

off_t io61_filesize(io61_file* f) {
    struct stat s;
    int r = fstat(f->fd, &s);
    if (r >= 0 && S_ISREG(s.st_mode))
        return s.st_size;
    else
        return -1;
}


// io61_eof(f)
//    Test if readable file `f` is at end-of-file. Should only be called
//    immediately after a `read` call that returned 0 or -1.

int io61_eof(io61_file* f) {
    char x;
    ssize_t nread = read(f->fd, &x, 1);
    if (nread == 1) {
        fprintf(stderr, "Error: io61_eof called improperly\n\
  (Only call immediately after a read() that returned 0 or -1.)\n");
        abort();
    }
    return nread == 0;
}
