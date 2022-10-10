#include <stdlib.h>
#include <stdio.h>
#include "ssd.h"
#include "initialize.h"
#include "pagemap.h"
#include "flash.h"
#include "avlTree.h"
#include "states.h"
#include <math.h>
#ifdef RECOVERY
#include "recover.h"

unsigned int get_recovery_node_num(struct ssd_info *ssd)
{
	struct recovery_operation *rec = NULL;
	unsigned int num = 0;
	rec = ssd->recovery_head;
	while (rec != NULL)
	{
		num++;
		rec = rec->next_node;
	}
	return num;
}

unsigned int get_pos_in_band(struct ssd_info *ssd, unsigned int ppn)
{
	unsigned int page_plane;
	page_plane = ssd->parameter->page_block * ssd->parameter->block_plane;
	return (ppn / page_plane);
}

Status is_same_superpage(struct sub_request *sub0, struct sub_request *sub1)
{
	if(sub0 == NULL || sub1 == NULL)
		return FALSE;
	if(sub0->location->block == sub1->location->block && sub0->location->page == sub1->location->page)
		return TRUE;
	return FALSE;
}

struct ssd_info *creat_read_sub_for_recover_page(struct ssd_info *ssd, struct sub_request *sub, struct request *req)
{
	unsigned int fault_ppn, fault_channel, fault_superblock, fault_pos, fault_size; //fault_*������ҳ����������Ϣ
	int ec_mode;
	struct recovery_operation *rec_node = NULL;
	unsigned int i, pos, flag;
	struct sub_request *sub_r = NULL, *sub_r_head = NULL;
	unsigned int plane_die, plane_chip, plane_channel;
	struct channel_info *channel = NULL;
	unsigned int first_parity;
	unsigned long long mask = 0;
	unsigned int channel_id, chip_id, die_id, plane_id, block_id, page_id;
	fault_ppn = sub->ppn;
	fault_channel = sub->location->channel;
	fault_superblock = get_band_id_from_ppn(ssd, fault_ppn);
	ec_mode = ssd->band_head[fault_superblock].ec_modle;
	fault_pos = fault_ppn / (ssd->parameter->page_block * ssd->parameter->block_plane);
#ifdef RECOVERY
	ssd->broken_page++;
#endif
	if(ec_mode == 0)
	{
		printf("Can't recover the page!\n");
		getchar();
		return NULL;
	}

	rec_node = (struct recovery_operation *)malloc(sizeof(struct recovery_operation));
	alloc_assert(rec_node, "rec_node");
	memset(rec_node, 0, sizeof(struct recovery_operation));
	if(rec_node == NULL)
	{
		return NULL;
	}
	rec_node->sub = sub;
	rec_node->req = req;
	rec_node->ec_mode = ec_mode;
	rec_node->next_node = NULL;

	plane_die = ssd->parameter->plane_die;
	plane_chip = plane_die * ssd->parameter->die_chip;
	plane_channel = plane_chip * ssd->parameter->chip_channel[0];

	// ��ȡĥ����
	int wear_num = 0, high_wear_num = 0, idle_num = 0, healthy_num = 0;
	unsigned long long wear_flag = 0, high_wear_flag = 0, idle_flag = 0, healthy_flag = 0;

	for (int i = 0; i < BAND_WITDH; i++)
	{
		switch (ssd->dram->wear_map->wear_map_entry[i * ssd->parameter->block_plane + fault_superblock].wear_state) {
		case 1:
		{
			wear_num++;
			wear_flag |= 1ll << i;
		}
		case 2:
		{
			high_wear_num++;
			high_wear_flag |= 1ll << i;
		}
		case 3:
		{
			idle_num++;
			idle_flag |= 1ll << i;
		}
		}
	}

	healthy_num = BAND_WITDH - wear_num - high_wear_num - idle_num;
	healthy_flag = ~(idle_flag | wear_flag | high_wear_flag);

