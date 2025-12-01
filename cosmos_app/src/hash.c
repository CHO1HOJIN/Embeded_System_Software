#include "hash.h"

uint32_t hash_find(const hash_table_t* ht, uint32_t key)
{
	uint32_t idx;
	uint32_t h;
	if(!ht || ht->bucket_count == 0)
		return HASH_NULL;
	h = key % ht->bucket_count;
	idx = ht->buckets[h];
	while(idx != HASH_NULL)
	{
		if(ht->nodes[idx].key == key)
			return idx;
		idx = ht->nodes[idx].next;
	}
	return HASH_NULL;
}

uint32_t hash_insert(hash_table_t* ht, uint32_t key, uint32_t lba)
{
	uint32_t idx, h;
	if(ht->node_count >= ht->node_capacity)
		return HASH_NULL;
	idx = ht->node_count++;
	h = key % ht->bucket_count;
	ht->nodes[idx].key = key;
	ht->nodes[idx].lba = lba;
	ht->nodes[idx].next = ht->buckets[h];
	ht->buckets[h] = idx;
	return idx;
}

uint32_t hash_upsert(hash_table_t* ht, uint32_t key, uint32_t lba)
{
	uint32_t idx;
	idx = hash_find(ht, key);
	if(idx != HASH_NULL)
	{
		ht->nodes[idx].lba = lba;
		return idx;
	}
	return hash_insert(ht, key, lba);
}