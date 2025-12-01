//////////////////////////////////////////////////////////////////////////////////
// nvme_io_cmd.c for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Youngjin Jo <yjjo@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// This file is part of Cosmos+ OpenSSD.
//
// Cosmos+ OpenSSD is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// Cosmos+ OpenSSD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Cosmos+ OpenSSD; see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Company: ENC Lab. <http://enc.hanyang.ac.kr>
// Engineer: Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//			 Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: NVMe IO Command Handler
// File Name: nvme_io_cmd.c
//
// Version: v1.0.1
//
// Description:
//   - handles NVMe IO command
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.1
//   - header file for buffer is changed from "ia_lru_buffer.h" to "lru_buffer.h"
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////


#include "xil_printf.h"
#include "debug.h"
#include "io_access.h"

#include "nvme.h"
#include "host_lld.h"
#include "nvme_io_cmd.h"

#include "../ftl_config.h"
#include "../request_transform.h"
#include "../memory_map.h"
#include "../hash.h"

static hash_table_t kvHash;

void InitHash()
{
	kvHash.node_count = 0;
	kvHash.buckets = (uint32_t*)BUCKET_BASE;
	kvHash.nodes = (hash_node_t*)NODE_BASE;
	
	for(uint32_t i = 0; i < HASH_COUNT; i++){
		kvHash.buckets[i] = HASH_NULL;
		kvHash.nodes[i].key = 0;
		kvHash.nodes[i].lba = 0;
		kvHash.nodes[i].next = HASH_NULL;
	}

}

void handle_nvme_io_read(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
	IO_READ_COMMAND_DW12 readInfo12;
	//IO_READ_COMMAND_DW13 readInfo13;
	//IO_READ_COMMAND_DW15 readInfo15;
	unsigned int startLba[2];
	unsigned int nlb;
	unsigned int nsid = nvmeIOCmd->NSID;
	

	readInfo12.dword = nvmeIOCmd->dword[12];
	//readInfo13.dword = nvmeIOCmd->dword[13];
	//readInfo15.dword = nvmeIOCmd->dword[15];

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = readInfo12.NLB;

	ASSERT(startLba[0] < storageCapacity_L / USER_CHANNELS && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0x3) == 0 && (nvmeIOCmd->PRP2[0] & 0x3) == 0); //error
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

	ReqTransNvmeToSlice(cmdSlotTag, startLba[0] + (storageCapacity_L / USER_CHANNELS) * (nsid - 1), nlb, IO_NVM_READ);
}


void handle_nvme_io_write(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
	IO_READ_COMMAND_DW12 writeInfo12;
	unsigned int startLba[2];
	unsigned int nlb;
	unsigned int nsid = nvmeIOCmd->NSID;

	writeInfo12.dword = nvmeIOCmd->dword[12];

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = writeInfo12.NLB;

	ASSERT(startLba[0] < storageCapacity_L / USER_CHANNELS && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	ASSERT((nvmeIOCmd->PRP1[0] & 0xF) == 0 && (nvmeIOCmd->PRP2[0] & 0xF) == 0);
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

	ReqTransNvmeToSlice(cmdSlotTag, startLba[0] + (storageCapacity_L / USER_CHANNELS) * (nsid - 1), nlb, IO_NVM_WRITE);
}

void handle_nvme_kv_put(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{
	NVME_COMPLETION nvmeCPL;
	uint32_t key, lba, idx;

	key = nvmeIOCmd->dword[10];
	idx = hash_find(&kvHash, key);

	if(idx != HASH_NULL){
		lba = kvHash.nodes[idx].lba;
	}
	else{
		lba = kvHash.node_count;
		idx = hash_insert(&kvHash, key, lba);
	}

	if(idx == HASH_NULL)
	{
		//DEBUG
		// xil_printf("HASH IS FULL! key=0x%08X\r\n", key);
		nvmeCPL.statusField.SC = SC_INTERNAL_DEVICE_ERROR;
		nvmeCPL.statusField.SCT = SCT_GENERIC_COMMAND_STATUS;
		set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
		return;
	}

	ASSERT((lba + 1) <= (storageCapacity_L / USER_CHANNELS));

	nvmeIOCmd->dword[10] = lba;
	nvmeIOCmd->dword[11] = 0;
	nvmeIOCmd->dword[12] = 0;

	//DEBUG
	xil_printf("KV_PUT key=0x%08X idx=%u lba=%u\r\n", key, idx, lba);

	handle_nvme_io_write(cmdSlotTag, nvmeIOCmd);

	return;
}

void handle_nvme_kv_get(unsigned int cmdSlotTag, NVME_IO_COMMAND *nvmeIOCmd)
{	
	NVME_COMPLETION nvmeCPL;
	uint32_t key, lba, idx;

	nvmeCPL.dword[0] = 0;
	nvmeCPL.specific = 0x0;

	key = nvmeIOCmd->dword[10];

	idx = hash_find(&kvHash, key);
	if(idx == HASH_NULL)
	{
		//DEBUG
		// xil_printf("KV_GET miss: key=0x%08X\r\n", key);
		nvmeCPL.statusField.SC = NVME_STATUS_NO_SUCH_KEY;
		nvmeCPL.statusField.SCT = SCT_VENDOR_SPECIFIC;
		set_auto_nvme_cpl(cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
		return;
	}

	lba = kvHash.nodes[idx].lba;
	ASSERT((lba + 1) <= (storageCapacity_L / USER_CHANNELS));

	nvmeIOCmd->dword[10] = lba;
	nvmeIOCmd->dword[11] = 0;
	nvmeIOCmd->dword[12] = 0;

	//DEBUG
	xil_printf("KV_GET key=0x%08X idx=%u lba=%u\r\n", key, idx, lba);

	ReqTransNvmeToSlice(cmdSlotTag, lba, 0, IO_NVM_KV_GET);

	return;
}
void handle_nvme_io_cmd(NVME_COMMAND *nvmeCmd)
{
	NVME_IO_COMMAND *nvmeIOCmd;
	NVME_COMPLETION nvmeCPL;
	unsigned int opc;
	nvmeIOCmd = (NVME_IO_COMMAND*)nvmeCmd->cmdDword;

	opc = (unsigned int)nvmeIOCmd->OPC;

	switch(opc)
	{
		case IO_NVM_FLUSH:
		{
		//	xil_printf("IO Flush Command\r\n");
			nvmeCPL.dword[0] = 0;
			nvmeCPL.specific = 0x0;
			set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
			break;
		}
		case IO_NVM_WRITE:
		{
			handle_nvme_io_write(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_NVM_READ:
		{
			handle_nvme_io_read(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_NVM_KV_PUT:
		{
			handle_nvme_kv_put(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_NVM_KV_GET:
		{
			handle_nvme_kv_get(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		default:
		{
			xil_printf("Not Support IO Command OPC: %X\r\n", opc);
			ASSERT(0);
			break;
		}
	}
}