	//û��ĥ���ʱĬ��ѡ�����һ����ΪУ���
	if (wear_num == 0)
	{
		wear_num = 1;
		int index;
		for (index = BAND_WITDH - 1; index >= 0; index--) {
			if ((high_wear_flag | (1ll << index) != high_wear_flag) && (idle_flag | (1ll << index) != idle_flag)) {
				wear_flag |= 1ll << index;
				break;
			}
		}
		healthy_num--;
		healthy_flag &= ~(1ll << index);
	}

	int band_id = -1;
	int error_page_num = 1;
	unsigned long long band_flag = healthy_flag | wear_flag;
	int band_width = 0; //�𻵿�������������
	int now_length = healthy_num + wear_num; //��ĥ��鲻����������֯
	for (int i = 0; i < wear_num; i++) {
		int parity_state = 0;
		band_width = (now_length + wear_num - i - 1) / (wear_num - i); // ����ȡ��
		now_length -= band_width;
		for (int j = 0; j < band_width; j++) {
			//Ѱ�Ҹ��𻵿�λ���ĸ�����
			int pos = find_first_one(ssd, band_flag);
			band_flag &= ~(1ll << pos);
			if (fault_pos == pos) {
				band_id = i;
				continue;
			}
			channel_id = pos / plane_channel;
			chip_id = (pos % plane_channel) / plane_chip;
			die_id = (pos % plane_chip) / plane_die;
			plane_id = pos % plane_die;
			block_id = rec_node->sub->location->block;
			page_id = rec_node->sub->location->page;
			if (ssd->channel_head[channel_id].chip_head[chip_id].die_head[die_id].plane_head[plane_id].blk_head[block_id].page_head[page_id].bad_page_flag == TRUE) {
				error_page_num++;//ͳ�������г����ܸ���
			}
			//mask�����Ҫ��ȡ���Ҵ����
			rec_node->block_for_recovery |= 1ll << pos;
		}
		if (band_id != -1) {
			break;
		}else {
			error_page_num = 1;
			rec_node->block_for_recovery = 0;
		}
	}

	if (band_id == -1) {
		printf("error, cannot find the broken page.");
		return ssd;
	}

	if (error_page_num > 1) {
		printf("error, cannot recover the broken page.");
		return ssd;
	}

	/*for(i = 0; i < BAND_WITDH - ec_mode; i++)
	{
	rec_node->block_for_recovery  += 1 << ((fault_pos + 1 + i) % BAND_WITDH);
	}*/

	mask = rec_node->block_for_recovery;

