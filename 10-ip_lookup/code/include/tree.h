#ifndef __TREE_H__
#define __TREE_H__

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

// do not change it
#define TEST_SIZE 100000

#define TRAIN_SIZE 697882 
#define I_NODE 0 // internal node
#define M_NODE 1 // match node
#define LEFT 0
#define RIGHT 1

#define MASK(x,y) (((x) & 0x000000ff) << (y))
typedef struct node{
    bool type; //I_NODE or M_NODE
    uint32_t port;
    struct node* lchild;
    struct node* rchild;
}node_t;

void create_tree(const char*);
uint32_t *lookup_tree(uint32_t *);
void create_tree_advance(const char*);
uint32_t *lookup_tree_advance(uint32_t *);

uint32_t* read_test_data(const char* lookup_file);
node_t* new_node();

// poptrie functions
typedef struct {
    uint64_t vector;
    uint64_t leafvec;
    uint32_t base1;
    uint32_t base0;
} __attribute__((packed)) poptrie_node_t;

typedef uint32_t direct_ptr_t;
#define DP_IS_INTERNAL(ptr) ((ptr) & 0x80000000) // MSB is 1 for internal node
#define DP_GET_INDEX(ptr)   ((ptr) & 0x7FFFFFFF)

typedef uint32_t poptrie_leaf_t;

typedef struct{
    direct_ptr_t* direct_array;
    int s;  

    poptrie_node_t* nodes;
    uint32_t node_count;
    uint32_t node_capacity;

    poptrie_leaf_t* leaves;
    uint32_t leaf_count;
    uint32_t leaf_capacity;
} poptrie_t;

#endif
