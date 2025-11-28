//////////////////////////////////////////////////////////////////////////////////
// nvme_io_cmd.h for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Youngjin Jo <yjjo@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
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
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: NVMe IO Command Handler
// File Name: nvme_io_cmd.h
//
// Version: v1.0.0
//
// Description:
//   - declares functions for handling NVMe IO commands
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#ifndef __NVME_IO_CMD_H_
#define __NVME_IO_CMD_H_
#include "../memory_map.h"

void handle_nvme_io_cmd(NVME_COMMAND *nvmeCmd);

// Key-Value 설정 (호스트 코드와 일치)
#define KV_KEY_SIZE             4           // 4 bytes 고정
#define KV_VALUE_SIZE           4096        // 4KB 고정 (1 slice)
#define KV_MAX_ENTRIES          8192        // 최대 키 개수
#define KV_HASH_SIZE  8192

#define KV_ENTRY_NONE           0xFFFFFFFF
#define KV_ENTRY_INVALID        0
#define KV_ENTRY_VALID          1


// Key-Value 매핑 엔트리
typedef struct _KV_ENTRY {
    unsigned int key;                       // 4바이트 키
    unsigned int logicalSliceAddr;          // 값이 저장된 논리 슬라이스 주소
    unsigned int valid;                     // 유효 플래그
} KV_ENTRY, *P_KV_ENTRY;


// Key-Value Hash 테이블
typedef struct _KV_MAP {
    KV_ENTRY kvEntry[KV_MAX_ENTRIES];
    unsigned int nextLogicalSliceAddr;
} KV_MAP, *P_KV_MAP;

#define KV_MAP_ADDR            (RESERVED1_START_ADDR + sizeof(KV_MAP))

// 함수 선언
void InitKvStore();
int KvLookup(unsigned int key);

#endif	//__NVME_IO_CMD_H_