	//���ָ����������ҵ���Ӧ��ͨ����
	for(i = 0; i < band_width - error_page_num; i++)
	{
		pos = find_first_one(ssd, mask);
		mask &= ~(1ll << pos);
		//pos = (fault_pos + 1 + i) % BAND_WITDH;
		sub_r = (struct sub_request *)malloc(sizeof(struct sub_request));
		alloc_assert(sub_r,"sub_r");
		memset(sub_r,0,sizeof(struct sub_request));
		if(sub_r == NULL)
		{
			return NULL;
		}

		sub_r->next_node = NULL;
		sub_r->next_subs = NULL;
		sub_r->update = NULL;
		sub_r->location = NULL;
		//sub��location����
		sub_r->location = (struct local *)malloc(sizeof(struct local));
		alloc_assert(sub_r->location, "sub_r->location");
		memset(sub_r->location, 0, sizeof(struct local));
		if(sub_r->location == NULL)
		{
			return NULL;
		}
		sub_r->location->channel = pos / plane_channel;
		sub_r->location->chip = (pos % plane_channel) / plane_chip;
		sub_r->location->die = (pos % plane_chip) / plane_die;
		sub_r->location->plane = pos % plane_die;
		sub_r->location->block = rec_node->sub->location->block;
		sub_r->location->page = rec_node->sub->location->page;
		//sub����������
		sub_r->begin_time = ssd->current_time;
		sub_r->current_state = SR_WAIT;
		sub_r->current_time = 0x7fffffffffffffff;
		sub_r->next_state = SR_R_C_A_TRANSFER;
		sub_r->next_state_predict_time = 0x7fffffffffffffff;
		sub_r->operation = READ;
		sub_r->size = sub->size;                                                               
		sub_r->state = sub->state;
		sub_r->type = RECOVER_SUB;
		sub_r->lpn = ssd->channel_head[sub_r->location->channel].chip_head[sub_r->location->chip].die_head[sub_r->location->die].plane_head[sub_r->location->plane].blk_head[sub_r->location->block].page_head[sub_r->location->page].lpn; //��Ҫ��ȡ��ҳ��lpn
		sub_r->ppn = find_ppn(ssd, sub_r->location->channel, sub_r->location->chip, sub_r->location->die, sub_r->location->plane, sub_r->location->block, sub_r->location->page);
		//if(sub_r->lpn == -2)
		//	sub_r->ppn = find_ppn(ssd, sub_r->location->channel, sub_r->location->chip, sub_r->location->die, sub_r->location->plane, sub_r->location->block, sub_r->location->page);
		//else
		//	sub_r->ppn = ssd->dram->map->map_entry[sub_r->lpn].pn;   //��Ҫ��ȡ��ppn
		//����req��ȡpageʱ�������������򽫲����Ľ�������������ӵ�req�������������
		if(req != NULL)
		{
			sub_r->next_subs = req->subs;
			req->subs = sub_r;
		}

		//��ͨ����������Ӹ��������ǰ�����ж϶�������������Ƿ����������������ֱ�ӽ��µ���������Ϊ��ɣ�������ӵ��������������
		channel = &ssd->channel_head[sub_r->location->channel];
		sub_r_head = channel->subs_r_head;
		flag = 0;
		while(sub_r_head != NULL)
		{
			if(sub_r_head->ppn == sub_r->ppn)
			{
				flag = 1;
				break;
			}
			sub_r_head = sub_r_head->next_node;
		}
		if(flag == 0) //�ڸ�ͨ���Ķ������������δ�ҵ���ͬ������
		{
			if(channel->subs_r_head != NULL){
				channel->subs_r_tail->next_node = sub_r;
				channel->subs_r_tail = sub_r;
			}
			else
			{
				channel->subs_r_head = sub_r;
				channel->subs_r_tail = sub_r;
			}
			ssd->read_sub_request++;
		}
		else
		{
			sub_r->current_state = SR_R_DATA_TRANSFER;
			sub_r->current_time = ssd->current_time;
			sub_r->next_state = SR_COMPLETE;
			sub->next_state_predict_time = ssd->current_time + 1000;
			sub_r->complete_time = ssd->current_time + 1000;

			rec_node->sub_r_complete_flag |= 1ll << pos;
			//printf("broken_lpn = %d\trecovery_lpn = %d\tblock_for_recovery = %d\tcomplete_flag = %d\n", rec_node->sub->lpn, sub_r->lpn, rec_node->block_for_recovery, rec_node->sub_r_complete_flag);
			//printf("sub_r_complete = %d\tchannel_for_recovery = %d\n", recovery_node->sub_r_complete_flag, recovery_node->channel_for_recovery);
			if(rec_node->sub_r_complete_flag == rec_node->block_for_recovery) //�����ǰ�ָ���������Ķ���������ˣ�����н���ָ��������ָ�������ҳд�뻺��
			{
				printf("2..................................................................Write recovered data to the cache!\n\n");
				//���ָ��ڵ����ָ�����β��
				if (ssd->recovery_head == NULL)
				{
					ssd->recovery_head = rec_node;
					ssd->recovery_tail = rec_node;
				}
				else
				{
					ssd->recovery_tail->next_node = rec_node;
					ssd->recovery_tail = rec_node;
				}
				write_recovery_page(ssd, rec_node, sub_r->complete_time + 20000);
				return ssd;
				//write_recovery_page(ssd, rec_node->sub->lpn, rec_node->sub->state, sub_r->complete_time + 20000);
				//ssd->recovery_page_num++;
			}
		}
	}
	//���ָ��ڵ����ָ�����β��
	if (ssd->recovery_head == NULL)
	{
		ssd->recovery_head = rec_node;
		ssd->recovery_tail = rec_node;
	}
	else
	{
		ssd->recovery_tail->next_node = rec_node;
		ssd->recovery_tail = rec_node;
	}
	return ssd;
}

