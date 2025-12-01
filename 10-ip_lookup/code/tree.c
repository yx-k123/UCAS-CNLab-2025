#include "include/tree.h"
#include <stdio.h>
#include <stdlib.h>

node_t *root_basic = NULL;    // root of basic tree
node_t *root_advance = NULL;  // root of advanced tree
poptrie_t poptrie;  // advanced tree structure

// return an array of ip represented by an unsigned integer, the length of array is TEST_SIZE
uint32_t* read_test_data(const char* lookup_file)
{
    // fprintf(stderr,"TODO:%s",__func__);
    uint32_t* ip_vec = (uint32_t*)malloc(sizeof(uint32_t)*TEST_SIZE);
    if(ip_vec == NULL){
        perror("Failed to allocate memory for ip_vec");
        return NULL;
    }

    FILE* fp = fopen(lookup_file,"r");
    if(fp == NULL){
        perror("Failed to open lookup file");
        free(ip_vec);
        return NULL;
    }

    char ip_str[32];
    int i = 0;
    while (i < TEST_SIZE && fscanf(fp, "%s", ip_str) != EOF) {
        uint32_t a, b, c, d;
        if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            ip_vec[i] = (a << 24) | (b << 16) | (c << 8) | d;
            i++;
        }
    }

    fclose(fp);
    return ip_vec;
}

// Create a new basic trie node
node_t* new_node() {
    node_t* node = (node_t*)malloc(sizeof(node_t));
    if (node) {
        node->type = I_NODE; // 默认为中间节点
        node->port = 0;
        node->lchild = NULL;
        node->rchild = NULL;
    }
    return node;
}

// Constructing a trie-tree to lookup according to `forward_file`
void create_tree(const char* forward_file)
{
    FILE* fp = fopen(forward_file, "r");
    if (fp == NULL) {
        perror("Failed to open forwarding file");
        return;
    }

    uint32_t a, b, c, d, prefix_len, port;
    
    while (fscanf(fp, "%u.%u.%u.%u %u %u", &a, &b, &c, &d, &prefix_len, &port) == 6) {
        uint32_t ip = (a << 24) | (b << 16) | (c << 8) | d;
        
        node_t** current = &root_basic;
        
        for (int j = 0; j < prefix_len; j++) {
            int bit = (ip >> (31 - j)) & 1;
            
            if (*current == NULL) {
                *current = new_node();
            }
            
            current = (bit == 0) ? &((*current)->lchild) : &((*current)->rchild);
        }

        if (*current == NULL) {
            *current = new_node();
        }
        
        (*current)->type = M_NODE;
        (*current)->port = port;
    }

    fclose(fp);
}

// Look up the ports of ip in file `lookup_file` using the basic tree
uint32_t *lookup_tree(uint32_t* ip_vec)
{
    uint32_t* port_vec = (uint32_t*)malloc(sizeof(uint32_t) * TEST_SIZE);
    if (port_vec == NULL) {
        perror("Failed to allocate memory for port_vec");
        return NULL;
    }

    for (int i = 0; i < TEST_SIZE; i++) {
        uint32_t ip = ip_vec[i];
        node_t* current = root_basic;
        uint32_t last_port = -1; // Default port if no match found

        for (int j = 0; j < 32; j++) {
            if (current == NULL) {
                break;
            }

            if (current->type == M_NODE) {
                last_port = current->port;
            }

            int bit = (ip >> (31 - j)) & 1;
            current = (bit == 0) ? current->lchild : current->rchild;
        }

        if (current != NULL && current->type == M_NODE) {
            last_port = current->port;
        }
        
        port_vec[i] = last_port;
    }

    return port_vec;
}

// advanced
#define POPCNT(x) __builtin_popcountll(x)
#define STRIDE 6
#define DIRECT_BITS 18
#define DIRECT_SIZE (1 << DIRECT_BITS)

void init_poptrie() {
    poptrie.s = DIRECT_BITS;
    poptrie.direct_array = (direct_ptr_t*)calloc(DIRECT_SIZE, sizeof(direct_ptr_t));
    
    poptrie.node_capacity = 2000000; 
    poptrie.node_count = 0;
    poptrie.nodes = (poptrie_node_t*)calloc(poptrie.node_capacity, sizeof(poptrie_node_t));

    poptrie.leaf_capacity = 2000000;
    poptrie.leaf_count = 0;
    poptrie.leaves = (poptrie_leaf_t*)malloc(sizeof(poptrie_leaf_t) * poptrie.leaf_capacity);
}

