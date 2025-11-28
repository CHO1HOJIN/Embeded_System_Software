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
#include "../address_translation.h"
#include "../memory_map.h"

P_KV_MAP kvMapPtr;
unsigned int kvNextLogicalSliceAddr;

// 해시 함수
unsigned int KvHash(unsigned int key) {
    return key % KV_MAX_ENTRIES;
}

void InitKvStore()
{
    unsigned int i;
    
    kvMapPtr = (P_KV_MAP)KV_MAP_ADDR;
    
    // KV 맵 초기화
    for (i = 0; i < KV_MAX_ENTRIES; i++) {
        kvMapPtr->kvEntry[i].key = 0;
        kvMapPtr->kvEntry[i].logicalSliceAddr = KV_ENTRY_NONE;
        kvMapPtr->kvEntry[i].valid = KV_ENTRY_INVALID;
    }
    
    // 논리 슬라이스 주소 초기화 (KV 전용 영역)
    kvNextLogicalSliceAddr = 0;
    
    xil_printf("[KV] Key-Value Store initialized (max %d entries)\r\n", KV_MAX_ENTRIES);
}

// 해시 테이블에서 키 조회 (Linear Probing)
// 반환: 찾으면 인덱스, 없으면 -1
int KvLookup(unsigned int key)
{
    unsigned int startIdx = KvHash(key);
    unsigned int idx = startIdx;
    unsigned int count = 0;
    
    while (count < KV_MAX_ENTRIES) {
        if (kvMapPtr->kvEntry[idx].valid == KV_ENTRY_INVALID) {
            // 빈 슬롯 만남 - 키 없음
            return -1;
        }
        
        if (kvMapPtr->kvEntry[idx].valid == KV_ENTRY_VALID &&
            kvMapPtr->kvEntry[idx].key == key) {
            // 키 찾음
            return (int)idx;
        }
        
        // 다음 슬롯으로 (Linear Probing)
        idx = (idx + 1) % KV_MAX_ENTRIES;
        count++;
    }
    
    return -1;  // 전체 탐색 후 없음
}

// 해시 테이블에서 삽입할 위치 찾기
// 반환: 삽입 가능한 인덱스, 가득 차면 -1
static int FindSlotForInsert(unsigned int key)
{
    unsigned int startIdx = KvHash(key);
    unsigned int idx = startIdx;
    unsigned int count = 0;
    
    while (count < KV_MAX_ENTRIES) {
        // 빈 슬롯이거나 같은 키가 있는 슬롯
        if (kvMapPtr->kvEntry[idx].valid == KV_ENTRY_INVALID) {
            return (int)idx;
        }
        
        if (kvMapPtr->kvEntry[idx].valid == KV_ENTRY_VALID &&
            kvMapPtr->kvEntry[idx].key == key) {
            // 기존 키 위치 반환 (업데이트용)
            return (int)idx;
        }
        
        // 다음 슬롯으로 (Linear Probing)
        idx = (idx + 1) % KV_MAX_ENTRIES;
        count++;
    }
    
    return -1;  // 테이블 가득 참
}

// 논리 슬라이스 주소 할당
static unsigned int AllocateLogicalSlice()
{
    unsigned int addr = kvNextLogicalSliceAddr;
    kvNextLogicalSliceAddr++;
    
    // Wrap around 방지 (실제로는 GC가 필요하지만 단순화)
    if (kvNextLogicalSliceAddr >= SLICES_PER_SSD) {
        kvNextLogicalSliceAddr = 0;
    }
    return addr;
}

// KV Put 핸들러
int handle_nvme_io_kv_put(unsigned int key, unsigned int cmdSlotTag, unsigned int nlb)
{
    int existingIdx = KvLookup(key);
    int targetIdx;
    unsigned int logicalSliceAddr;
    
    if (existingIdx >= 0) {
        // 기존 키 업데이트
        targetIdx = existingIdx;
        logicalSliceAddr = kvMapPtr->kvEntry[targetIdx].logicalSliceAddr;
        
        // 기존 슬라이스 무효화
        if (logicalSliceAddr != KV_ENTRY_NONE) {
            InvalidateOldVsa(logicalSliceAddr);
        }
        // 새 슬라이스 할당
        logicalSliceAddr = AllocateLogicalSlice();
    } else {
        // 새 키 삽입 - 해시 위치 찾기
        targetIdx = FindSlotForInsert(key);
        if (targetIdx < 0) {
            xil_printf("[KV] Error: Hash table full\r\n");
            return -1;
        }
        logicalSliceAddr = AllocateLogicalSlice();
    }
    
    // 엔트리 업데이트
    kvMapPtr->kvEntry[targetIdx].key = key;
    kvMapPtr->kvEntry[targetIdx].logicalSliceAddr = logicalSliceAddr;
    kvMapPtr->kvEntry[targetIdx].valid = KV_ENTRY_VALID;
    
    // 호스트에서 데이터 수신 (RxDMA) - 기존 Write 로직 활용
    ReqTransNvmeToSlice(cmdSlotTag, logicalSliceAddr, nlb, IO_NVM_WRITE);
    
    return 0;
}