//struct ssd_info *write_recovery_page(struct ssd_info *ssd, unsigned int lpn, int state, long long recovery_time)
//{
//	struct request *req;
//	struct sub_request *sub;
//	struct buffer_group *buffer_node = NULL, k;
//	unsigned int sectpr_count;
//	struct recovery_operation *p, *q;
//	//���lpn2ppn�������Ͽ�ppn2lpn����Ϊ�ָ����������ݿ��ܻ����ڻָ������е��������ݣ������Ҫ����ppn2lpn
//	ssd->dram->map->map_entry[lpn].pn = -1;
//	ssd->dram->map->map_entry[lpn].state = 0;
//
//	ssd->recovery_page_num++;
//	//Ѱ�Ҷ����������ڵ�����Ȼ����Ϊ���
//	//������ָ�������ɾ��
//	q = ssd->recovery_head;
//	if(q->sub->lpn == lpn){
//		ssd->recovery_head = q->next_node;
//		p = q;
//	}
//	else
//	{
//		p = q->next_node;
//		while(p != NULL)
//		{
//			if(p->sub->lpn == lpn)
//			{
//				q->next_node = p->next_node;
//				break;
//			}
//			q = p;
//			p = p->next_node;
//		}
//	}
//	// pΪ�ҵ���recovery_node
//	if (p != NULL)
//	{
//		sub = p->req->subs;
//		while (sub != NULL)
//		{
//			if (sub->lpn == lpn)
//			{
//				sub->complete_time = recovery_time;
//				sub->current_state = SR_COMPLETE;
//				printf("req->time = %16I64u\tsub_complete = %16I64u\tend - start = %16I64u\n", p->req->time, sub->complete_time, sub->complete_time - sub->begin_time);
//				//getchar();
//				break;
//			}
//			sub = sub->next_subs;
//		}
//		if (sub == NULL)
//		{
//			printf("Don't find the subrequest for recovery node\n");
//			getchar();
//		}
//	}
//	else
//	{
//		printf("Don't find the recovery node\n");
//		return NULL;
//	}
// 
//	printf("the recovery node number is %d\n", get_recovery_node_num(ssd));
//	insert2buffer(ssd, lpn, state, NULL, NULL);
//	p->sub = NULL;
//	p->req = NULL;
//	free(p);
//	p = NULL;
//	return ssd;
//}
struct ssd_info *write_recovery_page(struct ssd_info *ssd, struct recovery_operation *rec, long long recovery_time)
{
	struct sub_request *sub = NULL;
	if (rec->sub->lpn == 272565)
		printf("yes");
	ssd->dram->map->map_entry[rec->sub->lpn].pn = -1;
	ssd->dram->map->map_entry[rec->sub->lpn].state = 0;
	ssd->recovery_page_num++;
	if(delete_recovery_node(ssd, rec) == FAILURE)
	{ 
		printf("Error, delete recovery node\n");
		getchar();
		return NULL;

	}
	
	sub = rec->req->subs;
	while (sub != NULL)
	{
		if (sub->lpn == rec->sub->lpn)
		{
			sub->complete_time = recovery_time;
			sub->current_state = SR_COMPLETE;
			//printf("req->time = %16I64u\tsub_complete = %16I64u\tend - start = %16I64u\n", rec->req->time, sub->complete_time, sub->complete_time - sub->begin_time);
			//getchar();
			break;
		}
		sub = sub->next_subs;
	}
	if (sub == NULL)
	{
		printf("Don't find the subrequest for recovery node\n");
		getchar();
	}

	//printf("the recovery node number is %d\n", get_recovery_node_num(ssd));
	insert2buffer(ssd, rec->sub->lpn, rec->sub->state, NULL, NULL);
	rec->sub = NULL;
	rec->req = NULL;
	free(rec);
	rec = NULL;

	return ssd;
}

