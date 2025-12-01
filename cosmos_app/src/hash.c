#include "hash.h"

uint32_t hash_find(const hash_table_t* kvHash, uint32_t key)
{
	uint32_t idx;
	uint32_t h;
	if(!kvHash) return HASH_NULL;
	h = key % HASH_COUNT;
	idx = kvHash->buckets[h];
	while(idx != HASH_NULL)
	{
		if(kvHash->nodes[idx].key == key)
			return idx;
		idx = kvHash->nodes[idx].next;
	}
	return HASH_NULL;
}

uint32_t hash_insert(hash_table_t* kvHash, uint32_t key, uint32_t lba)
{
	uint32_t idx, h;
	if(kvHash->node_count >= HASH_COUNT)
		return HASH_NULL;
	idx = kvHash->node_count++;
	h = key % HASH_COUNT;
	kvHash->nodes[idx].key = key;
	kvHash->nodes[idx].lba = lba;
	kvHash->nodes[idx].next = kvHash->buckets[h];
	kvHash->buckets[h] = idx;
	return idx;
}