#define M61_DISABLE 1
#include "m61.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <stdbool.h>

// To check for wild writes
#define default_head 1234
#define default_foot 4321

// Default size for hash table for hhtest
#define table_size 1000

// Static struct to keep track of stats
static struct m61_statistics total_stats = {
    0, 0, 0, 0, 0, 0, NULL, NULL
};

// need to create some array of these to update and keep track of each
// hhtest line and update the amount of times it's happened.
typedef struct heavy_stats {
  struct heavy_stats* next;
  size_t size;
  const char *file;
  int line;
  unsigned long long allocs;
} heavy_stats;

struct heavy_stats* hash_table[table_size];

// Meta structure to keep track of info about each dynamic allocation
typedef struct m61_meta {
    int header;
    size_t size;
    bool is_active;
    const char *file;
    int line;
    struct m61_meta *next;
    struct m61_meta *prev;
} m61_meta;

// Footer to check for wild writes/boundary problems
typedef struct m61_foot {
    int footer;
} m61_foot;

// Pointer to keep track of memory leaks
struct m61_meta *root;

// Function to find padding to make sure all memory is aligned
// to double word size based on size of meta structure.
size_t find_pad(void) {
    if (sizeof(m61_meta) <= (2 * sizeof(long long)))
        return (2 * sizeof(long long)) - sizeof(m61_meta);
    else
        return sizeof(m61_meta) % sizeof(long long);
}

// Function to find the block of allocated memory that a given ptr is
// within and return the pointer to the meta data of that structure by
// using our root pointer.
m61_meta* find_meta(void* ptr, m61_meta* node) {
    if(!node)
        return NULL;
    size_t l_bound = (size_t) (node + 1);
    size_t u_bound = l_bound + node->size;
    if (l_bound <= (size_t) ptr && (size_t) ptr <= u_bound) {
         return node;
    }
    else {
      return find_meta(ptr, node->prev);
    }
}

// hash32shiftmult function from link in README.txt
int hash(int key)
{
  int c2=0x27d4eb2d; // a prime or an odd constant
  key = (key ^ 61) ^ (key >> 16);
  key = key + (key << 3);
  key = key ^ (key >> 4);
  key = key * c2;
  key = key ^ (key >> 15);
  return key;
}

void fill_heavy(m61_meta* meta) { 
  int key = (int) meta->file + meta->line;
  int index = hash(key) % table_size;
  if (!hash_table[index]) {
    hash_table[index] = (heavy_stats*) malloc(sizeof(heavy_stats));
    hash_table[index]->size = meta->size;
    hash_table[index]->file = meta->file;
    hash_table[index]->line = meta->line;
    hash_table[index]->allocs = 1;
    hash_table[index]->next = NULL;
    return;
  }

  heavy_stats* curr = hash_table[index];
  heavy_stats* prev = hash_table[index];

  while(curr != NULL) {
    if (curr->file == meta->file && curr->line == meta->line) {
      break;
    }
    else {
      prev = curr;
      curr = curr->next;
    }
  }
  if (curr == NULL) {
    curr = (heavy_stats*) malloc(sizeof(heavy_stats));
    prev->next = curr;
    curr->allocs = 0;
    curr->size = meta->size;
    curr->file = meta->file;
    curr->line = meta->line;
    curr->next = NULL;
  }
  curr->allocs++;
}

void* m61_malloc(size_t sz, const char* file, int line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    
    size_t pad = find_pad();
    size_t new_sz = sz + pad + sizeof(m61_meta) + sizeof(m61_foot);
    
    // Checks for too large of size (size_t going negative)
    if (new_sz < sz || (int)sz < 0) {
        total_stats.nfail++;
        total_stats.fail_size += sz;
        return NULL;
    }
    
    char *ptr = malloc(new_sz);
    
    if (!ptr) {

        total_stats.nfail++;
        total_stats.fail_size += sz;
        return NULL;
    }
    else {
        char *tmp_min = ptr;
	char *tmp_max = ptr + new_sz;
    
	if (!total_stats.heap_min || tmp_min < total_stats.heap_min) {
	    total_stats.heap_min = tmp_min;
	}
    
	if (tmp_max > total_stats.heap_max) {
	    total_stats.heap_max = tmp_max;
	}
        
	// pointer to meta structure is pad bytes after ptr returned from malloc
	m61_meta *meta = (m61_meta*) (ptr + pad);
        
        meta->header = default_head;
        meta->size = sz;
        meta->is_active = true;
        meta->prev = NULL;
        meta->next = NULL;
        meta->file = file;
        meta->line = line;
        
        m61_foot *foot = (m61_foot*) (ptr + pad + sizeof(m61_meta) + sz);
        
        foot->footer = default_foot;
        
	// This adds meta to our linked list of active allocated memory
        if(!root){
            root = meta;
            root->next = NULL;
            root->prev = NULL;
        }
        else {
            root->next = meta;
            meta->prev = root;
            root = meta;
        }
        
        total_stats.ntotal++;
        total_stats.nactive++;
        total_stats.total_size += sz;
        total_stats.active_size += sz;
	fill_heavy(meta);
	
	// Return ptr to the payload requested
        return ptr + sizeof(m61_meta) + pad;
    }
}