int delete_recovery_node(struct ssd_info *ssd, struct recovery_operation *rec)
{
	struct recovery_operation *p = NULL;
	p = ssd->recovery_head;
	if (p == NULL)
	{
		printf("there is no recovery node!\n");
		getchar();
		return FAILURE;
	}
	if (rec == ssd->recovery_head)
	{
		if (ssd->recovery_head != ssd->recovery_tail)
		{
			ssd->recovery_head = rec->next_node;
		}
		else
		{
			ssd->recovery_head = NULL;
			ssd->recovery_tail = NULL;
		}
	}
	else
	{
		while (p->next_node != NULL)
		{
			if (p->next_node == rec)
			{
				break;
			}
			p = p->next_node;
		}
		if (p->next_node != NULL)
		{
			if (p->next_node == ssd->recovery_tail)
			{
				p->next_node = NULL;
				ssd->recovery_tail = p;
			}
			else
			{
				p->next_node = rec->next_node;
			}
		}
		else
		{
			printf("Error, Don't find the recovery node!\n");
			getchar();
			return FAILURE;
		}
	}
	return SUCCESS;
}

#ifdef ACTIVE_RECOVERY
struct ssd_info *active_recovery(struct ssd_info *ssd, unsigned int broken_type, unsigned int broken_flag, struct request *req)
{
	unsigned int channel, chip, die, plane, block, page, lpn;
	unsigned int plane_channel, plane_chip, plane_die;
	unsigned int i, j, pos, broken_num, survival_num, survival_flag, valid_page_num;
	unsigned int *broken_lpn = NULL, *survival_pos = NULL, *broken_pos = NULL, *recovery_need_pos = NULL;
	unsigned int survive = 0, broken = 0;
	int ec_mode, recovery_state, *broken_state = NULL; 
	unsigned int start;

	if (broken_type == CHANNEL_FAULT)
	{
		printf("Error! This recovery mechanism does not resolve channel failures!\n");
		getchar();
		return NULL;
	}
	survival_flag = broken_flag ^ 0x0ffff;
	broken_num = size(broken_flag);
	survival_num = BAND_WITDH - broken_num;
	
	//��¼��ʧ��λ��
	broken_pos = (int *)malloc(broken_num * sizeof(int));
	alloc_assert(broken_pos, "broken_ppn");
	if (broken_pos == NULL)
	{
		return NULL;
	}
	memset(broken_pos, -1, broken_num * sizeof(int));
	//��¼�Ҵ��λ��
	survival_pos = (int *)malloc(survival_num * sizeof(int));
	alloc_assert(survival_pos, "survival_pos");
	if (survival_pos == NULL)
	{
		return NULL;
	}
	memset(survival_pos, -1, survival_num * sizeof(int));
	//��¼�Ҵ��Ͳ���������ڵ�superpage��λ��
	for (i = 0; i < BAND_WITDH; i++)
	{
		if ((broken_flag & (1 << i)) != 0)
		{
			broken_pos[broken++] = i;
		}
		else
		{
			survival_pos[survive++] = i;
		}
	}
 
