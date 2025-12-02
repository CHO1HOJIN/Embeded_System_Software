//////////////////////////////////////////////////////////////////////////////////
// hash.h for Cosmos+ OpenSSD
// Simple chaining hash table helper (caller-managed storage)
//////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "memory_map.h"

#define HASH_NULL 0xFFFFFFFFu

typedef struct hash_node {
    uint32_t key;   // KV의 key 값
    uint32_t lba;   // 해당 key에 매핑된 LBA (Logical Block Address)
    uint32_t next;  // chaining을 위한 다음 노드 인덱스
} hash_node_t;

typedef struct hash_table {
    uint32_t node_count;    // 현재 저장된 노드 수
    uint32_t* buckets;      // 버킷 배열 (각 버킷은 연결 리스트의 head 인덱스)
    hash_node_t* nodes;     // 노드 배열 (실제 데이터 저장)
} hash_table_t;

uint32_t hash_find(const hash_table_t* ht, uint32_t key);
uint32_t hash_insert(hash_table_t* ht, uint32_t key, uint32_t lba);