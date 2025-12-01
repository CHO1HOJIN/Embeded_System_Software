//////////////////////////////////////////////////////////////////////////////////
// hash.h for Cosmos+ OpenSSD
// Simple chaining hash table helper (caller-managed storage)
//////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stddef.h>
#include <stdint.h>

#define HASH_NULL 0xFFFFFFFFu

typedef struct hash_node {
	uint32_t key;
	uint32_t lba;
	uint32_t next;
} hash_node_t;

typedef struct hash_table {
	uint32_t bucket_count;
	uint32_t node_capacity;
	uint32_t node_count;
	uint32_t* buckets;
	hash_node_t* nodes;
} hash_table_t;

uint32_t hash_find(const hash_table_t* ht, uint32_t key);

uint32_t hash_insert(hash_table_t* ht, uint32_t key, uint32_t lba);

uint32_t hash_upsert(hash_table_t* ht, uint32_t key, uint32_t lba);