	// 
	plane_die = ssd->parameter->plane_die;
	plane_chip = plane_die * ssd->parameter->die_chip;
	plane_channel = plane_die * ssd->parameter->chip_channel[0];
	for (block = 0; block < ssd->parameter->block_plane; block++)
	{
		ec_mode = ssd->band_head[block].ec_modle;
		recovery_need_pos = (int *)malloc((BAND_WITDH - ec_mode) * sizeof(int));
		alloc_assert(recovery_need_pos, "recovery_need_pos");
		if (recovery_need_pos == NULL)
		{
			return NULL;
		}
		memset(recovery_need_pos, -1, (BAND_WITDH - ec_mode) * sizeof(int));
		printf("recover superblock %d\n", block);
		start = 0;
		for (page = 0; page < ssd->parameter->page_block; page++)
		{	
			//��¼��ʧ��lpn,�Ա�ָ�֮�����д����
			broken_lpn = (int *)malloc(broken_num * sizeof(unsigned int));
			alloc_assert(broken_lpn, "broken_ppn");
			if (broken_lpn == NULL)
			{
				return NULL;
			}
			memset(broken_lpn, -1, broken_num * sizeof(unsigned int));
			//��¼��ʧ��state
			broken_state = (int *)malloc(broken_num * sizeof(int));
			alloc_assert(broken_state, "broken_ppn");
			if (broken_state == NULL)
			{
				return NULL;
			}
			memset(broken_state, 0, broken_num * sizeof(int));

			valid_page_num = 0;
			//���ȵĽ��ָ�������䵽��Ӧ��ͨ����
			for (i = 0; i < (BAND_WITDH - ec_mode); i++)
			{
				recovery_need_pos[i] = survival_pos[start % (BAND_WITDH - broken_num)];
				start++;
			}
			//�ж�broken_page�Ƿ���Ч����־
			recovery_state = 0;
			for (j = 0; j < broken_num; j++)
			{
				pos = broken_pos[j];	 //��ȡ���������λ��
				channel = pos / plane_channel;
				chip = (pos % plane_channel) / plane_chip;
				die = (pos % plane_chip) / plane_die;
				plane = pos % plane_die;
				//����ҳ��Ч
				if ((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn != -1) &&
					(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn != -2) &&
					ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn <= (ssd->band_num * BAND_WITDH * ssd->parameter->page_block))
				{
					broken_lpn[j] = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn;
					broken_state[j] = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].valid_state;
					recovery_state |= broken_state[j];
					valid_page_num++;
				}
			}
			//�����ָ�������Ҫ�Ķ�������
			if (valid_page_num > 0)
			{
				active_recovery_page(ssd, broken_flag, recovery_need_pos, broken_lpn, broken_state, recovery_state, block, page, req);
			}			
		}
		if (recovery_need_pos != NULL)
		{
			free(recovery_need_pos);
			recovery_need_pos = NULL;
		}
	}	 
	if (broken_pos != NULL)
	{
		free(broken_pos);
		broken_pos = NULL;
	}
	if (survival_pos != NULL)
	{
		free(survival_pos);
		survival_pos = NULL;
	}
	return ssd;
}

struct ssd_info *active_recovery_page(struct ssd_info *ssd, unsigned int broken_flag, int *recovery_need_pos, unsigned int *broken_lpn, int *broken_state, int recovery_state, unsigned int block, unsigned int page, struct request *req)
{
	unsigned int survival_flag = 0;
	unsigned int i, j, pos, broken_num, survival_num;
	struct sub_request *sub_r = NULL, *sub = NULL;
	unsigned int plane_channel, plane_chip, plane_die;
	struct recovery_operation *rec_node = NULL;
	struct channel_info *channel = NULL;
	struct sub_request *sub_r_head = NULL;
	int flag;
	unsigned int chan, chip, die, plane;
	struct local *loc = NULL;

	//��ʧ�Ŀ�ĸ���
	broken_num = size(broken_flag);
	//����һ���ָ������ڵ�
	rec_node = (struct recovery_operation *)malloc(sizeof(struct recovery_operation));
	alloc_assert(rec_node, "rec_node");
	memset(rec_node, 0, sizeof(struct recovery_operation));
	if (rec_node == NULL)
	{
		return NULL;
	}
	rec_node->broken_flag = broken_flag;
	rec_node->broken_state = broken_state;
	rec_node->broken_lpn = broken_lpn;
	rec_node->ec_mode = ssd->band_head[block].ec_modle;
	rec_node->sub = NULL;
	rec_node->req = req;
	rec_node->next_node = NULL;
	//���ûָ��ڵ�����Ķ������־λ
	for (i = 0; i < (BAND_WITDH - rec_node->ec_mode); i++)
	{
		rec_node->block_for_recovery |= 1 << recovery_need_pos[i];
	}

	//���ָ��ڵ����ָ�����β��
	if (ssd->recovery_head == NULL)
	{
		ssd->recovery_head = rec_node;
		ssd->recovery_tail = rec_node;
	}
	else
	{
		ssd->recovery_tail->next_node = rec_node;
		ssd->recovery_tail = rec_node;
	}
	/*********************************/
	//����һ��������ҵ�req�ϣ��������䵽ͨ����ִ�У����ڻָ�����������ɺ󣬱�ǻָ�ʱ��
	sub = (struct sub_request *)malloc(sizeof(struct sub_request));
	alloc_assert(sub, "sub");
	memset(sub, 0, sizeof(struct sub_request));
	if (sub == NULL)
	{
		return NULL;
	}