void m61_free(void *ptr, const char *file, int line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    
    // No memory allocated yet means nothing to free
    if(total_stats.ntotal == 0) {
        printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not in heap\n", 
	       file, line, ptr);
        abort();
    }
    // No active memory or ptr is null means we cannot free that ptr
    if (total_stats.nactive == 0 || !ptr) {
        printf("MEMORY BUG: %s:%d: invalid free of pointer %p\n", 
	         file, line, ptr);
    }
    
    // Pointer arithmetic to find meta and footer
    m61_meta *meta = (m61_meta*) ptr - 1;
    m61_foot *foot = (m61_foot*) (ptr + meta->size);

    if (!meta->is_active) {
      
      // Use find_meta to find closest meta structure where ptr is within
      // that meta structures block
      m61_meta* found_ptr = find_meta(ptr, root);
      if (!found_ptr) {  
          printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n", 
		   file, line, ptr);
      }
      else {
	size_t offset = (size_t) ptr - (size_t) (found_ptr + 1);
	printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n",
	       file, line, ptr);
	printf("  %s:%d: %p is %zu bytes inside a %zu byte region allocated here\n",
	         found_ptr->file, found_ptr->line, ptr, offset, found_ptr->size);
      }
    }
    // If meta->header or foot->footer does not equal our default value, it
    // is likely that a wild write occured
    else if (meta->header != default_head 
	       || foot->footer != default_foot) {
        printf("MEMORY BUG: %s:%d: detected wild write during free of pointer %p\n",
	         file, line, ptr);
    }
    else if (meta->prev && meta->prev->next != meta) {
        printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n", 
	         file, line, ptr);
    }
    else {
    
        total_stats.nactive--;
        total_stats.active_size -= meta->size;
        meta->is_active = false;
	// Update heap min and max
        if((char*)meta < total_stats.heap_min) {
            total_stats.heap_min = (char*) meta->next;
        }
        if((char*)ptr + meta->size > total_stats.heap_max) {
            total_stats.heap_max = (char*) meta->prev; 
        }
	// Remove meta pointer from our linked list
        if(root == meta) {
            if(root->prev == NULL) {
                root = NULL;
            }
            else {
                root->prev->next = NULL;
                root = root->prev;
            }
        }
        else {
            if(meta->next != NULL) {
                meta->next->prev = meta->prev;
            }
            if(meta->prev != NULL) {
                meta->prev->next = meta->next;
            }
        }
	// free new_ptr which is the begining of our originally allocated
	// block of memory
        size_t pad = find_pad();
        void* new_ptr = ptr - sizeof(m61_meta) - pad;
        
        free(new_ptr);
    }
}

void* m61_realloc(void* ptr, size_t sz, const char* file, int line) {
    void* new_ptr = NULL;
    if (sz != 0)
        new_ptr = m61_malloc(sz, file, line);
    if (ptr && new_ptr) {
        size_t ptr_sz = ((m61_meta*) ptr - 1)->size;
	// if ptr_sz is less than sz, just memcpy ptr_sz bytes
        if(ptr_sz < sz) {
            memcpy(new_ptr, ptr, ptr_sz);
        }
	// else, memcpy up to sz bytes
        else {
            memcpy(new_ptr, ptr, sz);
        }
    }
    // free ptr and return new_ptr
    if (ptr) {
        m61_free(ptr, file, line);
    }
    return new_ptr;
}

void* m61_calloc(size_t nmemb, size_t sz, const char* file, int line) {
    // Your code here (to fix test014).
    
    void* ptr = NULL;
    // check if nmemb * sz > nmemb, if it is smaller nmemb * sz could have
    // wrapped around to a negative number
    if (nmemb * sz >= nmemb) {       
        ptr = m61_malloc(nmemb * sz, file, line);
    }
    if (ptr) {
        memset(ptr, 0, nmemb * sz);
    }
    else {
        total_stats.nfail++;
        total_stats.fail_size += nmemb * sz;
    }
    return ptr;
}

void m61_getstatistics(struct m61_statistics* stats) {
    // Stub: set all statistics to enormous numbers
    memset(stats, 255, sizeof(struct m61_statistics));
    
    // sets stats to total_stats
    *stats = total_stats;
}

void m61_printstatistics(void) {
    struct m61_statistics stats;
    m61_getstatistics(&stats);

    printf("malloc count: active %10llu   total %10llu   fail %10llu\n",
           stats.nactive, stats.ntotal, stats.nfail);
    printf("malloc size:  active %10llu   total %10llu   fail %10llu\n",
           stats.active_size, stats.total_size, stats.fail_size);
}

void m61_printleakreport(void) {
    m61_meta* meta = root;
    while(meta) {
        printf("LEAK CHECK: %s:%d: allocated object %p with size %zu\n", 
	       meta->file, meta->line, meta + 1, meta->size);
        meta = meta->prev;
    }
}

void m61_printheavyreport(void) {
  heavy_stats* curr;

  for(int i = 0; i < table_size; i++) {
    curr = hash_table[i];

    if (!curr)
      continue;
    else {
      
      while(curr != NULL) {

	  // line_size = total amt of memory a given file-line pair has allocated
	  unsigned long long line_size = hash_table[i]->size * hash_table[i]->allocs;

	  // percent is the percent of total memory a file-line pair has allocated
	  float percent = ((float) line_size) / ((float) total_stats.total_size);

	  percent *= 100.0;
	  if (percent >= 20.0) {
	    printf("HEAVY HITTER: %s:%d: %llu bytes (~%f)\n", 
		   curr->file, curr->line, line_size , percent);
	  }
	  curr = curr->next;
      }
    }
  }
}