// KV Get 핸들러
int handle_nvme_io_kv_get(unsigned int key, unsigned int cmdSlotTag, unsigned int *valueLen)
{
    int idx = KvLookup(key);
    
    if (idx < 0) {
        // 키 없음
        *valueLen = 0;
        return ENOSUCHKEY;
    }
    
    unsigned int logicalSliceAddr = kvMapPtr->kvEntry[idx].logicalSliceAddr;
    
    // 값 길이 설정 (4KB 고정)
    *valueLen = KV_VALUE_SIZE;
    
    // 호스트로 데이터 전송 (TxDMA) - 기존 Read 로직 활용
    ReqTransNvmeToSlice(cmdSlotTag, logicalSliceAddr, 0, IO_NVM_READ);
    
    return 0;
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
	//IO_READ_COMMAND_DW13 writeInfo13;
	//IO_READ_COMMAND_DW15 writeInfo15;
	unsigned int startLba[2];
	unsigned int nlb;
	unsigned int nsid = nvmeIOCmd->NSID;

	writeInfo12.dword = nvmeIOCmd->dword[12];
	//writeInfo13.dword = nvmeIOCmd->dword[13];
	//writeInfo15.dword = nvmeIOCmd->dword[15];

	//if(writeInfo12.FUA == 1)
	//	xil_printf("write FUA\r\n");

	startLba[0] = nvmeIOCmd->dword[10];
	startLba[1] = nvmeIOCmd->dword[11];
	nlb = writeInfo12.NLB;

	ASSERT(startLba[0] < storageCapacity_L / USER_CHANNELS && (startLba[1] < STORAGE_CAPACITY_H || startLba[1] == 0));
	//ASSERT(nlb < MAX_NUM_OF_NLB);
	ASSERT((nvmeIOCmd->PRP1[0] & 0xF) == 0 && (nvmeIOCmd->PRP2[0] & 0xF) == 0);
	ASSERT(nvmeIOCmd->PRP1[1] < 0x10000 && nvmeIOCmd->PRP2[1] < 0x10000);

	ReqTransNvmeToSlice(cmdSlotTag, startLba[0] + (storageCapacity_L / USER_CHANNELS) * (nsid - 1), nlb, IO_NVM_WRITE);
}

/* === Block-Level FTL (ESS4116) === */
void handle_nvme_io_mapping_info()
{
	unsigned int validLogicalCnt = 0;
	unsigned int validVirtualCnt = 0;

	for (unsigned int i = 0; i < SLICES_PER_SSD; i++) {
		if (logicalSliceMapPtr->logicalSlice[i].virtualSliceAddr != VSA_NONE)
			validLogicalCnt++;

		if (virtualSliceMapPtr->virtualSlice[i].logicalSliceAddr != LSA_NONE)
			validVirtualCnt++;
	}

	unsigned int mappedBlockCnt = 0;
	unsigned int totalLogicalBlocks = SLICES_PER_SSD / SLICES_PER_BLOCK;
	static unsigned char seenBlock[USER_BLOCKS_PER_SSD] = {0};
	memset(seenBlock, 0, sizeof(seenBlock));

	for (unsigned int i = 0; i < SLICES_PER_SSD; i++) {
		unsigned int vsa = logicalSliceMapPtr->logicalSlice[i].virtualSliceAddr;
		if (vsa != VSA_NONE) {
			unsigned int die = Vsa2VdieTranslation(vsa);
			unsigned int blk = Vsa2VblockTranslation(vsa);
			unsigned int globalBlkIdx = die * USER_BLOCKS_PER_DIE + blk;

			if (!seenBlock[globalBlkIdx]) {
				seenBlock[globalBlkIdx] = 1;
				mappedBlockCnt++;
			}
		}
	}

	unsigned int freeBlkCnt = 0;
	unsigned int invalidSum = 0;
	unsigned int blockCnt = 0;

	for (unsigned int d = 0; d < USER_DIES; d++) {
		freeBlkCnt += virtualDieMapPtr->die[d].freeBlockCnt;
		for (unsigned int b = 0; b < USER_BLOCKS_PER_DIE; b++) {
			invalidSum += virtualBlockMapPtr->block[d][b].invalidSliceCnt;
			blockCnt++;
		}
	}

	xil_printf("-----------------------------------------------------\r\n");
	xil_printf("[FTL] Mapping Table Summary\r\n");
	xil_printf("-----------------------------------------------------\r\n");
	xil_printf(" Valid logicalSliceMap entries : %u / %u\r\n", validLogicalCnt, SLICES_PER_SSD);
	xil_printf(" Valid virtualSliceMap entries : %u / %u\r\n", validVirtualCnt, SLICES_PER_SSD);
	xil_printf(" Unique mapped blocks          : %u / %u\r\n", mappedBlockCnt, totalLogicalBlocks);
	xil_printf("-----------------------------------------------------\r\n");
	xil_printf("[FTL] Space Utilization\r\n");
	xil_printf("-----------------------------------------------------\r\n");
	xil_printf(" Free blocks remaining         : %u\r\n", freeBlkCnt);
	xil_printf("-----------------------------------------------------\r\n");
}
/* ================================= */

