#ifndef __RECOVER_H__
#define __RECOVER_H__

#include "states.h"


#define PAGE_FALUT 0
#define BLOCK_FAULT 1
#define DIE_FAULT 2
#define CHIP_FAULT 4
#define CHANNEL_FAULT 8

unsigned int get_recovery_node_num(struct ssd_info *ssd);
unsigned int get_pos_in_band(struct ssd_info *ssd, unsigned int ppn);
int is_same_superpage(struct sub_request *sub0, struct sub_request *sub1);
int get_channel_from_ppn(struct ssd_info *ssd, unsigned int ppn);
int get_num_subs_r_from_channel(struct ssd_info *ssd, unsigned channel, int op);
struct ssd_info *creat_read_sub_for_recover_page(struct ssd_info *ssd, struct sub_request *sub, struct request *req);
//struct ssd_info *write_recovery_page(struct ssd_info *ssd, unsigned int lpn, int state, long long recovery_time);
int delete_recovery_node(struct ssd_info *ssd, struct recovery_operation *rec);
struct ssd_info *write_recovery_page(struct ssd_info *ssd, struct recovery_operation *rec, long long recovery_time);

#ifdef ACTIVE_RECOVERY
struct ssd_info *active_recovery(struct ssd_info *ssd, unsigned int broken_type, unsigned int broken_flag, struct request *req);
struct ssd_info *active_recovery_page(struct ssd_info *ssd, unsigned int broken_flag, int *recovery_need_pos, unsigned int *broken_lpn, int *broken_state, int recovery_state, unsigned int block, unsigned int page, struct request *req);
struct ssd_info *active_write_recovery_page(struct ssd_info *ssd, struct recovery_operation *rec, long long recovery_time);
#endif


#endif