void fill_poptrie_node(uint32_t p_node_idx, node_t* b_node, uint32_t default_port) {
    poptrie_node_t* p_node = &poptrie.nodes[p_node_idx];
    if (b_node && b_node->type == M_NODE) default_port = b_node->port;

    uint32_t ports[64];
    node_t* next_nodes[64];
    
    // Lookahead 6 bits
    for (int i = 0; i < 64; i++) {
        node_t* curr = b_node;
        uint32_t temp_port = default_port;
        bool is_internal = false;
        
        if (curr) {
            for (int bit_idx = 0; bit_idx < STRIDE; bit_idx++) {
                int bit = (i >> (STRIDE - 1 - bit_idx)) & 1;
                if (curr->type == M_NODE) temp_port = curr->port;
                curr = (bit == 0) ? curr->lchild : curr->rchild;
                if (!curr) break;
            }
        }
        
        if (curr) {
            if (curr->type == M_NODE) temp_port = curr->port;
            if (curr->lchild || curr->rchild) {
                is_internal = true;
                next_nodes[i] = curr;
            }
        }
        ports[i] = temp_port;
        next_nodes[i] = is_internal ? curr : NULL;
        if (is_internal) p_node->vector |= (1ULL << i);
    }

    // Allocate children nodes
    int internal_cnt = POPCNT(p_node->vector);
    if (internal_cnt > 0) {
        p_node->base1 = poptrie.node_count;
        poptrie.node_count += internal_cnt;
    }

    // Compress leaves
    uint32_t prev_port = 0x55AA55AA; // Impossible port to force first leaf recording
    for (int i = 0; i < 64; i++) {
        if (!((p_node->vector >> i) & 1)) { // Is Leaf
            if (ports[i] != prev_port) {
                p_node->leafvec |= (1ULL << i);
                prev_port = ports[i];
            }
        }
    }
    
    int leaf_cnt = POPCNT(p_node->leafvec);
    if (leaf_cnt > 0) {
        p_node->base0 = poptrie.leaf_count;
        poptrie.leaf_count += leaf_cnt;
        
        prev_port = 0x55AA55AA;
        int leaf_idx = 0;
        for (int i = 0; i < 64; i++) {
            if (!((p_node->vector >> i) & 1)) {
                 if (ports[i] != prev_port) {
                     poptrie.leaves[p_node->base0 + leaf_idx++] = ports[i];
                     prev_port = ports[i];
                 }
            }
        }
    }

    // Recursive calls for internal nodes
    int child_offset = 0;
    for (int i = 0; i < 64; i++) {
        if ((p_node->vector >> i) & 1) {
            fill_poptrie_node(p_node->base1 + child_offset++, next_nodes[i], ports[i]);
        }
    }
}

void fill_direct_pointing(node_t* curr, int depth, uint32_t idx, uint32_t default_port) {
    if (depth < DIRECT_BITS) {
        if (curr && curr->type == M_NODE) default_port = curr->port;

        if (curr == NULL) {
            // Fill remaining range with default port
            int remaining = DIRECT_BITS - depth;
            uint32_t start = idx << remaining;
            uint32_t end = (idx + 1) << remaining;
            
            uint32_t store_val = (default_port == 0xFFFFFFFF) ? 0x7FFFFFFF : default_port;
            for (uint32_t k = start; k < end; k++) {
                poptrie.direct_array[k] = store_val; // MSB 0 = Leaf
            }
            return;
        }
        fill_direct_pointing(curr->lchild, depth + 1, (idx << 1), default_port);
        fill_direct_pointing(curr->rchild, depth + 1, (idx << 1) | 1, default_port);
    } else {
        // Reached depth 18
        if (curr && curr->type == M_NODE) default_port = curr->port;
        
        if (curr == NULL || (curr->lchild == NULL && curr->rchild == NULL)) {
            uint32_t store_val = (default_port == 0xFFFFFFFF) ? 0x7FFFFFFF : default_port;
            poptrie.direct_array[idx] = store_val;
        } else {
            uint32_t node_idx = poptrie.node_count++;
            poptrie.direct_array[idx] = (1U << 31) | node_idx; // MSB 1 = Internal
            fill_poptrie_node(node_idx, curr, default_port);
        }
    }
}

// Constructing an advanced trie-tree to lookup according to `forward_file`
void create_tree_advance(const char* forward_file)
{
    // fprintf(stderr,"TODO:%s",__func__);
    // 1. Build basic binary trie
    if (root_basic != NULL) root_basic = NULL; // Assuming leak ignored for simplicity
    create_tree(forward_file);

    // 2. Build Poptrie
    init_poptrie();
    fill_direct_pointing(root_basic, 0, 0, 0xFFFFFFFF);
}

// Look up the ports of ip in file `ip_to_lookup.txt` using the advanced tree input is read from `read_test_data` func 
uint32_t *lookup_tree_advance(uint32_t* ip_vec)
{
    // fprintf(stderr,"TODO:%s",__func__);
    uint32_t* port_vec = (uint32_t*)malloc(sizeof(uint32_t) * TEST_SIZE);
    if (!port_vec) return NULL;

    for (int i = 0; i < TEST_SIZE; i++) {
        uint32_t ip = ip_vec[i];
        
        // Direct Pointing Lookup
        uint32_t dp_idx = ip >> (32 - DIRECT_BITS);
        direct_ptr_t dp_entry = poptrie.direct_array[dp_idx];

        if (!DP_IS_INTERNAL(dp_entry)) {
            uint32_t val = DP_GET_INDEX(dp_entry);
            if (val == 0x7FFFFFFF) port_vec[i] = 0xFFFFFFFF;
            else port_vec[i] = val;
            continue;
        }
        // printf("here\n");
        uint32_t node_idx = DP_GET_INDEX(dp_entry);
        poptrie_node_t* curr = &poptrie.nodes[node_idx];
        
        int current_bit = DIRECT_BITS; 
        while (current_bit < 32) {
            // Extract next 6 bits (or fewer if near end)
            uint32_t chunk = (ip << current_bit) >> (32 - STRIDE);
            
            if ((curr->vector >> chunk) & 1) {
                // Internal Node
                uint64_t mask = (1ULL << chunk) - 1;
                node_idx = curr->base1 + POPCNT(curr->vector & mask);
                curr = &poptrie.nodes[node_idx];
                current_bit += STRIDE;
            } else {
                // Leaf Node
                uint64_t mask = (chunk == 63) ? ~0ULL : ((1ULL << (chunk + 1)) - 1);
                int offset = POPCNT(curr->leafvec & mask);
                if (offset > 0)
                    port_vec[i] = poptrie.leaves[curr->base0 + offset - 1];
                else
                    port_vec[i] = -1; // Should not match here if logic correct
                goto next_ip;
            }
        }
        port_vec[i] = -1; // Fallback
        
        next_ip:;
    }
    return port_vec;
}