void handle_nvme_io_cmd(NVME_COMMAND *nvmeCmd)
{
	NVME_IO_COMMAND *nvmeIOCmd;
	NVME_COMPLETION nvmeCPL;
	unsigned int opc;
	nvmeIOCmd = (NVME_IO_COMMAND*)nvmeCmd->cmdDword;
	/*		xil_printf("OPC = 0x%X\r\n", nvmeIOCmd->OPC);
			xil_printf("PRP1[63:32] = 0x%X, PRP1[31:0] = 0x%X\r\n", nvmeIOCmd->PRP1[1], nvmeIOCmd->PRP1[0]);
			xil_printf("PRP2[63:32] = 0x%X, PRP2[31:0] = 0x%X\r\n", nvmeIOCmd->PRP2[1], nvmeIOCmd->PRP2[0]);
			xil_printf("dword10 = 0x%X\r\n", nvmeIOCmd->dword10);
			xil_printf("dword11 = 0x%X\r\n", nvmeIOCmd->dword11);
			xil_printf("dword12 = 0x%X\r\n", nvmeIOCmd->dword12);*/


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
//			xil_printf("IO Write Command\r\n");
			handle_nvme_io_write(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		case IO_NVM_READ:
		{
//			xil_printf("IO Read Command\r\n");
			handle_nvme_io_read(nvmeCmd->cmdSlotTag, nvmeIOCmd);
			break;
		}
		/* === Block-Level FTL (ESS4116) === */
		case IO_NVM_FTL_MAP:
		{
			handle_nvme_io_mapping_info();
			nvmeCPL.dword[0] = 0;
			nvmeCPL.specific = 0x0;
			set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
			break;
		}
		/* ================================= */
		/* ============ Key-Value =========== */
        case IO_KV_PUT:
        {
            // CDW10 = key, CDW12 = nlb
            unsigned int key = nvmeIOCmd->dword10;
            unsigned int nlb = nvmeIOCmd->dword12 & 0xFFFF;
            
            int ret = handle_nvme_io_kv_put(key, nvmeCmd->cmdSlotTag, nlb);
            
            // completion은 DMA 완료 후 자동 처리됨 (ReqTransNvmeToSlice에서)
            // 에러 시에만 즉시 completion
            if (ret < 0) {
                nvmeCPL.dword[0] = 0;
                nvmeCPL.specific = 0x1;  // 에러
                set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
            }
            break;
        }
        case IO_KV_GET:
        {
            // CDW10 = key
            unsigned int key = nvmeIOCmd->dword10;
            unsigned int valueLen = 0;
            
            int ret = handle_nvme_io_kv_get(key, nvmeCmd->cmdSlotTag, &valueLen);
            
            if (ret == ENOSUCHKEY) {
                // 키 없음 - ENOSUCHKEY 반환
                nvmeCPL.dword[0] = 0;
                nvmeCPL.specific = ENOSUCHKEY;
                set_nvme_cpl(nvmeCmd->qID, nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
                break;
            }
            
            // 성공 시 completion은 DMA 완료 후 처리
            // result에 valueLen 설정 필요
            if (ret < 0) {
                nvmeCPL.dword[0] = 0;
                nvmeCPL.specific = 0x1;
                set_auto_nvme_cpl(nvmeCmd->cmdSlotTag, nvmeCPL.specific, nvmeCPL.statusFieldWord);
            }
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

