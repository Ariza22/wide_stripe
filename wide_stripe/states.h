#ifndef __STATUS_H__
#define __STATUS_H__

#define USE_EC "EC_P" //
#define USE_SUPERBLOCK "SUPERBLOCK" //采用超级块条带组织
#define CALCULATION "CALCULATION" //EC的编解码计算时间

//#define BROKEN "BROKEN"
//#define BROKEN_BLOCK "BROKEN_BLOCK"
#define BROKEN_PAGE "BROKEN_PAGE"
/*默认为被动恢复*/
#define  RECOVERY "RECOVERY"
/*主动恢复*/
//#define ACTIVE_RECOVERY "ACTIVE_RECOVERY"


//白盒测试（物理条带组织）
#define GC "_gc"//降低处理gc请求的优先级。
#define USE_PARITY "P "
#define USE_WHITE_PARITY "WHITE_P"      //以物理页为条带组织RAID，使用全动态分配方式保证一个物理页条带逐步写下去

#define ASCII "ts-lvm0" //Exchange、src2-lvm0、ts-lvm0、rsrch-lvm0



#define BUF " " //consider the delay of ECC 
#define PRE_FUNCTION_TEST 2
#define DEAL_FUNCTION_TEST 1
#define REPEAT_TIME 1

#define GC_SUB 2
#define FAULT_SUB 3
#define RECOVER_SUB 4
//#define PARITY_IN_REQ 1
//#define PARITY_BEFORE_SUB 2

#define HALF_VALID_RATE 0.1
#define HOT_RATE 0.8
#define PARITY_RATE 0.9

#define FACTORY_BAD_BLOCK 0

#define DATA 1
#define PARITY 2 
#define BAD_BLOCK 1
#define BAD_PAGE 1
#define COLD 0
#define HOT 1


#define PAGE_DATA 8 //ssd->parameter->subpage_page*2
#define SECTOR_DATA 2
#endif