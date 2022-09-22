#ifndef __RAID_H__
#define __DAID_H__

#define PARITY_SIZE 4
#define CHANNEL_NUM 2
#define MAX_EC_MODLE 4
#define BAND_WITDH 16
#define SUPERBLOCK_ALLOCTION 2


void pre_process_test(struct ssd_info *ssd,int type);

struct sub_request *creat_parity_sub_request(struct ssd_info *ssd,unsigned int start_lpn ,unsigned int lpn,unsigned int parity_size,unsigned int parity_state,struct request*req,unsigned int operation);
void pre_write_parity_page(struct ssd_info *ssd,unsigned int lsn);
void delete_r_sub_from_channel(struct ssd_info*ssd,unsigned int channel,struct sub_request *sub_ch);

//void generate_parity(struct ssd_info *ssd,struct sub_request *sub);

unsigned int  spread_lsn(struct ssd_info * ssd,unsigned int lsn);
unsigned int  spread_lpn(struct ssd_info * ssd,unsigned int lpn);
unsigned int shrink_lsn(struct ssd_info * ssd,unsigned int lsn);
unsigned int shrink_lpn(struct ssd_info * ssd,unsigned int lpn);


/*
Used for black RAID 
*/

void pre_write_white_parity(struct ssd_info *ssd);
int insert_sub_to_queue_head(struct ssd_info *ssd,struct sub_request *parity_sub,unsigned int channel);
int white_parity_gc(struct ssd_info *ssd);
int swap_sub_request( struct sub_request*sub1,struct sub_request *sub2);
int copy_sub_request( struct sub_request*sub1,struct sub_request *tmp);







#endif 