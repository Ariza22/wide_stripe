#ifndef __STATUS_H__
#define __STATUS_H__

#define USE_EC "EC_P" //
#define USE_SUPERBLOCK "SUPERBLOCK" //���ó�����������֯
#define CALCULATION "CALCULATION" //EC�ı�������ʱ��

//#define BROKEN "BROKEN"
//#define BROKEN_BLOCK "BROKEN_BLOCK"
#define BROKEN_PAGE "BROKEN_PAGE"
/*Ĭ��Ϊ�����ָ�*/
#define  RECOVERY "RECOVERY"
/*�����ָ�*/
//#define ACTIVE_RECOVERY "ACTIVE_RECOVERY"


//�׺в��ԣ�����������֯��
#define GC "_gc"//���ʹ���gc��������ȼ���
#define USE_PARITY "P "
#define USE_WHITE_PARITY "WHITE_P"      //������ҳΪ������֯RAID��ʹ��ȫ��̬���䷽ʽ��֤һ������ҳ������д��ȥ

#define ASCII "ts-lvm0" //Exchange��src2-lvm0��ts-lvm0��rsrch-lvm0



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