	sub->next_node = NULL;
	sub->next_subs = NULL;
	sub->update = NULL;
	sub->location = NULL;
	
	sub->location = (struct local *)malloc(sizeof(struct local));
	sub->location->block = block;
	sub->location->page = page;
	sub->begin_time = ssd->current_time;
	sub->current_state = SR_WAIT;
	sub->current_time = 0x7fffffffffffffff;
	sub->next_state = SR_R_C_A_TRANSFER;
	sub->next_state_predict_time = 0x7fffffffffffffff;
	sub->lpn = broken_lpn[0];
	sub->size = 0;                                                               /*��Ҫ�������������������С*/
	//sub->ppn = ssd->dram->map->map_entry[broken_lpn[0]].pn;
	sub->operation = READ;
	sub->state = recovery_state;
	sub->type = FAULT_SUB;
	//�Ѹ�������뵽req��sub����ͷ
	if(req != NULL)
	{
		sub->next_subs = req->subs;
		req->subs = sub;
	}
	rec_node->sub = sub;
	/***************************************************/
	//Ϊ��ǰsuperpage�ָ���������
	plane_die = ssd->parameter->plane_die;
	plane_chip = plane_die * ssd->parameter->die_chip;
	plane_channel = plane_die * ssd->parameter->chip_channel[0];
	
	for (i = 0; i < (BAND_WITDH - rec_node->ec_mode); i++)
	{
		pos = recovery_need_pos[i];	 //��ȡ���������λ��
		//�����������
		sub_r = (struct sub_request *)malloc(sizeof(struct sub_request));
		alloc_assert(sub_r, "sub_r");
		memset(sub_r, 0, sizeof(struct sub_request));
		if (sub_r == NULL)
		{
			return NULL;
		}

		sub_r->next_node = NULL;
		sub_r->next_subs = NULL;
		sub_r->update = NULL;
		sub_r->location = NULL;
		//sub��location����
		sub_r->location = (struct local *)malloc(sizeof(struct local));
		alloc_assert(sub_r->location, "sub_r->location");
		memset(sub_r->location, 0, sizeof(struct local));
		if (sub_r->location == NULL)
		{
			return NULL;
		}
		sub_r->location->channel = pos / plane_channel;
		sub_r->location->chip = (pos % plane_channel) / plane_chip;
		sub_r->location->die = (pos % plane_chip) / plane_die;
		sub_r->location->plane = pos % plane_die;
		sub_r->location->block = block;
		sub_r->location->page = page;
		//sub����������
		sub_r->begin_time = ssd->current_time;
		sub_r->current_state = SR_WAIT;
		sub_r->current_time = 0x7fffffffffffffff;
		sub_r->next_state = SR_R_C_A_TRANSFER;
		sub_r->next_state_predict_time = 0x7fffffffffffffff;
		sub_r->operation = READ;
		sub_r->size = ssd->parameter->subpage_page;
		sub_r->state = recovery_state;
		sub_r->type = RECOVER_SUB;
		sub_r->lpn = ssd->channel_head[sub_r->location->channel].chip_head[sub_r->location->chip].die_head[sub_r->location->die].plane_head[sub_r->location->plane].blk_head[sub_r->location->block].page_head[sub_r->location->page].lpn; //��Ҫ��ȡ��ҳ��lpn
		sub_r->ppn = find_ppn(ssd, sub_r->location->channel, sub_r->location->chip, sub_r->location->die, sub_r->location->plane, sub_r->location->block, sub_r->location->page);
		//�����޸�ʱ������һ��req���ָ������Ķ���������ӵ�req�������������
		if (req != NULL)
		{
			sub_r->next_subs = req->subs;
			req->subs = sub_r;
		}
		channel = &ssd->channel_head[sub_r->location->channel];
		if (channel->subs_r_head != NULL) {
			channel->subs_r_tail->next_node = sub_r;
			channel->subs_r_tail = sub_r;
		}
		else
		{
			channel->subs_r_head = sub_r;
			channel->subs_r_tail = sub_r;
		}
		ssd->read_sub_request++;


		////��ͨ����������Ӹ��������ǰ�����ж϶�������������Ƿ����������������ֱ�ӽ��µ���������Ϊ��ɣ�������ӵ��������������
		//channel = &ssd->channel_head[sub_r->location->channel];
		//sub_r_head = channel->subs_r_head;
		//flag = 0;
		//while (sub_r_head != NULL)
		//{
		//	if (sub_r_head->ppn == sub_r->ppn)
		//	{
		//		flag = 1;
		//		break;
		//	}
		//	sub_r_head = sub_r_head->next_node;
		//}
		////�ڸ�ͨ���Ķ������������δ�ҵ���ͬ��������ӵ��������������
		//if (flag == 0) 
		//{
		//	if (channel->subs_r_head != NULL) {
		//		channel->subs_r_tail->next_node = sub_r;
		//		channel->subs_r_tail = sub_r;
		//	}
		//	else
		//	{
		//		channel->subs_r_head = sub_r;
		//		channel->subs_r_tail = sub_r;
		//	}
		//	ssd->read_sub_request++;
		//}
		//else
		//{
		//	sub_r->current_state = SR_R_DATA_TRANSFER;
		//	sub_r->current_time = ssd->current_time;
		//	sub_r->next_state = SR_COMPLETE;
		//	
		//	sub_r->complete_time = ssd->current_time + 1000;

		//	rec_node->sub_r_complete_flag |= 1 << pos;
		//	//printf("broken_lpn = %d\trecovery_lpn = %d\tblock_for_recovery = %d\tcomplete_flag = %d\n", rec_node->sub->lpn, sub_r->lpn, rec_node->block_for_recovery, rec_node->sub_r_complete_flag);
		//	
		//	if (rec_node->sub_r_complete_flag == rec_node->block_for_recovery) //�����ǰ�ָ���������Ķ���������ˣ�����н���ָ��������ָ�������ҳд�뻺��
		//	{
		//		printf("All read requests required for recovery have been serviced, then the recovered data is written to flash!\n\n");
		//		active_write_recovery_page(ssd, rec_node, sub_r->complete_time + 20000);				
		//		//write_recovery_page(ssd, rec_node->sub->lpn, rec_node->sub->state, sub_r->complete_time + 20000);
		//	}
		//}
	}
	

