//This program generates a database that stores all
 //*  combinations of the edge pieces of a Rubik's cube.
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include "moves.h"
#include "edatabase.h"

#ifndef NUM_THREADS
  #define NUM_THREADS 1
#endif

static atomic_uchar *database;
static uint8_t comb[NUM_THREADS][NUM_EDGES];
static uint8_t temp[NUM_THREADS][NUM_EDGES];
static unsigned depth;
static uint64_t hvalues[20];

static void breadth_first_search(int offset);
static void write_DB(void);

int main(void) {

  // Get memory for the database.
  if ((database=(atomic_uchar*)malloc(E_DB_SIZE)) == NULL) {
    perror("Not enough memory for database.\n");
    exit(1);
  }

  // Set first entry to 0 and all other states to 0xFF.
  database[0] = 0x0F;
  for (uint64_t i=1; i<E_DB_SIZE; i++)
    database[i] = 0xFF;

  // Put all turn functions in a static array of functions called moves.
  initialize_turns();

  // Show progress tracker.
  update_percent();

  // Do BFS by expanding only nodes of a given depth on each pass until database is full.
  depth = 0;
  int done=0;
  #if NUM_THREADS > 1
  void *multiBFS(void *offset);
  #endif
  while (!done) {

    #if NUM_THREADS > 1
    pthread_t t[NUM_THREADS];
    int offset[NUM_THREADS];
    for (int i=0; i<NUM_THREADS; i++) {
      offset[i] = i;
      pthread_create(&t[i], NULL, multiBFS, (void*)(offset+i));
    }
    void* retval[NUM_THREADS];
    for (int i=0; i<NUM_THREADS; i++)
      pthread_join(t[i], &retval[i]);
    for (int i=0; i<NUM_THREADS; i++) {
      hvalues[depth] += *((uint64_t*)retval[i]);
     // free((uint64_t*)retval[i]);
    }
    if (hvalues[depth] == 0)
      done = 1;
    depth++;
    #else
    done = 1;
    for (uint64_t i=0; i<E_DB_SIZE; i++) {

      if ((unsigned char)(database[i]>>4) == depth) {
        FIND_COMB(i*2, comb[0]);
        breadth_first_search(0);
        hvalues[depth]++;
        done = 0;
      }
      if ((database[i]&0x0F) == depth) {
        FIND_COMB(i*2+1, comb[0]);
        breadth_first_search(0);
        hvalues[depth]++;
        done = 0;
      }
    }
    depth++;
    #endif
  }
  printf("\rDatabase generation: 100%%   \n");
  // Write database to a file.
  write_DB();

  // Print table of heuristic values.
  for (unsigned i=0; i<depth-1; i++)
    printf("%2u move to solve:  %lu\n", i, hvalues[i]);

  printf("Done.\n");
}

#if NUM_THREADS > 1
void *multiBFS(void *offset) {
  uint64_t *total = (uint64_t*)malloc(sizeof(uint64_t));
  uint64_t dboffset = *((int*)offset);
  *total = 0;
  for (uint64_t i=E_DB_SIZE/NUM_THREADS*dboffset; i<E_DB_SIZE/NUM_THREADS*(dboffset+1); i++) {
    if (database[i]>>4 == depth) {
      FIND_COMB(i*2, comb[dboffset]);
      breadth_first_search(dboffset);
      *total += 1;
    }
    if ((database[i]&0x0F) == depth) {
      FIND_COMB(i*2+1, comb[dboffset]);
      breadth_first_search(dboffset);
      *total += 1;
    }
  }
  pthread_exit(total);
}
#endif

void breadth_first_search(int offset) {

  // Add NEW combinations to the end of the queue
  uint64_t index, add, pos;
  for (int i=0; i<18; i++) {

    // If turn affects edges cubes that we care about.
    if ((*movesE[i])(comb[offset], temp[offset])) {

      // If new combination hasn't been seen, add value of depth+1 to database.
      index = GET_INDEX(temp[offset]);
      add = index / 2;
      pos = index % 2;  // 0 = left 4 bits  1 = right 4 bits
      if ((pos ? database[add] & 0x0F : database[add] >> 4) == 15) {

        // Add depth+1 to database.
        if (pos)
          database[add] = database[add] & ((depth+1) | 0xF0);
        else
          database[add] = database[add] & (((depth+1) << 4) | 0x0F);

        // Increase fill amount (atomic).
        fill_amount++;
        if ((double)fill_amount / (E_DB_SIZE * 2) >= fill_percent + .00001)
          update_percent();
      }
    }
  }
}

void write_DB(void) {

  printf("Writing to file: 0%%");
  // Write database to a file.
  int fd;
  if ((fd=creat("pattern_databases/edges"FILENAME"_"TRACKED_NAME".patdb", 0644)) == -1) {
    perror("Unable to create file\n");
    exit(1);
  }

  int64_t amount = 0, remain = E_DB_SIZE;
  while ((amount=write(fd, database + E_DB_SIZE - remain, (remain>1048576) ? 1048576 : remain)) != remain) {
    if (amount == -1) {
      perror("Problem writing to file.\n");
      exit(1);
    }
    remain -= amount;
    printf("\rWriting to file: %-.0lf%%", (1.0-(double)remain/E_DB_SIZE) * 100);
  }

  if (close(fd) == -1) {
    perror("There was a problem closing the file.\n");
    exit(1);
  }
    printf("\rWriting to file: 100%%\n");
}
