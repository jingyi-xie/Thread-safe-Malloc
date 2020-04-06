#include "my_malloc.h"
#define META_SIZE sizeof(meta_t)

//Global variable to store the head of the linked list
meta_t * head_lock = NULL;
__thread meta_t * head_tls = NULL;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

//Add the current node to the linked list, handle merge in the process
void addToList(meta_t * ptr, meta_t ** head_dp) {
  //printf("Debug: add %d at %lu\n", (int) ptr->size, (uintptr_t)ptr);
  if (*head_dp == NULL || *head_dp > ptr) {
    meta_t * oldHead = *head_dp;
    *head_dp = ptr;
    ptr->next = oldHead;
    if ((*head_dp)->next != NULL &&
        (void *)(*head_dp) + META_SIZE + (*head_dp)->size == (*head_dp)->next) {
      (*head_dp)->size += META_SIZE + (*head_dp)->next->size;
      (*head_dp)->next = (*head_dp)->next->next;
    }
    return;
  }
  meta_t * curr = *head_dp;
  while (curr != NULL && curr->next != NULL) {
    void * curr_end = (void *)curr + META_SIZE + curr->size;
    void * ptr_end = (void *)ptr + META_SIZE + ptr->size;
    if (curr_end == ptr) {
      //printf("Debug: merge\n");
      curr->size += META_SIZE + ptr->size;
      //After merge, check if the newly merged block needs merge again
      if ((void *)curr + META_SIZE + curr->size == curr->next) {
        //printf("Debug: merge again\n");
        curr->size += META_SIZE + curr->next->size;
        curr->next = curr->next->next;
      }
      return;
    }
    if (ptr_end == curr->next) {
      //printf("Debug: merge\n");
      void * temp = (void *)curr->next - ptr->size - META_SIZE;
      meta_t * new = temp;
      new->size = curr->next->size + META_SIZE + ptr->size;
      new->next = curr->next->next;
      curr->next = new;
      //After merge, check if the newly merged block needs merge again
      if ((void *)curr + META_SIZE + curr->size == curr->next) {
        //printf("Debug: merge again\n");
        curr->size += META_SIZE + curr->next->size;
        curr->next = curr->next->next;
      }
      return;
    }
    //The order of the list is the ascending order of pointers
    if (curr->next > ptr) {
      meta_t * temp = curr->next;
      curr->next = ptr;
      ptr->next = temp;
      return;
    }
    curr = curr->next;
  }
  //Add to the end of list, check if need to merge first
  if ((void *)curr + META_SIZE + curr->size == ptr) {
    curr->size += META_SIZE + ptr->size;
  }
  else {
    curr->next = ptr;
    ptr->next = NULL;
  }
}

//Split the block.
//Threshold: the size of this data segment is larger than 2 * size + META_SIZE
void splitBlock(size_t size, meta_t * toSplit, meta_t ** head_dp) {
  if (toSplit->size > 2 * size + META_SIZE) {
    //printf("Split!\n");
    void * temp = (void *)toSplit + META_SIZE + size;
    meta_t * newBlk = temp;
    newBlk->size = toSplit->size - size - META_SIZE;
    newBlk->next = NULL;
    addToList(newBlk, head_dp);
    toSplit->size = size;
  }
}

//Recursive helper function to delete the node in the list
meta_t * deleteHelper(meta_t ** curr_dp, meta_t * toDelete) {
  if (*curr_dp == NULL) {
    return NULL;
  }
  if (*curr_dp == toDelete) {
    meta_t * res = (*curr_dp)->next;
    return res;
  }
  (*curr_dp)->next = deleteHelper(&(*curr_dp)->next, toDelete);
  return *curr_dp;
}

void deleteList(meta_t * node, meta_t ** head_dp) {
  if (node == NULL) {
    return;
  }
  //printf("Debug: delete %d\n", (int) node->size);
  *head_dp = deleteHelper(head_dp, node);
}

//Find the best fit. If found the exact size as requested, abort the search.
meta_t * bf_find_free(size_t size, meta_t * head) {
  size_t best_dif = SIZE_MAX;
  meta_t * curr = head;
  meta_t * res = NULL;
  while (curr != NULL) {
    if (curr->size == size) {
      return curr;
    }
    if (curr->size >= size && best_dif > curr->size - size) {
      res = curr;
      best_dif = curr->size - size;
    }
    curr = curr->next;
  }
  return res;
}

//Create new data region with system call sbrk()
//Update data_segment_size
meta_t * create_newspace(size_t size, int lockVersion) {
  meta_t * res = NULL;
  void * blk = NULL;
  if (lockVersion == 1) {
    res = sbrk(0);
    blk = sbrk(size + META_SIZE);
  }
  else {
    pthread_mutex_lock(&lock);
    res = sbrk(0);
    blk = sbrk(size + META_SIZE);
    pthread_mutex_unlock(&lock);
  }
  if (blk == (void *)-1) {
    return NULL;
  }
  res->size = size;
  res->next = NULL;
  //printf("Debug: create new space of size %d at %lu\n", (int)size, (uintptr_t)res);
  return res;
}

//Best fit malloc.
void * my_malloc(size_t size, meta_t ** head, int lockVersion) {
  if (size < 0) {
    return NULL;
  }
  meta_t * blk = bf_find_free(size, *head);
  //if (blk != NULL) {
  //printf("Debug: expect size:%d, bf found %d\n", (int)size, (int) blk->size);
  //}
  if (blk == NULL) {
    blk = create_newspace(size, lockVersion);
  }
  else {
    splitBlock(size, blk, head);
    deleteList(blk, head);
    //print();
  }
  return (blk + 1);
}

void my_free(void * ptr, meta_t ** head) {
  if (ptr == NULL) {
    return;
  }
  meta_t * meta_ptr = (meta_t *)ptr - 1;
  addToList(meta_ptr, head);
}

void * ts_malloc_lock(size_t size) {
  pthread_mutex_lock(&lock);
  void * res = my_malloc(size, &head_lock, 1);
  pthread_mutex_unlock(&lock);
  return res;
}
void ts_free_lock(void * ptr) {
  pthread_mutex_lock(&lock);
  my_free(ptr, &head_lock);
  pthread_mutex_unlock(&lock);
}

void * ts_malloc_nolock(size_t size) {
  return my_malloc(size, &head_tls, 0);
}
void ts_free_nolock(void * ptr) {
  my_free(ptr, &head_tls);
}