	return ssd;

}

struct ssd_info *active_write_recovery_page(struct ssd_info *ssd, struct recovery_operation *rec, long long recovery_time)
{
	struct sub_request *sub = NULL;
	unsigned int i, broken_num;
	
	ssd->recovery_page_num++;
	if(delete_recovery_node(ssd, rec) == FAILURE)
	{ 
		printf("Error, delete recovery node\n");
		getchar();
		return NULL;
	}
	broken_num = size(rec->broken_flag);
	for (i = 0; i < broken_num; i++)
	{
		//����Ч��ҳ����д����
		if (rec->broken_lpn[i] != -1)
		{
			ssd->dram->map->map_entry[rec->broken_lpn[i]].pn = -1;
			ssd->dram->map->map_entry[rec->broken_lpn[i]].state = 0;
			rec->sub->complete_time = recovery_time;
			rec->sub->current_state = SR_COMPLETE;
			//printf("req->time = %16I64u\tsub_complete = %16I64u\tend - start = %16I64u\n", rec->req->time, rec->sub->complete_time, rec->sub->complete_time - rec->sub->begin_time);
			// ���ָ�������д������
			insert2buffer(ssd, rec->broken_lpn[i], rec->broken_state[i], NULL, rec->req);
		}
	}
	if(get_recovery_node_num(ssd) %1000 == 0)
		printf("the recovery node number is %d\n", get_recovery_node_num(ssd));
	if (rec != NULL)
	{
		if (rec->broken_lpn != NULL)
		{
			free(rec->broken_lpn);
			rec->broken_lpn = NULL;
		}
		if (rec->broken_state != NULL)
		{
			free(rec->broken_state);
			rec->broken_state = NULL;
		}
		rec->sub = NULL;
		rec->req = NULL;
		rec->next_node = NULL;
	
		free(rec);
		rec = NULL;
	}
	
	return ssd;
}

#endif
#endif


