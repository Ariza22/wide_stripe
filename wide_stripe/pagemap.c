/*****************************************************************************************************************************
This project was supported by the National Basic Research 973 Program of China under Grant No.2011CB302301
Huazhong University of Science and Technology (HUST)   Wuhan National Laboratory for Optoelectronics

FileName�� pagemap.h
Author: Hu Yang		Version: 2.1	Date:2011/12/02
Description:

History:
<contributor>     <time>        <version>       <desc>                   <e-mail>
Yang Hu	        2009/09/25	      1.0		    Creat SSDsim       yanghu@foxmail.com
				2010/05/01        2.x           Change
Zhiming Zhu     2011/07/01        2.0           Change               812839842@qq.com
Shuangwu Zhang  2011/11/01        2.1           Change               820876427@qq.com
Chao Ren        2011/07/01        2.0           Change               529517386@qq.com
Hao Luo         2011/01/01        2.0           Change               luohao135680@gmail.com
Zhiming Zhu     2012/07/19        2.1.1         Correct erase_planes()   812839842@qq.com
*****************************************************************************************************************************/

#define _CRTDBG_MAP_ALLOC

#include <stdlib.h>
#include <crtdbg.h>

#include "initialize.h"
#include "pagemap.h"
#include "ssd.h"
#include "flash.h"
#include "states.h"
//#include "raid.h"


/************************************************
*����,�����ļ�ʧ��ʱ�������open �ļ��� error��
*************************************************/
void file_assert(int error, char* s)
{
	if (error == 0) return;
	printf("open %s error\n", s);
	getchar();
	exit(-1);
}

/*****************************************************
*����,�������ڴ�ռ�ʧ��ʱ�������malloc ������ error��
******************************************************/
void alloc_assert(void* p, char* s)//����
{
	if (p != NULL) return;
	printf("malloc %s error\n", s);
	getchar();
	exit(-1);
}

/*********************************************************************************
*����
*A��������time_t��device��lsn��size��ope��<0ʱ�������trace error:.....��
*B��������time_t��device��lsn��size��ope��=0ʱ�������probable read a blank line��
**********************************************************************************/
void trace_assert(_int64 time_t, int device, unsigned int lsn, int size, int ope)//����
{
	if (time_t < 0 || device < 0 || lsn < 0 || size < 0 || ope < 0)
	{
		printf("trace error:%I64u %d %d %d %d\n", time_t, device, lsn, size, ope);
		getchar();
		exit(-1);
	}
	if (time_t == 0 && device == 0 && lsn == 0 && size == 0 && ope == 0)
	{
		printf("probable read a blank line\n");
		getchar();
	}
}


/************************************************************************************
*�����Ĺ����Ǹ�������ҳ��ppn���Ҹ�����ҳ���ڵ�channel��chip��die��plane��block��page
*�õ���channel��chip��die��plane��block��page���ڽṹlocation�в���Ϊ����ֵ
*************************************************************************************/
struct local* find_location(struct ssd_info* ssd, unsigned int ppn)
{
	struct local* location = NULL;
	unsigned int i = 0;
	int pn, ppn_value = ppn;
	int page_plane = 0, page_die = 0, page_chip = 0, page_channel = 0;

	pn = ppn;

#ifdef DEBUG
	printf("enter find_location\n");
#endif

	location = (struct local*)malloc(sizeof(struct local));
	alloc_assert(location, "location");
	memset(location, 0, sizeof(struct local));

	page_plane = ssd->parameter->page_block * ssd->parameter->block_plane;	/*ÿ��plane�е�page��*/
	page_die = page_plane * ssd->parameter->plane_die;						/*ÿ��die�е�page��*/
	page_chip = page_die * ssd->parameter->die_chip;						/*ÿ��chip�е�page��*/
	page_channel = page_chip * ssd->parameter->chip_channel[0];				/*channel[0]�е�page��*/

	/*******************************************************************************
	*page_channel��һ��channel��page����Ŀ�� ppn/page_channel�͵õ������ĸ�channel��
	*��ͬ���İ취���Եõ�chip��die��plane��block��page
	********************************************************************************/
	location->channel = ppn / page_channel;
	location->chip = (ppn % page_channel) / page_chip;
	location->die = ((ppn % page_channel) % page_chip) / page_die;
	location->plane = (((ppn % page_channel) % page_chip) % page_die) / page_plane;
	location->block = ((((ppn % page_channel) % page_chip) % page_die) % page_plane) / ssd->parameter->page_block;
	location->page = (((((ppn % page_channel) % page_chip) % page_die) % page_plane) % ssd->parameter->page_block) % ssd->parameter->page_block;

	return location;
}


/*****************************************************************************
*��������Ĺ����Ǹ��ݲ���channel��chip��die��plane��block��page���ҵ�������ҳ��
*�����ķ���ֵ�����������ҳ��
******************************************************************************/
unsigned int find_ppn(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int page)
{
	unsigned int ppn = 0;
	unsigned int i = 0;
	int page_plane = 0, page_die = 0, page_chip = 0;
	int page_channel[100];                  /*��������ŵ���ÿ��channel��page��Ŀ*/

#ifdef DEBUG
	printf("enter find_psn,channel:%d, chip:%d, die:%d, plane:%d, block:%d, page:%d\n", channel, chip, die, plane, block, page);
#endif

	/*********************************************
	*�����plane��die��chip��channel�е�page����Ŀ
	**********************************************/
	page_plane = ssd->parameter->page_block * ssd->parameter->block_plane;
	page_die = page_plane * ssd->parameter->plane_die;
	page_chip = page_die * ssd->parameter->die_chip;
	while (i < ssd->parameter->channel_number)
	{
		page_channel[i] = ssd->parameter->chip_channel[i] * page_chip;
		i++;
	}

	/****************************************************************************
	*��������ҳ��ppn��ppn��channel��chip��die��plane��block��page��page�������ܺ�
	*****************************************************************************/
	i = 0;
	while (i < channel)
	{
		ppn = ppn + page_channel[i];
		i++;
	}
	ppn = ppn + page_chip * chip + page_die * die + page_plane * plane + block * ssd->parameter->page_block + page;

	return ppn;
}

/********************************
*���������ǻ��һ�����������״̬
*********************************/
int set_entry_state(struct ssd_info* ssd, unsigned int lsn, unsigned int size)
{
	int temp, state, move;

	temp = ~(0xffffffff << size);
	move = lsn % ssd->parameter->subpage_page;
	state = temp << move;

	return state;
}

struct ssd_info* pre_process_superpage(struct ssd_info* ssd)
{
	int fl = 0;
	unsigned int device, lsn, size, ope, lpn, full_page;
	unsigned int largest_lsn, sub_size, ppn, add_size = 0;
	unsigned int i = 0, j, k, l;
	int map_entry_new, map_entry_old, modify;
	int flag = 0;
	int last_flag = 0;
	char buffer_request[200];
	unsigned int oringin_lsn = 0, mulriple = 0;
	struct local* location;
	__int64 time;
	errno_t err;
	unsigned int break_flag = 0;
	unsigned int new_page_flag = 0;
	static unsigned int pre_process_sub_request = 0, request_num = 0;

	printf("\n");
	printf("begin pre_process_page.................\n");

	if ((err = fopen_s(&(ssd->tracefile), ssd->tracefilename, "r")) != 0)      /*��trace�ļ����ж�ȡ����*/
	{
		printf("the trace file can't open\n");
		return NULL;
	}
	/*���SSDͨ����Ŀ�Ƿ���ȷ*/
	if (ssd->parameter->channel_number % CHANNEL_NUM != 0)
	{
		printf("the SSD is not suit for the parity strip\n");
		return NULL;
	}

	full_page = ~(0xffffffff << (ssd->parameter->subpage_page));
	/*��������ssd������߼�������*/
	largest_lsn = (unsigned int)((ssd->parameter->chip_num * ssd->parameter->die_chip * ssd->parameter->plane_die * ssd->parameter->block_plane * ssd->parameter->page_block * ssd->parameter->subpage_page) * (1 - ssd->parameter->overprovide));//*(1-ssd->parameter->aged_ratio);

	largest_lsn = largest_lsn / (ssd->parameter->subpage_page * BAND_WITDH) * (ssd->parameter->subpage_page * (BAND_WITDH - MAX_EC_MODLE));//ԭʼ�����������
	ssd->free_superblock_num--; //���˵�ǰ�����suoerblock֮�⣬ʣ��Ŀ��и���
	while (fgets(buffer_request, 200, ssd->tracefile))
	{
		sscanf_s(buffer_request, "%I64u %d %d %d %d", &time, &device, &lsn, &size, &ope);
		fl++;
		trace_assert(time, device, lsn, size, ope);                         /*���ԣ���������time��device��lsn��size��ope���Ϸ�ʱ�ͻᴦ��*/
		request_num++;
		/*if(request_num == 326)//335
			printf("-2");*/

		lsn = lsn % largest_lsn;                                    /*��ֹ��õ�lsn������lsn����*/
		add_size = 0;                                                     /*add_size����������Ѿ�Ԥ����Ĵ�С*/

		if (ope == 1)                                                      /*����ֻ�Ƕ������Ԥ������Ҫ��ǰ����Ӧλ�õ���Ϣ������Ӧ�޸�*/
		{
			while (add_size < size)
			{
				sub_size = ssd->parameter->subpage_page - (lsn % ssd->parameter->subpage_page);	/*�������Ҫ��ȡ��subpage�ĸ���*/
				if (add_size + sub_size >= size)                             /*ֻ�е�һ������Ĵ�СС��һ��page�Ĵ�Сʱ�����Ǵ���һ����������һ��pageʱ������������*/
				{
					sub_size = size - add_size;
					add_size += sub_size;
				}

				if ((sub_size > ssd->parameter->subpage_page) || (add_size > size))/*��Ԥ����һ���Ӵ�Сʱ�������С����һ��page�����Ѿ�����Ĵ�С����size�ͱ���*/
				{
					printf("pre_process sub_size:%d\n", sub_size);
				}

				/*******************************************************************************************************
				*�����߼�������lsn������߼�ҳ��lpn
				*�ж����dram��ӳ���map����lpnλ�õ�״̬
				*A�����״̬==0����ʾ��ǰû��д����������Ҫֱ�ӽ�ub_size��С����ҳд��ȥд��ȥ
				*B�����״̬>0����ʾ����ǰ��д��������Ҫ��һ���Ƚ�״̬����Ϊ��д��״̬��������ǰ��״̬���ص��������ĵط�
				********************************************************************************************************/
				lpn = lsn / ssd->parameter->subpage_page;
				new_page_flag = 0;
				if (ssd->dram->map->map_entry[lpn].state == 0)                 //״̬Ϊ0�����
				{
					new_page_flag = 1;
					pre_process_sub_request++;
					if (pre_process_sub_request > largest_lsn / 4 * (1 - ssd->parameter->aged_ratio))
						break_flag = 1;
					//�������get_ppn_for_pre_process����Ϊÿ�����������ȡԤ�����ppn��������д����ʱ�������У�����ݣ�����superpage��Сһ����д��flash
					pre_process_for_read_request(ssd, lsn, sub_size);

				}
				else if (ssd->dram->map->map_entry[lpn].state > 0)           //״̬��Ϊ0�����
				{
					//�õ��µ�״̬������ԭ����״̬���õ�һ����״̬
					map_entry_new = set_entry_state(ssd, lsn, sub_size);
					map_entry_old = ssd->dram->map->map_entry[lpn].state;
					modify = map_entry_new | map_entry_old;

					//�޸���ز�����ͳ����Ϣ		
					ppn = ssd->dram->map->map_entry[lpn].pn;
					location = find_location(ssd, ppn);
					/*
					ssd->program_count++;
					ssd->channel_head[location->channel].program_count++;
					ssd->channel_head[location->channel].chip_head[location->chip].program_count++;
					*/
					ssd->dram->map->map_entry[lsn / ssd->parameter->subpage_page].state = modify;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state = modify;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state = ((~modify) & full_page);

					free(location);
					location = NULL;
				}
				lsn = lsn + sub_size;				//�¸����������ʼλ��
				add_size += sub_size;			//�Ѿ������˵�add_size��С�仯
			}
		}
	}
	pre_process_for_last_read_request(ssd);
	printf("\n");
	printf("pre_process is complete!\n");

	fclose(ssd->tracefile);

	for (i = 0; i < ssd->parameter->channel_number; i++)
	{
		for (l = 0; l < ssd->parameter->chip_channel[0]; l++)
		{
			for (j = 0; j < ssd->parameter->die_chip; j++)
			{
				for (k = 0; k < ssd->parameter->plane_die; k++)
				{
					fprintf(ssd->outputfile, "channel:%d,chip:%d,die:%d,plane:%d have free page: %d\n", i, l, j, k, ssd->channel_head[i].chip_head[l].die_head[j].plane_head[k].free_page);
					fflush(ssd->outputfile);
				}
			}
		}
	}
	return ssd;
}


unsigned int pre_process_for_read_request(struct ssd_info* ssd, unsigned int lsn, unsigned int sub_size)
{
	unsigned int i, j, lpn;
	unsigned int plane_channel, plane_chip, plane_die;
	unsigned int channel, chip, die, plane;
	unsigned int active_superblock;
	int ec_mode;
	int map_flag = 0;
	unsigned int ppn;
	struct local* location;
	unsigned int parity_state = 0;
	int current_superpage = 0;
	unsigned int first_parity;
	unsigned int full_page;
	full_page = ~(0xffffffff << (ssd->parameter->subpage_page));
	lpn = lsn / ssd->parameter->subpage_page;
	// ��channel0��chip0��die0��plane0��Ѱ���Ƿ���ڻ�Ծ������
	if (find_superblock_for_pre_read(ssd, 0, 0, 0, 0) == FAILURE)
	{
		printf("the read operation is expand the capacity of SSD\n");
		return 0;
	}
	active_superblock = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].active_block; //��ȡ��ǰ�������
	ec_mode = ssd->band_head[active_superblock].ec_modle; //��ȡ��Ҫд�ĳ������Ӧ��ECģʽ


	current_superpage = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page + 1; //��ȡ��Ҫд�ĳ���ҳ	
	if (current_superpage >= (int)(ssd->parameter->page_block))
	{
		ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page = 0;
		printf("error! the last write page larger than 64!!\n");
		return ERROR;
	}
	first_parity = BAND_WITDH - (((current_superpage + 1) * ec_mode) % BAND_WITDH); //�ҳ���һ��У���λ��
	if (ssd->strip_bit == 0)
	{
		for (i = first_parity; i < first_parity + ec_mode; i++)
		{
			ssd->strip_bit = ssd->strip_bit | (1ll << (i % BAND_WITDH));
		}
	}
	i = find_first_zero(ssd, ssd->strip_bit);
	ssd->strip_bit = ssd->strip_bit | (1ll << i);
	ssd->dram->superpage_buffer[i].lpn = lpn;
	ssd->dram->superpage_buffer[i].size = sub_size;
	ssd->dram->superpage_buffer[i].state = set_entry_state(ssd, lsn, sub_size);
	//�������������������ҳ���룬�����У��ҳ״̬������У��ҳ����һ���Խ���superpage��С������Ԥд��������
	if (size(ssd->strip_bit) == BAND_WITDH)
	{
		//����У��ҳ״̬
		for (i = 0; i < BAND_WITDH; i++)
		{
			parity_state |= ssd->dram->superpage_buffer[i].state;
		}
		//����У������
		for (i = first_parity; i < first_parity + ec_mode; i++)
		{
			ssd->dram->superpage_buffer[i % BAND_WITDH].lpn = -2;
			ssd->dram->superpage_buffer[i % BAND_WITDH].size = size(parity_state);
			ssd->dram->superpage_buffer[i % BAND_WITDH].state = parity_state;
		}
		//����ǰsuperpage��С�Ķ�����Ԥ��д��������
		plane_die = ssd->parameter->plane_die;
		plane_chip = plane_die * ssd->parameter->die_chip;
		plane_channel = plane_chip * ssd->parameter->chip_channel[0];
		for (i = 0; i < BAND_WITDH; i++)
		{
			channel = i / plane_channel;
			chip = (i % plane_channel) / plane_chip;
			die = (i % plane_chip) / plane_die;
			plane = i % plane_die;

			if (write_page(ssd, channel, chip, die, plane, active_superblock, &ppn) == ERROR)
			{
				return 0;
			}
			location = find_location(ssd, ppn);
			//printf("ppn = %d, channel = %d\tchip = %d\tdie = %d\tplane = %d\tblock=%d\tpage = %d\n", ppn, channel, chip, die, plane, active_superblock, current_superpage);
			/*
			ssd->program_count++;
			ssd->channel_head[location->channel].program_count++;
			ssd->channel_head[location->channel].chip_head[location->chip].program_count++;
			*/
			lpn = ssd->dram->superpage_buffer[i].lpn;
			if (lpn != -2) {
				ssd->dram->map->map_entry[lpn].pn = ppn;
				ssd->dram->map->map_entry[lpn].state = ssd->dram->superpage_buffer[i].state;   //0001
			}
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn = lpn;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state = ssd->dram->map->map_entry[lpn].state;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state = ((~ssd->dram->map->map_entry[lpn].state) & full_page);

			if (location->page == 0 && map_flag == 0) {

				map_flag = 1;
				for (j = 0; j < BAND_WITDH; j++) {
					ssd->dram->map->pbn[active_superblock][j] = j * ssd->parameter->block_plane + active_superblock;
				}
			}

			free(location);
			location = NULL;
		}
		//printf("\n");
		ssd->strip_bit = 0;
		memset(ssd->dram->superpage_buffer, 0, BAND_WITDH * sizeof(struct sub_request));
	}
	return 1;
}
unsigned int pre_process_for_last_read_request(struct ssd_info* ssd)
{
	unsigned int i, j, lpn;
	unsigned int plane_channel, plane_chip, plane_die;
	unsigned int channel, chip, die, plane;
	unsigned int active_superblock;
	int ec_mode;
	int map_flag = 0;
	unsigned int ppn;
	struct local* location;
	unsigned int parity_state = 0;
	int current_superpage = 0;
	unsigned int first_parity;
	unsigned int full_page;
	full_page = ~(0xffffffff << (ssd->parameter->subpage_page));

	// ��channel0��chip0��die0��plane0��Ѱ���Ƿ���ڻ�Ծ������
	if (find_superblock_for_pre_read(ssd, 0, 0, 0, 0) == FAILURE)
	{
		printf("the read operation is expand the capacity of SSD\n");
		return 0;
	}
	active_superblock = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].active_block; //��ȡ��ǰ�������
	ec_mode = ssd->band_head[active_superblock].ec_modle; //��ȡ��Ҫд�ĳ������Ӧ��ECģʽ


	current_superpage = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page + 1; //��ȡ��Ҫд�ĳ���ҳ	
	if (current_superpage >= (int)(ssd->parameter->page_block))
	{
		ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page = 0;
		printf("error! the last write page larger than 64!!\n");
		return ERROR;
	}
	first_parity = BAND_WITDH - (((current_superpage + 1) * ec_mode) % BAND_WITDH); //�ҳ���һ��У���λ��

	//����У��ҳ״̬
	for (i = 0; i < BAND_WITDH; i++)
	{
		parity_state |= ssd->dram->superpage_buffer[i].state;
	}
	//����У������
	for (i = first_parity; i < first_parity + ec_mode; i++)
	{
		ssd->dram->superpage_buffer[i % BAND_WITDH].lpn = -2;
		ssd->dram->superpage_buffer[i % BAND_WITDH].size = size(parity_state);
		ssd->dram->superpage_buffer[i % BAND_WITDH].state = parity_state;
	}
	//����ǰsuperpage��С�Ķ�����Ԥ��д��������
	plane_die = ssd->parameter->plane_die;
	plane_chip = plane_die * ssd->parameter->die_chip;
	plane_channel = plane_chip * ssd->parameter->chip_channel[0];
	for (i = 0; i < BAND_WITDH; i++)
	{
		if ((ssd->strip_bit & (1ll << i)) != 0)
		{
			channel = i / plane_channel;
			chip = (i % plane_channel) / plane_chip;
			die = (i % plane_chip) / plane_die;
			plane = i % plane_die;

			if (write_page(ssd, channel, chip, die, plane, active_superblock, &ppn) == ERROR)
			{
				return 0;
			}
			location = find_location(ssd, ppn);
			//printf("ppn = %d, channel = %d\tchip = %d\tdie = %d\tplane = %d\tblock=%d\tpage = %d\n", ppn, channel, chip, die, plane, active_superblock, current_superpage);
			/*
			ssd->program_count++;
			ssd->channel_head[location->channel].program_count++;
			ssd->channel_head[location->channel].chip_head[location->chip].program_count++;
			*/
			lpn = ssd->dram->superpage_buffer[i].lpn;
			if (lpn != -2) {
				ssd->dram->map->map_entry[lpn].pn = ppn;
				ssd->dram->map->map_entry[lpn].state = ssd->dram->superpage_buffer[i].state;   //0001
			}
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn = lpn;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state = ssd->dram->map->map_entry[lpn].state;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state = ((~ssd->dram->map->map_entry[lpn].state) & full_page);

			if (location->page == 0 && map_flag == 0) {

				map_flag = 1;
				for (j = 0; j < BAND_WITDH; j++) {
					ssd->dram->map->pbn[active_superblock][j] = j * ssd->parameter->block_plane + active_superblock;
				}
			}

			free(location);
			location = NULL;
		}
	}
	//printf("\n");
	ssd->strip_bit = 0;
	memset(ssd->dram->superpage_buffer, 0, BAND_WITDH * sizeof(struct sub_request));

	return 1;
}
/**************************************
*����������ΪԤ��������ȡ����ҳ��ppn
*��ȡҳ�ŷ�Ϊ��̬��ȡ�;�̬��ȡ
**************************************/
int count = 0;
unsigned int get_ppn_for_pre_process(struct ssd_info* ssd, unsigned int lsn)
{
	unsigned int channel = 0, chip = 0, die = 0, plane = 0, page = 0;
	unsigned int ppn, lpn;
	unsigned int active_block;
	unsigned long total_band_num = 0;
	unsigned int channel_num = 0, chip_num = 0, die_num = 0, plane_num = 0;
	unsigned int token_flag = 0;//band_num=0;
	unsigned int p_ch = 0;
	unsigned long largest_page_num = 0;

#ifdef DEBUG
	printf("enter get_psn_for_pre_process\n");
#endif

	channel_num = ssd->parameter->channel_number;
	chip_num = ssd->parameter->chip_channel[0];
	die_num = ssd->parameter->die_chip;
	plane_num = ssd->parameter->plane_die;
	lpn = lsn / ssd->parameter->subpage_page;
#ifdef USE_EC
	total_band_num = ssd->band_num;
#else
	total_band_num = ssd->parameter->page_block;//chip_num*die_num*plane_num**ssd->parameter->block_plane
#endif
	if (ssd->parameter->allocation_scheme == 0)                           //��̬��ʽ�»�ȡppn
	{
		if (ssd->parameter->dynamic_allocation == 0)                      //��ʾȫ��̬��ʽ�£�Ҳ����channel��chip��die��plane��block�ȶ��Ƕ�̬����
		{
			channel = ssd->token;
#ifdef USE_EC

			//band_num = ssd->current_band; //����
			ssd->strip_bits[0] = ssd->strip_bits[0] | (1 << ssd->token);
			ssd->chip_num = ssd->channel_head[channel].token;
			//printf("strip+bit = %d\n",ssd->strip_bit);
			// ��ȡ��ǰchip��die��plane��λ���Թ�Ѱ�һ�Ծ��
			chip = ssd->channel_head[channel].token;
			ssd->channel_head[channel].token = (chip + 1) % ssd->parameter->chip_channel[0];
			die = ssd->channel_head[channel].chip_head[chip].token;
			ssd->channel_head[channel].chip_head[chip].token = (die + 1) % ssd->parameter->die_chip;
			plane = ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token = (plane + 1) % ssd->parameter->plane_die;


			//����������Ϊ��һ��ͨ��
			ssd->token = (ssd->token + 1) % ssd->parameter->channel_number;

#else
#ifdef USE_WHITE_PARITY
			largest_page_num = (unsigned int)(total_band_num * ssd->parameter->channel_number * (1 - ssd->parameter->overprovide));

			//ssd->page_num = (ssd->page_num+1)%(largest_page_num);
			ssd->page_num = (ssd->page_num + 1) % (total_band_num * PARITY_SIZE);

			if (ssd->page_num != 0)
			{
				band_num = (ssd->page_num - 1) / PARITY_SIZE;
				//ssd->strip_bit = ssd->strip_bit|(1<<(ssd->page_num-1)%PARITY_SIZE);
			}

			else
			{
				band_num = ssd->parameter->page_block - 1;
				//ssd->strip_bit = ~(0xffffffff<<PARITY_SIZE);
			}

			p_ch = PARITY_SIZE - band_num % (PARITY_SIZE + 1);

			if (((1 + ssd->token) / ssd->parameter->channel_number) == (ssd->token / ssd->parameter->channel_number))//�����ͬһ������
			{
				if (p_ch == (1 + ssd->token) % ssd->parameter->channel_number)
					token_flag = 1;
			}
			else//����ͬһ��������������һ��������У��λ����ͨ�� 0 ��
			{
				if (PARITY_SIZE - ((1 + band_num) % (ssd->parameter->page_block)) % (PARITY_SIZE + 1) == 0)//
					token_flag = 1;
			}
			ssd->strip_bit = ssd->strip_bit | (1 << ssd->token);
			ssd->chip_num = ssd->channel_head[channel].token;
			ssd->token = (ssd->token + 1 + token_flag) % ssd->parameter->channel_number;
#else
			ssd->token = (ssd->token + 1) % ssd->parameter->channel_number;
#endif 
			chip = ssd->channel_head[channel].token;
			ssd->channel_head[channel].token = (chip + 1) % ssd->parameter->chip_channel[0];
			die = ssd->channel_head[channel].chip_head[chip].token;
			ssd->channel_head[channel].chip_head[chip].token = (die + 1) % ssd->parameter->die_chip;
			plane = ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token = (plane + 1) % ssd->parameter->plane_die;
#endif
		}
		else if (ssd->parameter->dynamic_allocation == 1)                 //��ʾ�붯̬��ʽ��channel��̬������package��die��plane��̬����                 
		{
			channel = lpn % ssd->parameter->channel_number;
			chip = ssd->channel_head[channel].token;
			ssd->channel_head[channel].token = (chip + 1) % ssd->parameter->chip_channel[0];
			die = ssd->channel_head[channel].chip_head[chip].token;
			ssd->channel_head[channel].chip_head[chip].token = (die + 1) % ssd->parameter->die_chip;
			plane = ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token = (plane + 1) % ssd->parameter->plane_die;
		}
	}
	else if (ssd->parameter->allocation_scheme == 1)                       //��ʾ��̬���䣬ͬʱҲ��0,1,2,3,4,5��6�в�ͬ��̬���䷽ʽ
	{
		switch (ssd->parameter->static_allocation)
		{

		case 0:
		{
			channel = (lpn / (plane_num * die_num * chip_num)) % channel_num;
			chip = lpn % chip_num;
			die = (lpn / chip_num) % die_num;
			plane = (lpn / (die_num * chip_num)) % plane_num;
			break;
		}
		case 1:
		{
			channel = lpn % channel_num;
			chip = (lpn / channel_num) % chip_num;
			die = (lpn / (chip_num * channel_num)) % die_num;
			plane = (lpn / (die_num * chip_num * channel_num)) % plane_num;

			break;
		}
		case 2:
		{
			channel = lpn % channel_num;
			chip = (lpn / (plane_num * channel_num)) % chip_num;
			die = (lpn / (plane_num * chip_num * channel_num)) % die_num;
			plane = (lpn / channel_num) % plane_num;
			break;
		}
		case 3:
		{
			channel = lpn % channel_num;
			chip = (lpn / (die_num * channel_num)) % chip_num;
			die = (lpn / channel_num) % die_num;
			plane = (lpn / (die_num * chip_num * channel_num)) % plane_num;
			break;
		}
		case 4:
		{
			channel = lpn % channel_num;
			chip = (lpn / (plane_num * die_num * channel_num)) % chip_num;
			die = (lpn / (plane_num * channel_num)) % die_num;
			plane = (lpn / channel_num) % plane_num;

			break;
		}
		case 5:
		{
			channel = lpn % channel_num;
			chip = (lpn / (plane_num * die_num * channel_num)) % chip_num;
			die = (lpn / channel_num) % die_num;
			plane = (lpn / (die_num * channel_num)) % plane_num;

			break;
		}
		default: return 0;
		}
	}

	//�����������䷽���ҵ�channel��chip��die��plane��������������ҵ�active_block�����Ż��ppn
	if (find_active_block(ssd, channel, chip, die, plane) == FAILURE)
	{
		printf("the read operation is expand the capacity of SSD\n");
		return 0;
	}
	active_block = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	if (write_page(ssd, channel, chip, die, plane, active_block, &ppn) == ERROR)
	{
		return 0;
	}
	page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page;
	//printf("channel = %d\tchip = %d\tdie = %d\tplane = %d\tactive_block = %d\tpage = %d\n", channel, chip, die, plane, active_block, page);
#ifdef USE_EC
	ssd->current_band[0] = get_band_id_from_ppn(ssd, ppn); //����ppn��ȡband_id
	//printf("band_id=%d data_ch=%d chip=%d die=%d plane=%d  ",ssd->current_band, channel, chip, die, plane);
	if (ssd->current_band[0] == 1) {
		count++;
	}
#endif

	return ppn;
}

#ifdef USE_EC
/***************************************************************************************************
*������������������channel��chip��die��plane�����ҵ�һ��active_blockȻ���������block�����ҵ�һ��ҳ��
*������find_ppn�ҵ�ppn��
****************************************************************************************************/
struct ssd_info* get_ppn(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, struct sub_request* sub)
{
	int old_ppn = -1;
	unsigned int ppn, lpn, full_page;
	unsigned int active_block;
	//unsigned int block;
	unsigned int page, flag = 0, flag1 = 0;
	unsigned int p_state = 0, state = 0, copy_subpage = 0;
	struct local* location;
	struct direct_erase* direct_erase_node, * new_direct_erase;
	struct gc_operation* gc_node;
	unsigned int band_num = 0, p_ch = 0;
	int i = 0, pbn = 0;
	struct sub_request* parity_sub = NULL;
	unsigned int first_parity = 0;
	unsigned int pbn_offset = 0;

	unsigned int j = 0, k = 0, l = 0, m = 0, n = 0;

	unsigned int block_die, block_chip;
	unsigned int band2_chip, band2_die, band2_plane;
	first_parity = ssd->parameter->channel_number - ssd->band_head[band_num].ec_modle; //�������е�һ��У����ͨ����
/*
#ifdef DEBUG
	printf("enter get_ppn,channel:%d, chip:%d, die:%d, plane:%d\n",channel,chip,die,plane);
#endif
*/
	full_page = ~(0xffffffff << (ssd->parameter->subpage_page));
	lpn = sub->lpn;

	/*************************************************************************************
	*���ú���find_active_block��channel��chip��die��plane�ҵ���Ծblock
	*�����޸����channel��chip��die��plane��active_block�µ�last_write_page��free_page_num
	**************************************************************************************/
	if (find_active_block(ssd, channel, chip, die, plane) == FAILURE)
	{
		printf("get_ppn()\tERROR :there is no free page in channel:%d, chip:%d, die:%d, plane:%d\n", channel, chip, die, plane);
		return NULL;
	}
	active_block = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page + 1; //��д���pageƫ��
	ppn = find_ppn(ssd, channel, chip, die, plane, active_block, page);
	band_num = get_band_id_from_ppn(ssd, ppn);
	//�ҵ��Ǽ�¼�е��ĸ�����
	for (i = 0; i < 4; i++)
	{
		if (abs((int)band_num - ssd->current_band[i]) < 2)
		{
			break;
		}
	}
	//������ڼ�¼�У���������Ϊ�µ���������˵��������¼�����⣬���޸�
	if (i == 4)
	{
		//printf("get_ppn()\tThe band nummber is error!\tchannel(%d) chip(%d) die(%d) plane(%d)\n", channel, chip, die, plane);
		return NULL;
	}
	//printf("pos\tchannel = %d\tchip = %d\tdie = %d\tplane = %d\tblock = %d\n", channel, chip, die, plane, active_block);
	//printf("last_page = %d\tfree_page = %d\n", ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page,
//											   ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num);
	if (write_page(ssd, channel, chip, die, plane, active_block, &ppn) == ERROR)
	{
		printf("get_ppn()\terror! the last write page larger than 64!!\n");
		return NULL;
	}
	ssd->strip_bits[i] = ssd->strip_bits[i] | (1 << channel);
	page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page; //��д���pageƫ��
	// �޸������ppn,location��Ϣ
	sub->location->channel = channel;
	sub->location->chip = chip;
	sub->location->die = die;
	sub->location->plane = plane;
	sub->location->block = active_block;
	sub->location->page = page;
	sub->ppn = ppn;



	/*
		printf("%d\t%d\t%d\t%d\t%d\t%d\n", channel, chip, die, plane, active_block, page, ppn);
		printf("band_num = %d\tband0 = %d\tband1 = %d\tband2 = %d\tband2 = %d\n",band_num, ssd->current_band[0], ssd->current_band[1], ssd->current_band[2], ssd->current_band[3]);
		printf("strip_bit0 = %d\tstrip_bit1 = %d\tstrip_bit2 = %d\tstrip_bit3 = %d\n",ssd->strip_bits[0], ssd->strip_bits[1], ssd->strip_bits[2], ssd->strip_bits[3]);
		*/
		//����ҳʱ������lpn2ppn��ӳ���ϵ
	if (channel < first_parity)
	{
		if (ssd->dram->map->map_entry[lpn].state == 0)                                       /*this is the first logical page*/
		{
			if (ssd->dram->map->map_entry[lpn].pn != 0)
			{
				printf("Error in get_ppn()\n");
			}
			ssd->dram->map->map_entry[lpn].pn = ppn;
			state = sub->state;
			ssd->dram->map->map_entry[lpn].state = state;
		}
		else                                                                            /*����߼�ҳ�����˸��£���Ҫ��ԭ����ҳ��ΪʧЧ*/
		{
			old_ppn = ssd->dram->map->map_entry[lpn].pn;
			location = find_location(ssd, old_ppn);  //��ȡ��ҳ��λ��
			if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn != lpn)
			{
				printf("\nError in get_ppn()\n");
			}
			// ����ҳ��Ϊ��Ч
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state = 0;             /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state = 0;              /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn = 0;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;

			/*******************************************************************************************
			*��block��ȫ��invalid��ҳ������ֱ��ɾ�������ڴ���һ���ɲ����Ľڵ㣬����location�µ�plane����
			********************************************************************************************/
			if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num == ssd->parameter->page_block)
			{
				new_direct_erase = (struct direct_erase*)malloc(sizeof(struct direct_erase));
				alloc_assert(new_direct_erase, "new_direct_erase");
				memset(new_direct_erase, 0, sizeof(struct direct_erase));

				new_direct_erase->block = location->block;
				new_direct_erase->next_node = NULL;
				direct_erase_node = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
				if (direct_erase_node == NULL)
				{
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node = new_direct_erase;
				}
				else
				{
					new_direct_erase->next_node = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node = new_direct_erase;
				}
			}

			free(location);
			location = NULL;
			ssd->dram->map->map_entry[lpn].pn = ppn;
			state = ssd->dram->map->map_entry[lpn].state | sub->state;
			ssd->dram->map->map_entry[lpn].state = state;
		}
		/*
		//�޸�ssd��program_count,free_page�ȱ���
		ssd->program_count++;
		ssd->channel_head[channel].program_count++;
		ssd->channel_head[channel].chip_head[chip].program_count++;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].lpn=lpn;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].valid_state=state;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].free_state=((~state)&full_page);
		*/

		// ��������д������ҳ�������У�����������Ӧͨ����
		if (size(ssd->strip_bits[i]) == first_parity)
		{
			//printf("creat parity sub_request!\n");
			p_state = 0;
			// ��ȡУ��ҳ��״̬
			for (i = 0; i < first_parity; ++i) {
				p_state = p_state | ssd->channel_head[i].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].valid_state;
			}
			if (p_state == 0)
				printf("parity_sub->state\n");

			for (p_ch = first_parity; p_ch < ssd->parameter->channel_number; p_ch++) {
				parity_sub = (struct sub_request*)malloc(sizeof(struct sub_request));
				alloc_assert(parity_sub, "parity_sub");
				memset(parity_sub, 0, sizeof(struct sub_request));

				parity_sub->state = p_state;
				parity_sub->lpn = -2;
				parity_sub->operation = WRITE;
				parity_sub->size = size(parity_sub->state);
				parity_sub->current_state = SR_WAIT;
				parity_sub->current_time = ssd->current_time;
				parity_sub->begin_time = ssd->current_time;

				parity_sub->location = (struct local*)malloc(sizeof(struct local));
				alloc_assert(parity_sub->location, "parity_sub->location");
				memset(parity_sub->location, -1, sizeof(struct local));
				parity_sub->location->channel = p_ch;
				//����У�����󵽶�Ӧͨ����д�����������
				insert_sub_to_queue_head(ssd, parity_sub, p_ch);
				ssd->write_sub_request++;
			}
			//services_2_write(ssd,p_ch,&ssd->channel_head[p_ch].busy_flag,&ssd->change_current_t_flag);
		}
	}
	else
	{	//У������

		state = sub->state;
		location = sub->location;
		if (channel == first_parity)
		{
			//pbn_offsetΪ��ǰͨ���е�pbnƫ�ƣ������в�ͬ��������pbn = channel_id * block_channel + pbn_offset
			pbn_offset = (ssd->parameter->block_plane * ssd->parameter->plane_die * ssd->parameter->die_chip) * location->chip
				+ (ssd->parameter->block_plane * ssd->parameter->plane_die) * location->die
				+ (ssd->parameter->block_plane) * location->plane
				+ location->block;
			for (i = 0; i < ssd->parameter->channel_number; ++i) {
				ssd->dram->map->pbn[ssd->current_band[0]][i] = i * ssd->band_num + pbn_offset;
			}
		}
	}

	//�޸�SSD��Ϣ
	ssd->program_count++;
	ssd->channel_head[channel].program_count++;
	ssd->channel_head[channel].chip_head[chip].program_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].lpn = lpn;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].valid_state = state;
	//printf("channel = %d\tstate = %d\n", channel, state);
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].free_state = ((~state) & full_page);

	//�޸�����
	//ssd->token = (ssd->token + 1) % ssd->parameter->channel_number;
	//ssd->chip_num = ssd->channel_head[location->channel].token;
	ssd->channel_head[channel].token = (chip + 1) % ssd->parameter->chip_channel[0];
	ssd->channel_head[channel].chip_head[chip].token = (die + 1) % ssd->parameter->die_chip;
	ssd->channel_head[channel].chip_head[chip].die_head[die].token = (plane + 1) % ssd->parameter->plane_die;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].free_state == 15)
		printf("\tfree_state error\n");

	if (ssd->parameter->active_write == 0)                                            /*���û���������ԣ�ֻ����gc_hard_threshold�������޷��ж�GC����*/
	{                                                                               /*���plane�е�free_page����Ŀ����gc_hard_threshold���趨����ֵ�Ͳ���gc����*/
		//channel = 0; //������GC��ֻ��鿴ͨ��0�Ŀ���ҳ
		if ((ssd->gc_request < 5 * ssd->parameter->channel_number) ||
			(ssd->channel_head[0].chip_head[chip].die_head[die].plane_head[plane].free_page < (ssd->parameter->page_block * ssd->parameter->block_plane * ssd->parameter->gc_hard_threshold) * 0.5))
			if (ssd->channel_head[0].chip_head[chip].die_head[die].plane_head[plane].free_page < (ssd->parameter->page_block * ssd->parameter->block_plane * ssd->parameter->gc_hard_threshold))
			{
				gc_node = (struct gc_operation*)malloc(sizeof(struct gc_operation));
				alloc_assert(gc_node, "gc_node");
				memset(gc_node, 0, sizeof(struct gc_operation));

				gc_node->next_node = NULL;
				gc_node->chip = chip;
				gc_node->die = die;
				gc_node->plane = plane;
				gc_node->block = 0xffffffff;
				gc_node->page = 0;
				gc_node->state = GC_WAIT;
				gc_node->priority = GC_UNINTERRUPT;
				gc_node->next_node = ssd->channel_head[0].gc_command;
				ssd->channel_head[0].gc_command = gc_node;
				ssd->gc_request++;
			}
	}

	//�����¼�еĵ�һ��������д�꣨����ҳ+У��ҳ�����򽫸���������¼�����������ź�����λͼ��
	if (size(ssd->strip_bits[0]) == ssd->parameter->channel_number)
	{
		ssd->strip_bits[0] = ssd->strip_bits[1];
		ssd->strip_bits[1] = ssd->strip_bits[2];
		ssd->strip_bits[2] = ssd->strip_bits[3];
		ssd->strip_bits[3] = 0;

		ssd->current_band[0] = ssd->current_band[1];
		ssd->current_band[1] = ssd->current_band[2];
		ssd->current_band[2] = ssd->current_band[3];
		block_die = ssd->parameter->plane_die * ssd->parameter->block_plane;
		block_chip = ssd->parameter->die_chip * block_die;
		chip = (ssd->current_band[3] / block_chip) % ssd->parameter->chip_channel[0];
		die = ((ssd->current_band[3] % block_chip) / block_die) % ssd->parameter->die_chip;
		plane = ((ssd->current_band[3] % block_chip % block_die) / ssd->parameter->block_plane) % ssd->parameter->plane_die;
		//printf("before  band2\tchannel = %d\tchip = %d\tdie = %d\tplane = %d\tblock = %d\tpage = %d\n", channel, chip, die, plane, active_block, page);
/*
		chip = (ssd->channel_head[channel].token + 2) / ssd->parameter->chip_channel[0];
		die = ssd->channel_head[channel].chip_head[chip].token;
		plane = ssd->channel_head[channel].chip_head[chip].die_head[die].token;
		*/
		band2_chip = (chip + 1) % ssd->parameter->chip_channel[0];
		if ((chip + 1) / ssd->parameter->chip_channel[0]) {
			band2_die = (die + 1) % ssd->parameter->die_chip;
			if ((die + 1) / ssd->parameter->plane_die) {
				band2_plane = (plane + 1) % ssd->parameter->plane_die;
			}
			else
			{
				band2_plane = plane;
			}
		}
		else
		{
			band2_die = die;
			band2_plane = plane;
		}
		find_active_block(ssd, channel, band2_chip, band2_die, band2_plane);
		active_block = ssd->channel_head[channel].chip_head[band2_chip].die_head[band2_die].plane_head[band2_plane].active_block;
		page = ssd->channel_head[channel].chip_head[band2_chip].die_head[band2_die].plane_head[band2_plane].blk_head[active_block].last_write_page + 1;
		ppn = find_ppn(ssd, channel, band2_chip, band2_die, band2_plane, active_block, page);
		//printf("after   band2\tchannel = %d\tchip = %d\tdie = %d\tplane = %d\tblock = %d\tpage = %d\n", channel, band2_chip, band2_die, band2_plane, active_block, page);
		ssd->current_band[3] = get_band_id_from_ppn(ssd, ppn);//
	}
	//printf("band0 = %d\tband1 = %d\tband2 = %d\n",ssd->current_band[0], ssd->current_band[1], ssd->current_band[2]);
	//printf("sucess!\n");
	return ssd;
}

#else
/***************************************************************************************************
*������������������channel��chip��die��plane�����ҵ�һ��active_blockȻ���������block�����ҵ�һ��ҳ��
*������find_ppn�ҵ�ppn��
****************************************************************************************************/
struct ssd_info* get_ppn(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, struct sub_request* sub)
{
	int old_ppn = -1;
	unsigned int ppn, lpn, full_page;
	unsigned int active_block;
	unsigned int block;
	unsigned int page, flag = 0, flag1 = 0;
	unsigned int old_state = 0, state = 0, copy_subpage = 0;
	struct local* location;
	struct direct_erase* direct_erase_node, * new_direct_erase;
	struct gc_operation* gc_node;
	unsigned int band_num = 0, p_ch = 0;
	int i = 0, pbn = 0;
	struct sub_request* parity_sub = NULL;

	unsigned int j = 0, k = 0, l = 0, m = 0, n = 0;

#ifdef DEBUG
	printf("enter get_ppn,channel:%d, chip:%d, die:%d, plane:%d\n", channel, chip, die, plane);
#endif

	full_page = ~(0xffffffff << (ssd->parameter->subpage_page));
	lpn = sub->lpn;

	/*************************************************************************************
	*���ú���find_active_block��channel��chip��die��plane�ҵ���Ծblock
	*�����޸����channel��chip��die��plane��active_block�µ�last_write_page��free_page_num
	**************************************************************************************/
	if (find_active_block(ssd, channel, chip, die, plane) == FAILURE)
	{
		printf("ERROR :there is no free page in channel:%d, chip:%d, die:%d, plane:%d\n", channel, chip, die, plane);
		return ssd;
	}

	active_block = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page > 63)
	{
		printf("error! the last write page larger than 64!!\n");
		while (1) {}
	}

	block = active_block;
	page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page;

#ifdef USE_WHITE_PARITY
	if (lpn != -2)
#endif 
		if (ssd->dram->map->map_entry[lpn].state == 0)                                       /*this is the first logical page*/
		{
			if (ssd->dram->map->map_entry[lpn].pn != 0)
			{
				printf("Error in get_ppn()\n");
			}
			ssd->dram->map->map_entry[lpn].pn = find_ppn(ssd, channel, chip, die, plane, block, page);
			ssd->dram->map->map_entry[lpn].state = sub->state;
		}
		else                                                                            /*����߼�ҳ�����˸��£���Ҫ��ԭ����ҳ��ΪʧЧ*/
		{
			ppn = ssd->dram->map->map_entry[lpn].pn;
			location = find_location(ssd, ppn);
			if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn != lpn)
			{
				printf("\nError in get_ppn()\n");
			}

			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state = 0;             /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state = 0;              /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn = 0;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;

			/*******************************************************************************************
			*��block��ȫ��invalid��ҳ������ֱ��ɾ�������ڴ���һ���ɲ����Ľڵ㣬����location�µ�plane����
			********************************************************************************************/
			if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num == ssd->parameter->page_block)
			{
				new_direct_erase = (struct direct_erase*)malloc(sizeof(struct direct_erase));
				alloc_assert(new_direct_erase, "new_direct_erase");
				memset(new_direct_erase, 0, sizeof(struct direct_erase));

				new_direct_erase->block = location->block;
				new_direct_erase->next_node = NULL;
				direct_erase_node = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
				if (direct_erase_node == NULL)
				{
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node = new_direct_erase;
				}
				else
				{
					new_direct_erase->next_node = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node = new_direct_erase;
				}
			}

			free(location);
			location = NULL;
			ssd->dram->map->map_entry[lpn].pn = find_ppn(ssd, channel, chip, die, plane, block, page);
			ssd->dram->map->map_entry[lpn].state = (ssd->dram->map->map_entry[lpn].state | sub->state);
		}


	sub->location->channel = channel;
	sub->location->chip = chip;
	sub->location->die = die;
	sub->location->plane = plane;
	sub->location->block = active_block;
	sub->location->page = page;
#ifdef USE_WHITE_PARITY
	sub->ppn = find_ppn(ssd, channel, chip, die, plane, block, page);
	i = 0;
	if (sub->location->page == 0)
	{
		location = sub->location;
		while (i < PARITY_SIZE + 1)
		{
			pbn = //(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_channel[0])*location->channel
				(ssd->parameter->block_plane * ssd->parameter->plane_die * ssd->parameter->die_chip) * location->chip
				+ (ssd->parameter->block_plane * ssd->parameter->plane_die) * location->die
				+ (ssd->parameter->block_plane) * location->plane
				+ location->block;
			ssd->dram->map->pbn[pbn][i] = pbn;
			i++;
		}
		location = NULL;
	}
#else
	sub->ppn = ssd->dram->map->map_entry[lpn].pn; 									 /*�޸�sub�������ppn��location�ȱ���*/
#endif 

	ssd->program_count++;                                                           /*�޸�ssd��program_count,free_page�ȱ���*/
	ssd->channel_head[channel].program_count++;
	ssd->channel_head[channel].chip_head[chip].program_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].lpn = lpn;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].valid_state = sub->state;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].free_state = ((~(sub->state)) & full_page);
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].written_count++;
	ssd->write_flash_count++;
	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].free_state == 15)
		printf("\tfree_state error\n");


#ifdef USE_WHITE_PARITY

	if (sub->lpn != -2)
		ssd->page_num = (ssd->page_num + 1) % (ssd->parameter->page_block * PARITY_SIZE);
	if (ssd->page_num != 0)
	{
		//ssd->strip_bit = ssd->strip_bit|(1<<(ssd->page_num-1)%PARITY_SIZE);
		band_num = (ssd->page_num - 1) / PARITY_SIZE;
	}
	else
	{
		band_num = ssd->parameter->page_block - 1;
		//ssd->strip_bit = ~(0xffffffff<<PARITY_SIZE);
	}

	ssd->strip_bit = ssd->strip_bit | (1 << channel);
	ssd->chip_num = chip;
	/*if(ssd->strip_bit == 31)
		printf("strip error\n");*/
		//if(ssd->page_num % PARITY_SIZE == 0)
	if (size(ssd->strip_bit) == PARITY_SIZE)
	{


		p_ch = PARITY_SIZE - band_num % (PARITY_SIZE + 1);

		parity_sub = (struct sub_request*)malloc(sizeof(struct sub_request));
		alloc_assert(parity_sub, "parity_sub");
		memset(parity_sub, 0, sizeof(struct sub_request));
		i = 0;
		while (i < PARITY_SIZE + 1)
		{
			if (i != p_ch)
				parity_sub->state = parity_sub->state | ssd->channel_head[i].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].valid_state;
			i++;
		}
		if (parity_sub->state == 0)
			printf("parity_sub->state");
		parity_sub->lpn = -2;
		parity_sub->operation = WRITE;
		parity_sub->size = size(parity_sub->state);
		parity_sub->current_state = SR_WAIT;
		parity_sub->current_time = ssd->current_time;
		parity_sub->begin_time = ssd->current_time;

		parity_sub->location = (struct local*)malloc(sizeof(struct local));
		alloc_assert(parity_sub->location, "parity_sub->location");
		memset(parity_sub->location, -1, sizeof(struct local));
		parity_sub->location->channel = p_ch;

		//insert the parity_sub to the first channel queue;
		insert_sub_to_queue_head(ssd, parity_sub, p_ch);
		ssd->write_sub_request++;
		//services_2_write(ssd,p_ch,&ssd->channel_head[p_ch].busy_flag,&ssd->change_current_t_flag);
	}

#endif 


	if (ssd->parameter->active_write == 0)                                            /*���û���������ԣ�ֻ����gc_hard_threshold�������޷��ж�GC����*/
	{                                                                               /*���plane�е�free_page����Ŀ����gc_hard_threshold���趨����ֵ�Ͳ���gc����*/
#ifdef USE_WHITE_PARITY
		channel = 0;
#endif
		if ((ssd->gc_request < 5 * ssd->parameter->channel_number) ||
			(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page < (ssd->parameter->page_block * ssd->parameter->block_plane * ssd->parameter->gc_hard_threshold) * 0.5))
			if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page < (ssd->parameter->page_block * ssd->parameter->block_plane * ssd->parameter->gc_hard_threshold))
			{
				gc_node = (struct gc_operation*)malloc(sizeof(struct gc_operation));
				alloc_assert(gc_node, "gc_node");
				memset(gc_node, 0, sizeof(struct gc_operation));

				gc_node->next_node = NULL;
				gc_node->chip = chip;
				gc_node->die = die;
				gc_node->plane = plane;
				gc_node->block = 0xffffffff;
				gc_node->page = 0;
				gc_node->state = GC_WAIT;
				gc_node->priority = GC_UNINTERRUPT;
				gc_node->next_node = ssd->channel_head[channel].gc_command;
				ssd->channel_head[channel].gc_command = gc_node;
				ssd->gc_request++;
			}
	}

	return ssd;
}
#endif
/*****************************************************************************************
*��������������ڸ�����channel��chip��die��plane��Ϊgc����Ѱ���µ�ppn��
*��Ϊ��gc��������Ҫ�ҵ��µ��������ԭ��������ϵ�����
*��gc��Ѱ���������ĺ�������������ѭ����gc����
******************************************************************************************/
unsigned int get_ppn_for_gc(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane)
{
	unsigned int ppn;
	unsigned int active_block, block, page;

#ifdef DEBUG
	printf("enter get_ppn_for_gc,channel:%d, chip:%d, die:%d, plane:%d\n", channel, chip, die, plane);
#endif

	if (find_active_block(ssd, channel, chip, die, plane) != SUCCESS)
	{
		printf("\n\n Error int get_ppn_for_gc().\n");
		return 0xffffffff;
	}

	active_block = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page > 63)
	{
		printf("error! the last write page larger than 64!!\n");
		while (1) {}
	}

	block = active_block;
	page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page;

	ppn = find_ppn(ssd, channel, chip, die, plane, block, page);

	ssd->program_count++;
	ssd->channel_head[channel].program_count++;
	ssd->channel_head[channel].chip_head[chip].program_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].written_count++;
	ssd->write_flash_count++;
	ssd->write_flash_gc_count++;

	return ppn;

}

/*********************************************************************************************************************
* ��־�� ��2011��7��28���޸�
*�����Ĺ��ܾ���erase_operation������������channel��chip��die��plane�µ�block������
*Ҳ���ǳ�ʼ�����block����ز�����eg��free_page_num=page_block��invalid_page_num=0��last_write_page=-1��erase_count++
*�������block�����ÿ��page����ز���ҲҪ�޸ġ�
*********************************************************************************************************************/
/*
Status erase_operation(struct ssd_info * ssd,unsigned int channel ,unsigned int chip ,unsigned int die ,unsigned int plane ,unsigned int block)
{
	unsigned int i=0;
	if(ssd->erase_count > 8000)
		for (i=0;i<ssd->parameter->page_block;i++)
			if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state != 0)
				ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num=ssd->parameter->page_block;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num=0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page=-1;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].erase_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].gc_flag = 0;
	for (i=0;i<ssd->parameter->page_block;i++)
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state=PG_SUB;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state=0;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lpn=-1;
	}
	ssd->erase_count++;
	ssd->channel_head[channel].erase_count++;
	ssd->channel_head[channel].chip_head[chip].erase_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page+=ssd->parameter->page_block;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].can_erase_block--;

	return SUCCESS;

}
*/
/**************************************************************************************
*��������Ĺ����Ǵ���INTERLEAVE_TWO_PLANE��INTERLEAVE��TWO_PLANE��NORMAL�µĲ����Ĳ�����
***************************************************************************************/
Status erase_planes(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die1, unsigned int plane1, unsigned int command)
{
	unsigned int die = 0;
	unsigned int plane = 0;
	unsigned int block = 0;
	struct direct_erase* direct_erase_node = NULL;
	unsigned int block0 = 0xffffffff;
	unsigned int block1 = 0;

	if ((ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node == NULL) ||
		((command != INTERLEAVE_TWO_PLANE) && (command != INTERLEAVE) && (command != TWO_PLANE) && (command != NORMAL)))     /*���û�в�������������command���ԣ����ش���*/
	{
		return ERROR;
	}

	/************************************************************************************************************
	*�����������ʱ������Ҫ���Ͳ����������channel��chip���ڴ��������״̬����CHANNEL_TRANSFER��CHIP_ERASE_BUSY
	*��һ״̬��CHANNEL_IDLE��CHIP_IDLE��
	*************************************************************************************************************/
	block1 = ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node->block;

	ssd->channel_head[channel].current_state = CHANNEL_TRANSFER;
	ssd->channel_head[channel].current_time = ssd->current_time;
	ssd->channel_head[channel].next_state = CHANNEL_IDLE;

	ssd->channel_head[channel].chip_head[chip].current_state = CHIP_ERASE_BUSY;
	ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
	ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;

	/*****************************************************************************************
	*�߼�����INTERLEAVE_TWO_PLANE�Ĵ���
	*��ÿһ��die�У�
	*���ҵ���һ�����ڿ�ֱ��ɾ�����plane����¼���һ��block�Ŀ��block0���ٲ���block0
	*Ȼ���ٽ�����plane�ĵ�һ����ֱ��ɾ����Ŀ��block��block0���жԱȣ������ͬ����в���
	******************************************************************************************/
	if (command == INTERLEAVE_TWO_PLANE)
	{
		for (die = 0; die < ssd->parameter->die_chip; die++)
		{
			block0 = 0xffffffff;
			for (plane = 0; plane < ssd->parameter->plane_die; plane++)
			{
				direct_erase_node = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
				if (direct_erase_node != NULL)
				{

					block = direct_erase_node->block;

					if (block0 == 0xffffffff)
					{
						block0 = block;
					}
					else
					{
						if (block != block0)
						{
							continue;
						}

					}
					ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node = direct_erase_node->next_node;
					erase_operation(ssd, channel, chip, die, plane, block);     /*��ʵ�Ĳ��������Ĵ���*/
					free(direct_erase_node);
					direct_erase_node = NULL;
					ssd->direct_erase_count++;
				}

			}
		}

		ssd->interleave_mplane_erase_count++;                             /*������һ��interleave two plane erase����,��������������ʱ�䣬�Լ���һ��״̬��ʱ��*/
		ssd->channel_head[channel].next_state_predict_time = ssd->current_time + 18 * ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tWB;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time - 9 * ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tBERS;

	}
	/*****************************************************************
	*�߼�����INTERLEAVE�Ĵ���
	*�Բ���ָ����die1��ֻ�Բ���ָ����plane1�еĿ�ֱ��ɾ������в���
	*������die��ÿһ��plane�ĵ�һ����ֱ��ɾ������в���
	******************************************************************/
	else if (command == INTERLEAVE)
	{
		for (die = 0; die < ssd->parameter->die_chip; die++)
		{
			for (plane = 0; plane < ssd->parameter->plane_die; plane++)
			{
				if (die == die1)
				{
					plane = plane1;
				}
				direct_erase_node = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
				if (direct_erase_node != NULL)
				{
					block = direct_erase_node->block;
					ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node = direct_erase_node->next_node;
					erase_operation(ssd, channel, chip, die, plane, block);
					free(direct_erase_node);
					direct_erase_node = NULL;
					ssd->direct_erase_count++;
					break;
				}
			}
		}
		ssd->interleave_erase_count++;
		ssd->channel_head[channel].next_state_predict_time = ssd->current_time + 14 * ssd->parameter->time_characteristics.tWC;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time + ssd->parameter->time_characteristics.tBERS;
	}
	/*******************************************************
	*�߼�����TWO_PLANE�Ĵ���
	*�ڲ���ָ����die1�в���ÿ��plane�ĵ�һ����ֱ��ɾ���飬
	*�����block��block1��ͬ���������в���
	********************************************************/
	else if (command == TWO_PLANE)
	{

		for (plane = 0; plane < ssd->parameter->plane_die; plane++)
		{
			direct_erase_node = ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane].erase_node;
			if ((direct_erase_node != NULL))
			{
				block = direct_erase_node->block;
				if (block == block1)
				{
					ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane].erase_node = direct_erase_node->next_node;
					erase_operation(ssd, channel, chip, die1, plane, block);
					free(direct_erase_node);
					direct_erase_node = NULL;
					ssd->direct_erase_count++;
				}
			}
		}

		ssd->mplane_erase_conut++;
		ssd->channel_head[channel].next_state_predict_time = ssd->current_time + 14 * ssd->parameter->time_characteristics.tWC;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time + ssd->parameter->time_characteristics.tBERS;
	}
	else if (command == NORMAL)                                             /*��ͨ����NORMAL�Ĵ���*/
	{
		direct_erase_node = ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node;
		block = direct_erase_node->block;
		ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node = direct_erase_node->next_node;
		free(direct_erase_node);
		direct_erase_node = NULL;
		erase_operation(ssd, channel, chip, die1, plane1, block);

		ssd->direct_erase_count++;
		ssd->channel_head[channel].next_state_predict_time = ssd->current_time + 5 * ssd->parameter->time_characteristics.tWC;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time + ssd->parameter->time_characteristics.tWB + ssd->parameter->time_characteristics.tBERS;
	}
	else
	{
		return ERROR;
	}

	direct_erase_node = ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node;

	if (((direct_erase_node) != NULL) && (direct_erase_node->block == block1))
	{//������command����INTERLEAVE_TWO_PLANE��INTERLEAVE��TWO_PLANE��NORMAL֮һ��ʱ����ܻ�����������
		return FAILURE;
	}
	else
	{
		return SUCCESS;
	}
}



/*******************************************************************************************************************
*GC������ĳ��plane��free��������ֵ���д�������ĳ��plane������ʱ��GC����ռ�����plane���ڵ�die����Ϊdie��һ��������Ԫ��
*��һ��die��GC���������������ĸ�planeͬʱerase������interleave erase������GC����Ӧ������������ʱֹͣ���ƶ����ݺͲ���
*ʱ���У����Ǽ�϶ʱ�����ֹͣGC���������Է����µ�������󣬵��������������������϶ʱ�䣬����GC������������������
*GC��ֵ��һ������ֵ��һ��Ӳ��ֵ������ֵ��ʾ�������ֵ�󣬿��Կ�ʼ������GC���������ü�Ъʱ�䣬GC���Ա��µ��������жϣ�
*������Ӳ��ֵ��ǿ����ִ��GC�������Ҵ�GC�������ܱ��жϣ�ֱ���ص�Ӳ��ֵ���ϡ�
*������������棬�ҳ����die���е�plane�У���û�п���ֱ��ɾ����block��Ҫ���еĻ�������interleave two plane���ɾ��
*��Щblock�������ж���plane������ֱ��ɾ����block��ͬʱɾ�������еĻ��������ǵ������plane����ɾ��������Ҳ������Ļ���
*ֱ����������gc_parallelism�������н�һ��GC�������ú���Ѱ��ȫ��Ϊinvalid�Ŀ飬ֱ��ɾ�����ҵ���ֱ��ɾ���ķ���1��û����
*������-1��
*********************************************************************************************************************/
int gc_direct_erase(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane)
{
	unsigned int lv_die = 0, lv_plane = 0;                                                           /*Ϊ����������ʹ�õľֲ����� Local variables*/
	unsigned int interleaver_flag = FALSE, muilt_plane_flag = FALSE;
	unsigned int normal_erase_flag = TRUE;

	struct direct_erase* direct_erase_node1 = NULL;
	struct direct_erase* direct_erase_node2 = NULL;

	direct_erase_node1 = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
	if (direct_erase_node1 == NULL)
	{
		return FAILURE;
	}
	//�ҵ�plane�е�һ������ֱ�Ӳ�����block��������Ĵ�erase_node������ɾ��
	while (direct_erase_node1 != NULL)
	{
		if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[direct_erase_node1->block].invalid_page_num != ssd->parameter->page_block)
		{

			direct_erase_node1 = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node->next_node;
			free(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node);
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node = direct_erase_node1;
		}
		else
		{
			break;
		}
	}
	if (direct_erase_node1 == NULL)
	{
		return FAILURE;
	}
	/********************************************************************************************************
	*Two_plane:������plane�����λ�õ�ҳ���в��ж�д
	*���ܴ���TWOPLANE�߼�����ʱ��������Ӧ��channel��chip��die��������ͬ��plane�ҵ�����ִ��TWOPLANE������block
	*����muilt_plane_flagΪTRUE
	*********************************************************************************************************/
	if ((ssd->parameter->advanced_commands & AD_TWOPLANE) == AD_TWOPLANE)
	{
		for (lv_plane = 0; lv_plane < ssd->parameter->plane_die; lv_plane++)
		{
			direct_erase_node2 = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
			if ((lv_plane != plane) && (direct_erase_node2 != NULL))
			{
				if ((direct_erase_node1->block) == (direct_erase_node2->block))
				{
					muilt_plane_flag = TRUE;
					break;
				}
			}
		}
	}

	/***************************************************************************************
	*Interleave:ͬһ��оƬ��ͬdies��ͬʱִ�ж��pages�Ķ�/д/����
	*���ܴ���INTERLEAVE�߼�����ʱ��������Ӧ��channel��chip�ҵ�����ִ��INTERLEAVE������block
	*����interleaver_flagΪTRUE
	****************************************************************************************/
	if ((ssd->parameter->advanced_commands & AD_INTERLEAVE) == AD_INTERLEAVE)
	{
		for (lv_die = 0; lv_die < ssd->parameter->die_chip; lv_die++)
		{
			if (lv_die != die)
			{
				for (lv_plane = 0; lv_plane < ssd->parameter->plane_die; lv_plane++)
				{
					if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node != NULL)
					{
						interleaver_flag = TRUE;
						break;
					}
				}
			}
			if (interleaver_flag == TRUE)
			{
				break;
			}
		}
	}

	/************************************************************************************************************************
	*A������ȿ���ִ��twoplane������block���п���ִ��interleaver������block����ô��ִ��INTERLEAVE_TWO_PLANE�ĸ߼������������
	*B�����ֻ����ִ��interleaver������block����ô��ִ��INTERLEAVE�߼�����Ĳ�������
	*C�����ֻ����ִ��TWO_PLANE������block����ô��ִ��TWO_PLANE�߼�����Ĳ�������
	*D��û��������Щ�������ô��ֻ�ܹ�ִ����ͨ�Ĳ���������
	*************************************************************************************************************************/
	if ((muilt_plane_flag == TRUE) && (interleaver_flag == TRUE) && ((ssd->parameter->advanced_commands & AD_TWOPLANE) == AD_TWOPLANE) && ((ssd->parameter->advanced_commands & AD_INTERLEAVE) == AD_INTERLEAVE))
	{
		if (erase_planes(ssd, channel, chip, die, plane, INTERLEAVE_TWO_PLANE) == SUCCESS)
		{
			return SUCCESS;
		}
	}
	else if ((interleaver_flag == TRUE) && ((ssd->parameter->advanced_commands & AD_INTERLEAVE) == AD_INTERLEAVE))
	{
		if (erase_planes(ssd, channel, chip, die, plane, INTERLEAVE) == SUCCESS)
		{
			return SUCCESS;
		}
	}
	else if ((muilt_plane_flag == TRUE) && ((ssd->parameter->advanced_commands & AD_TWOPLANE) == AD_TWOPLANE))
	{
		if (erase_planes(ssd, channel, chip, die, plane, TWO_PLANE) == SUCCESS)
		{
			return SUCCESS;
		}
	}

	if ((normal_erase_flag == TRUE))                              /*����ÿ��plane���п���ֱ��ɾ����block��ֻ�Ե�ǰplane������ͨ��erase����������ֻ��ִ����ͨ����*/
	{
		if (erase_planes(ssd, channel, chip, die, plane, NORMAL) == SUCCESS)
		{
			return SUCCESS;
		}
		else
		{
			return FAILURE;                                     /*Ŀ���planeû�п���ֱ��ɾ����block����ҪѰ��Ŀ����������ʵʩ��������*/
		}
	}
	return SUCCESS;
}

/*****************************************************************
*����:������location��ָ�������ҳ���ƶ���ͬһplane�Ĳ�ͬblock��
******************************************************************/
Status move_page(struct ssd_info* ssd, struct local* location, unsigned int* transfer_size)
{
	struct local* new_location = NULL;
	unsigned int free_state = 0, valid_state = 0;
	unsigned int lpn = 0, old_ppn = 0, ppn = 0;

	lpn = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn;
	valid_state = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state;
	free_state = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state;
	old_ppn = find_ppn(ssd, location->channel, location->chip, location->die, location->plane, location->block, location->page);      /*��¼�����Ч�ƶ�ҳ��ppn���Ա�map���߶���ӳ���ϵ�е�ppn������ɾ������Ӳ���*/
	ppn = get_ppn_for_gc(ssd, location->channel, location->chip, location->die, location->plane);                /*�ҳ�����ppnһ�����ڷ���gc������plane��,����ʹ��copyback������Ϊgc������ȡppn*/

	new_location = find_location(ssd, ppn);                                                                   /*�����»�õ�ppn��ȡnew_location*/

	/*********************************************************************************************************
	*Copyback��ͬһ��plane��һ��page���Ƶ���һ��page����ռ��I/O���ߡ���Ҫ��������Դpage��Ŀ��pageͬ��ͬż
	**********************************************************************************************************/
	if ((ssd->parameter->advanced_commands & AD_COPYBACK) == AD_COPYBACK)
	{
		if (ssd->parameter->greed_CB_ad == 1)                                                                /*̰����ʹ�ø߼�����*/
		{
			ssd->copy_back_count++;
			ssd->gc_copy_back++;
			while (old_ppn % 2 != ppn % 2)
			{//���˹۵�:��Ϊpage�ķ����ǰ�˳��ģ����Ը�ѭ��ֻ��ִ��һ�Σ����Ըĳ�if
				ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].free_state = 0;
				ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].lpn = 0;
				ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].valid_state = 0;
				ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].invalid_page_num++;

				free(new_location);
				new_location = NULL;

				ppn = get_ppn_for_gc(ssd, location->channel, location->chip, location->die, location->plane);    /*�ҳ�����ppnһ�����ڷ���gc������plane�У�����������ż��ַ����,����ʹ��copyback����*/
				ssd->program_count--;
				ssd->write_flash_count--;
				ssd->waste_page_count++;
			}
			if (new_location == NULL)
			{
				new_location = find_location(ssd, ppn);
			}

			ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].free_state = free_state;
			ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].lpn = lpn;
			ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].valid_state = valid_state;
		}
		else
		{
			if (old_ppn % 2 != ppn % 2)
			{
				(*transfer_size) += size(valid_state);
			}
			else
			{

				ssd->copy_back_count++;
				ssd->gc_copy_back++;
			}
		}
	}
	else
	{
		(*transfer_size) += size(valid_state);
	}
	ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].free_state = free_state;
	ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].lpn = lpn;
	ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].valid_state = valid_state;


	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state = 0;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn = 0;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state = 0;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;

	if (old_ppn == ssd->dram->map->map_entry[lpn].pn)                                                     /*�޸�ӳ���*/
	{
		ssd->dram->map->map_entry[lpn].pn = ppn;
	}

	free(new_location);
	new_location = NULL;

	return SUCCESS;
}

/*******************************************************************************************************************************************
*Ŀ���planeû�п���ֱ��ɾ����block����ҪѰ��Ŀ����������ʵʩ�������������ڲ����жϵ�gc�����У��ɹ�ɾ��һ���飬����1��û��ɾ��һ���鷵��-1
*����������У����ÿ���Ŀ��channel,die�Ƿ��ǿ��е�,����invalid_page_num����block
********************************************************************************************************************************************/
int uninterrupt_gc(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane)
{
	unsigned int i = 0, invalid_page = 0;
	unsigned int block, active_block, transfer_size, free_page, page_move_count = 0;                           /*��¼ʧЧҳ���Ŀ��*/
	struct local* location = NULL;
	unsigned int total_invalid_page_num = 0;

	if (find_active_block(ssd, channel, chip, die, plane) != SUCCESS)                                           /*��ȡ��Ծ��*/
	{
		printf("\n\n Error in uninterrupt_gc().\n");
		return ERROR;
	}
	active_block = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;

	invalid_page = 0;
	transfer_size = 0;
	block = -1;
	for (i = 0; i < ssd->parameter->block_plane; i++)                                                           /*�������invalid_page�Ŀ�ţ��Լ�����invalid_page_num*/
	{
		total_invalid_page_num += ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
		if ((active_block != i) && (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num > invalid_page))
		{
			invalid_page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
			block = i;
		}
	}
	if (block == -1)
	{
		return 1;
	}

	//if(invalid_page<5)
	//{
		//printf("\ntoo less invalid page. \t %d\t %d\t%d\t%d\t%d\t%d\t\n",invalid_page,channel,chip,die,plane,block);
	//}

	free_page = 0;
	for (i = 0; i < ssd->parameter->page_block; i++)		                                                     /*������ÿ��page���������Ч���ݵ�page��Ҫ�ƶ��������ط��洢*/
	{
		if ((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state & PG_SUB) == 0x0000000f)
		{
			free_page++;
		}
		if (free_page != 0)
		{
			printf("\ntoo much free page. \t %d\t .%d\t%d\t%d\t%d\t\n", free_page, channel, chip, die, plane);
		}
		if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state > 0) /*��ҳ����Чҳ����Ҫcopyback����*/
		{
			location = (struct local*)malloc(sizeof(struct local));
			alloc_assert(location, "location");
			memset(location, 0, sizeof(struct local));

			location->channel = channel;
			location->chip = chip;
			location->die = die;
			location->plane = plane;
			location->block = block;
			location->page = i;
			move_page(ssd, location, &transfer_size);                                                   /*��ʵ��move_page����*/
			page_move_count++;

			free(location);
			location = NULL;
		}
	}
	ssd->total_gc_move_page_count = ssd->total_gc_move_page_count + page_move_count;
	erase_operation(ssd, channel, chip, die, plane, block);	                                              /*ִ����move_page�����󣬾�����ִ��block�Ĳ�������*/

	ssd->channel_head[channel].current_state = CHANNEL_GC;
	ssd->channel_head[channel].current_time = ssd->current_time;
	ssd->channel_head[channel].next_state = CHANNEL_IDLE;
	ssd->channel_head[channel].chip_head[chip].current_state = CHIP_ERASE_BUSY;
	ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
	ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;

	/***************************************************************
	*�ڿ�ִ��COPYBACK�߼������벻��ִ��COPYBACK�߼���������������£�
	*channel�¸�״̬ʱ��ļ��㣬�Լ�chip�¸�״̬ʱ��ļ���
	***************************************************************/
	if ((ssd->parameter->advanced_commands & AD_COPYBACK) == AD_COPYBACK)
	{
		if (ssd->parameter->greed_CB_ad == 1)
		{

			ssd->channel_head[channel].next_state_predict_time = ssd->current_time + page_move_count * (7 * ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tR + 7 * ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tPROG);
			ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time + ssd->parameter->time_characteristics.tBERS;
		}
	}
	else
	{

		ssd->channel_head[channel].next_state_predict_time = ssd->current_time + page_move_count * (7 * ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tR + 7 * ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tPROG) + transfer_size * SECTOR * (ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tRC);
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time + ssd->parameter->time_characteristics.tBERS;

	}

	return 1;
}


/*******************************************************************************************************************************************
*Ŀ���planeû�п���ֱ��ɾ����block����ҪѰ��Ŀ����������ʵʩ�������������ڿ����жϵ�gc�����У��ɹ�ɾ��һ���飬����1��û��ɾ��һ���鷵��-1
*����������У����ÿ���Ŀ��channel,die�Ƿ��ǿ��е�
********************************************************************************************************************************************/
int interrupt_gc(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, struct gc_operation* gc_node)
{
	unsigned int i, block, active_block, transfer_size, invalid_page = 0;
	struct local* location;

	active_block = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	transfer_size = 0;
	if (gc_node->block >= ssd->parameter->block_plane)
	{
		for (i = 0; i < ssd->parameter->block_plane; i++)
		{
			if ((active_block != i) && (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num > invalid_page))
			{
				invalid_page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
				block = i;
			}
		}
		gc_node->block = block;
	}

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[gc_node->block].invalid_page_num != ssd->parameter->page_block)     /*����Ҫִ��copyback����*/
	{
		for (i = gc_node->page; i < ssd->parameter->page_block; i++)
		{
			if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[gc_node->block].page_head[i].valid_state > 0)
			{
				location = (struct local*)malloc(sizeof(struct local));
				alloc_assert(location, "location");
				memset(location, 0, sizeof(struct local));

				location->channel = channel;
				location->chip = chip;
				location->die = die;
				location->plane = plane;
				location->block = block;
				location->page = i;
				transfer_size = 0;

				move_page(ssd, location, &transfer_size);

				free(location);
				location = NULL;

				gc_node->page = i + 1;
				ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[gc_node->block].invalid_page_num++;
				ssd->channel_head[channel].current_state = CHANNEL_C_A_TRANSFER;
				ssd->channel_head[channel].current_time = ssd->current_time;
				ssd->channel_head[channel].next_state = CHANNEL_IDLE;
				ssd->channel_head[channel].chip_head[chip].current_state = CHIP_COPYBACK_BUSY;
				ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
				ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;

				if ((ssd->parameter->advanced_commands & AD_COPYBACK) == AD_COPYBACK)
				{
					ssd->channel_head[channel].next_state_predict_time = ssd->current_time + 7 * ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tR + 7 * ssd->parameter->time_characteristics.tWC;
					ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time + ssd->parameter->time_characteristics.tPROG;
				}
				else
				{
					ssd->channel_head[channel].next_state_predict_time = ssd->current_time + (7 + transfer_size * SECTOR) * ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tR + (7 + transfer_size * SECTOR) * ssd->parameter->time_characteristics.tWC;
					ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time + ssd->parameter->time_characteristics.tPROG;
				}
				return 0;
			}
		}
	}
	else
	{
		erase_operation(ssd, channel, chip, die, plane, gc_node->block);

		ssd->channel_head[channel].current_state = CHANNEL_C_A_TRANSFER;
		ssd->channel_head[channel].current_time = ssd->current_time;
		ssd->channel_head[channel].next_state = CHANNEL_IDLE;
		ssd->channel_head[channel].next_state_predict_time = ssd->current_time + 5 * ssd->parameter->time_characteristics.tWC;

		ssd->channel_head[channel].chip_head[chip].current_state = CHIP_ERASE_BUSY;
		ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
		ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time + ssd->parameter->time_characteristics.tBERS;

		return 1;                                                                      /*��gc������ɣ�����1�����Խ�channel�ϵ�gc����ڵ�ɾ��*/
	}

	printf("there is a problem in interrupt_gc\n");
	return 1;
}

/*************************************************************
*����:��gc_node��channel�е�gc��(��ͷ��gc_command)ɾ��
**************************************************************/
int delete_gc_node(struct ssd_info* ssd, unsigned int channel, struct gc_operation* gc_node)
{
	struct gc_operation* gc_pre = NULL;
	if (gc_node == NULL)
	{
		return ERROR;
	}

	if (gc_node == ssd->channel_head[channel].gc_command)
	{
		ssd->channel_head[channel].gc_command = gc_node->next_node;
	}
	else
	{
		gc_pre = ssd->channel_head[channel].gc_command;
		while (gc_pre->next_node != NULL)
		{
			if (gc_pre->next_node == gc_node)
			{
				gc_pre->next_node = gc_node->next_node;
				break;
			}
			gc_pre = gc_pre->next_node;
		}
	}
	free(gc_node);
	gc_node = NULL;
	ssd->gc_request--;
	return SUCCESS;
}

/***************************************
*��������Ĺ����Ǵ���channel��ÿ��gc����
****************************************/
Status gc_for_channel(struct ssd_info* ssd, unsigned int channel)
{
	int flag_direct_erase = 1, flag_gc = 1, flag_invoke_gc = 1;
	unsigned int chip, die, plane, flag_priority = 0;
	unsigned int current_state = 0, next_state = 0;
	long long next_state_predict_time = 0;
	struct gc_operation* gc_node = NULL, * gc_p = NULL;

	/*******************************************************************************************
	*����ÿһ��gc_node����ȡgc_node���ڵ�chip�ĵ�ǰ״̬���¸�״̬���¸�״̬��Ԥ��ʱ��
	*�����ǰ״̬�ǿ��У������¸�״̬�ǿ��ж��¸�״̬��Ԥ��ʱ��С�ڵ�ǰʱ�䣬�����ǲ����жϵ�gc
	*��ô��flag_priority��Ϊ1������Ϊ0
	********************************************************************************************/
	gc_node = ssd->channel_head[channel].gc_command;
	while (gc_node != NULL)
	{
		current_state = ssd->channel_head[channel].chip_head[gc_node->chip].current_state;
		next_state = ssd->channel_head[channel].chip_head[gc_node->chip].next_state;
		next_state_predict_time = ssd->channel_head[channel].chip_head[gc_node->chip].next_state_predict_time;
		if ((current_state == CHIP_IDLE) || ((next_state == CHIP_IDLE) && (next_state_predict_time <= ssd->current_time)))
		{
			if (gc_node->priority == GC_UNINTERRUPT)                                     /*���gc�����ǲ����жϵģ����ȷ������gc����*/
			{
				flag_priority = 1;
				break;
			}
		}
		gc_node = gc_node->next_node;
	}
	if (flag_priority != 1)                                                              /*û���ҵ������жϵ�gc��������ִ�ж��׵�gc����*/
	{
		gc_node = ssd->channel_head[channel].gc_command;
		while (gc_node != NULL)
		{
			current_state = ssd->channel_head[channel].chip_head[gc_node->chip].current_state;
			next_state = ssd->channel_head[channel].chip_head[gc_node->chip].next_state;
			next_state_predict_time = ssd->channel_head[channel].chip_head[gc_node->chip].next_state_predict_time;
			/******************************************************************
			*��Ҫgc������Ŀ��chip�ǿ��еģ�
			*��chip����һ��״̬�ǿ��е�����һ״̬��Ԥ��ʱ��С��ssd�ĵ�ǰʱ�䣬
			*�ſ��Խ���gc����
			*******************************************************************/
			if ((current_state == CHIP_IDLE) || ((next_state == CHIP_IDLE) && (next_state_predict_time <= ssd->current_time)))
			{
				break;
			}
			gc_node = gc_node->next_node;
		}

	}
	if (gc_node == NULL)
	{
		return FAILURE;
	}

	chip = gc_node->chip;
	die = gc_node->die;
	plane = gc_node->plane;

	/*************************************************************************************************************
	*�����жϵ�gc����
	*���ȵ���gc_direct_erase����������channel,chip,die,plane�¿���ֱ��ɾ����block��
	*���û�п���ֱ�Ӳ�����block�����uninterrupt_gc����������channel,chip,die,plane��invalid_page_num����block
	*����ɲ�������֮����Ҫ����ǰgc����ڵ��Ƴ��������
	**************************************************************************************************************/
	if (gc_node->priority == GC_UNINTERRUPT)
	{
		flag_direct_erase = gc_direct_erase(ssd, channel, chip, die, plane);
		if (flag_direct_erase != SUCCESS)
		{
			flag_gc = uninterrupt_gc(ssd, channel, chip, die, plane);                         /*��һ��������gc�������ʱ���Ѿ�����һ���飬������һ��������flash�ռ䣩������1����channel����Ӧ��gc��������ڵ�ɾ��*/
			if (flag_gc == 1)
			{
				delete_gc_node(ssd, channel, gc_node);
			}
		}
		else
		{
			delete_gc_node(ssd, channel, gc_node);
		}
		return SUCCESS;
	}
	/*************************************************************************************************************
	*���жϵ�gc����
	*��Ҫ����ȷ�ϸ�channel��û�������������ʱ����Ҫʹ�����channel��û�еĻ�����ִ��gc�������еĻ�����ִ��gc����
	*���ȵ���gc_direct_erase����������channel,chip,die,plane�¿���ֱ��ɾ����block��
	*���û�п���ֱ�Ӳ�����block�����uninterrupt_gc����������channel,chip,die,plane��invalid_page_num����block
	*����ɲ�������֮����Ҫ����ǰgc����ڵ��Ƴ��������
	**************************************************************************************************************/
	else
	{
		flag_invoke_gc = decide_gc_invoke(ssd, channel);                                  /*�ж��Ƿ�����������Ҫchannel���������������Ҫ���channel����ô���gc�����ͱ��ж���*/

		if (flag_invoke_gc == 1)
		{
			flag_direct_erase = gc_direct_erase(ssd, channel, chip, die, plane);
			if (flag_direct_erase == -1)
			{
				flag_gc = interrupt_gc(ssd, channel, chip, die, plane, gc_node);             /*��һ��������gc�������ʱ���Ѿ�����һ���飬������һ��������flash�ռ䣩������1����channel����Ӧ��gc��������ڵ�ɾ��*/
				if (flag_gc == 1)
				{
					delete_gc_node(ssd, channel, gc_node);
				}
			}
			else if (flag_direct_erase == 1)
			{
				delete_gc_node(ssd, channel, gc_node);
			}
			return SUCCESS;
		}
		else
		{
			return FAILURE;
		}
	}
}



/************************************************************************************************************
*flag�������gc��������ssd��������idle������±����õģ�1��������ȷ����channel��chip��die��plane�����ã�0��
*����gc��������Ҫ�ж��Ƿ��ǲ����жϵ�gc����������ǣ���Ҫ��һ����Ŀ��block��ȫ�����������ɣ�����ǿ��жϵģ�
*�ڽ���GC����ǰ����Ҫ�жϸ�channel��die�Ƿ����������ڵȴ����������û����ʼһ��һ���Ĳ������ҵ�Ŀ��
*���һ��ִ��һ��copyback����������gc��������ʱ����ǰ�ƽ���������һ��copyback����erase����
*����gc������һ����Ҫ����gc��������Ҫ����һ�����жϣ�������Ӳ��ֵ����ʱ���������gc����������������ֵ����ʱ��
*��Ҫ�жϣ������channel���Ƿ����������ڵȴ�(��д������ȴ��Ͳ��У�gc��Ŀ��die����busy״̬Ҳ����)�����
*�оͲ�ִ��gc���������������ִ��һ������
************************************************************************************************************/
unsigned int gc(struct ssd_info* ssd, unsigned int channel, unsigned int flag)
{
	unsigned int i;
	int flag_direct_erase = 1, flag_gc = 1, flag_invoke_gc = 1;
	unsigned int flag_priority = 0;
	struct gc_operation* gc_node = NULL, * gc_p = NULL;

	if (flag == 1)	/*����ssd����IDEL�����*/
	{
		for (i = 0; i < ssd->parameter->channel_number; i++)
		{
			flag_priority = 0;
			flag_direct_erase = 1;
			flag_gc = 1;
			flag_invoke_gc = 1;
			gc_node = NULL;
			gc_p = NULL;
			if ((ssd->channel_head[i].current_state == CHANNEL_IDLE) || (ssd->channel_head[i].next_state == CHANNEL_IDLE && ssd->channel_head[i].next_state_predict_time <= ssd->current_time))
			{
				channel = i;
				if (ssd->channel_head[channel].gc_command != NULL)
				{
					gc_for_channel(ssd, channel);
				}
			}
		}
		return SUCCESS;

	}
	else		/*ֻ�����ĳ���ض���channel��chip��die����gc����Ĳ���(ֻ���Ŀ��die�����ж������ǲ���idle��*/
	{
		if ((ssd->parameter->allocation_scheme == 1) || ((ssd->parameter->allocation_scheme == 0) && (ssd->parameter->dynamic_allocation == 1)))
		{
			if ((ssd->channel_head[channel].subs_r_head != NULL) || (ssd->channel_head[channel].subs_w_head != NULL))    /*�������������ȷ�������*/
			{
				return 0;
			}
		}

		gc_for_channel(ssd, channel);
		return SUCCESS;
	}
}



/**********************************************************
*�ж�channel�Ƿ�������ռ�ã����Ƿ���������ȴ�
*���û�У�����1�Ϳ��Է���gc����
*����з���0���Ͳ���ִ��gc������gc�������ж�
***********************************************************/
int decide_gc_invoke(struct ssd_info* ssd, unsigned int channel)
{
	struct sub_request* sub;
	struct local* location;

	if ((ssd->channel_head[channel].subs_r_head == NULL) && (ssd->channel_head[channel].subs_w_head == NULL))    /*������Ҷ�д�������Ƿ���Ҫռ�����channel�����õĻ�����ִ��GC����*/
	{
		return 1;                                                                        /*��ʾ��ǰʱ�����channelû����������Ҫռ��channel*/
	}
	else
	{
		if (ssd->channel_head[channel].subs_w_head != NULL)
		{
			return 0;
		}
		else if (ssd->channel_head[channel].subs_r_head != NULL)
		{
			sub = ssd->channel_head[channel].subs_r_head;
			while (sub != NULL)
			{
				/*************************************************************************************
				*����������Ǵ��ڵȴ�״̬��
				*�������Ŀ��chip����idle����chip����һ״̬��idle����һ״̬��Ԥ��ʱ��С��ϵͳ��ǰʱ��
				*����ִ��gc����������0
				**************************************************************************************/
				if (sub->current_state == SR_WAIT)
				{
					location = find_location(ssd, sub->ppn);
					if ((ssd->channel_head[location->channel].chip_head[location->chip].current_state == CHIP_IDLE) || ((ssd->channel_head[location->channel].chip_head[location->chip].next_state == CHIP_IDLE) &&
						(ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time <= ssd->current_time)))
					{
						free(location);
						location = NULL;
						return 0;
					}
					free(location);
					location = NULL;
				}
				/*************************************************************************************
				*������������һ״̬�����ݴ���״̬��
				*�������Ŀ��chip���ڵ���һ״̬��Ԥ��ʱ��С��ϵͳ��ǰʱ��
				*����ִ��gc����������0
				**************************************************************************************/
				else if (sub->next_state == SR_R_DATA_TRANSFER)
				{
					location = find_location(ssd, sub->ppn);
					if (ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time <= ssd->current_time)
					{
						free(location);
						location = NULL;
						return 0;
					}
					free(location);
					location = NULL;
				}
				sub = sub->next_node;
			}
		}
		return 1;
	}
}


// �ڻ�ȡ�µ�superblockʱ��������superblock��ʣ������Ƿ�����GCҪ������������GC
int gc_for_superblock(struct ssd_info* ssd, struct request* req)
{
	int gc_superblock_number;  //GC��superblock��
	unsigned int gc_subpage_num; //GC��Ǩ����Ч��ҳ�ĸ���
	//̰��Ѱ����Чҳ����superblock
	gc_superblock_number = find_superblock_for_gc(ssd);
	if (gc_superblock_number == -1)
	{
		printf("There is no invalid page in every superblock!\n");
		getchar();
		return FAILURE;
	}
	ouput_bad_block(ssd);
	//�����superblock�е���Ч���ݣ�������д��
	gc_subpage_num = process_for_gc(ssd, req, gc_superblock_number);

	ssd->band_head[gc_superblock_number].pe_cycle++;

	////��P/E���ڴﵽ4/5��������ֵʱ����ECģʽ���ɿ��ԣ��������
	//if (ssd->band_head[gc_superblock_number].pe_cycle > (ssd->parameter->ers_limit * 4) / 5)
	//{
	//	ssd->band_head[gc_superblock_number].ec_modle = 4;
	//}
	return SUCCESS;
}


int find_superblock_for_gc(struct ssd_info* ssd)
{
	int block = -1;
	unsigned int active_superblock;
	unsigned int superblock_invalid_page, largest_superblock_invalid_page;
	unsigned int i, pos, channel, chip, die, plane;

	//ͳ�Ƶ�i��superblock��active_superblock����)����Чҳ��Ŀ
	largest_superblock_invalid_page = 0;
	for (i = 0; i < ssd->parameter->block_plane; i++)
	{
		if (ssd->band_head[i].advance_gc_flag == 1) {
			//����gc���ָ�ĥ��������
			block = i;
			break;
		}
		superblock_invalid_page = 0;
		active_superblock = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].active_block;
		//ͳ�Ƶ�i��superblock����Чҳ��Ŀ
		for (channel = 0; channel < ssd->parameter->channel_number; channel++)
		{
			for (chip = 0; chip < ssd->channel_head[channel].chip; chip++)
			{
				for (die = 0; die < ssd->parameter->die_chip; die++)
				{
					for (plane = 0; plane < ssd->parameter->plane_die; plane++)
					{
						if (i != active_superblock)
							superblock_invalid_page = superblock_invalid_page + ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
					}
				}
			}
		}
		//�ҳ������Чҳ��superblock���
		if (superblock_invalid_page > largest_superblock_invalid_page)
		{
			block = i;
			largest_superblock_invalid_page = superblock_invalid_page;
		}
	}
	if (block == -1)
		return ERROR;
	return block;
	/*
	for(channel=0;channel<ssd->parameter->channel_number;channel++)
	{
		if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].gc_flag)
		{
			//printf("this block  is being gc ed \n");
			return FAILURE;
		}
		if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num != 0)
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].gc_flag = 1;
	}
	*/
}
int process_for_gc(struct ssd_info* ssd, struct request* req, unsigned int block)
{
	unsigned int channel, chip, die, plane, page, sub_page;
	unsigned int plane_channel, plane_chip, plane_die;
	unsigned int ec_mode;
	unsigned int gc_subpage_num, valid_subpage_num, valid_size;
	int valid_state, lpn;
	long long end_transer_time;
	unsigned int i;

	gc_subpage_num = 0;
	for (page = 0; page < ssd->parameter->page_block; page++)
	{
		for (channel = 0; channel < ssd->parameter->channel_number; channel++)
		{
			for (chip = 0; chip < ssd->parameter->chip_channel[channel]; chip++)
			{
				valid_subpage_num = 0; //ͳ�Ƹ�chip��Ҫ��ȡ����page
				ssd->channel_head[channel].chip_head[chip].next_state_predict_time += ssd->parameter->time_characteristics.tR;
				for (die = 0; die < ssd->parameter->die_chip; die++)
				{
					for (plane = 0; plane < ssd->parameter->plane_die; plane++)
					{
						valid_state = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].valid_state;
						if (valid_state > 0)
						{
							lpn = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn;
							// ��Ч���û�����
							if (lpn != -2 && lpn != -1)
							{
								ssd->total_gc_move_page_count++;
								//��ÿ��sub_page����Ǩ��
								valid_subpage_num += size(valid_state);
								gc_subpage_num += size(valid_state);


								ssd->dram->map->map_entry[lpn].state = 0;
								ssd->dram->map->map_entry[lpn].pn = 0;       //ֵ��Ҫ�޸�
								// �����߼���Ӧ����if(valid_sub_page_num > 0)֮����ܽ���д������д�������ڴ˴��������
								insert2buffer(ssd, lpn, valid_state, NULL, req);
								ssd->write_flash_gc_count++;

							}
						}
					}
				}
				if (valid_subpage_num > 0)
				{
					end_transer_time = ssd->channel_head[channel].chip_head[chip].next_state_predict_time + valid_subpage_num * ssd->parameter->subpage_capacity * ssd->parameter->time_characteristics.tRC;
					ssd->channel_head[channel].next_state_predict_time = (end_transer_time > ssd->channel_head[channel].next_state_predict_time) ? ssd->channel_head[channel].next_state_predict_time : end_transer_time;
				}
			}
		}
	}

	for (channel = 0; channel < ssd->parameter->channel_number; channel++)
	{
		ssd->channel_head[channel].current_state = CHANNEL_GC;
		ssd->channel_head[channel].next_state = CHANNEL_IDLE;
		for (chip = 0; chip < ssd->parameter->chip_channel[channel]; chip++)
		{
			for (die = 0; die < ssd->parameter->die_chip; die++)
			{
				for (plane = 0; plane < ssd->parameter->plane_die; plane++)
				{
					erase_operation(ssd, channel, chip, die, plane, block);
				}
			}
			ssd->channel_head[channel].chip_head[chip].next_state_predict_time += ssd->parameter->time_characteristics.tBERS;
		}
	}
	ssd->free_superblock_num++;
	/*
	plane_die = ssd->parameter->plane_die;
	plane_chip = plane_die * ssd->parameter->die_chip;
	plane_channel = plane_chip * ssd->parameter->chip_channel[0];
	for(i = 0; i < BAND_WITDH; i++)
	{
		channel = i / plane_channel;
		chip = (i % plane_channel) / plane_chip;
		die = (i % plane_chip) / plane_die;
		plane = i % plane_die;
		ssd->channel_head[channel].current_state = CHANNEL_GC;
		ssd->channel_head[channel].next_state = CHANNEL_IDLE;
		erase_operation(ssd, channel, chip, die, plane, block);
	}
	for(channel = 0; channel < ssd->parameter->channel_number; channel++)
	{
		for(chip = 0; chip < ssd->parameter->chip_channel[channel]; chip++)
		{
			ssd->channel_head[channel].chip_head[chip].next_state_predict_time += ssd->parameter->time_characteristics.tBERS;
		}
	}
	*/

	return gc_subpage_num;
}

Status erase_operation(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block)
{
	unsigned int i = 0;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num = ssd->parameter->page_block;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num = 0;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page = -1;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].erase_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].gc_flag = 0;
	for (i = 0; i < ssd->parameter->page_block; i++)
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state = PG_SUB;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state = 0;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lpn = -1;
	}
	ssd->erase_count++;
	ssd->channel_head[channel].erase_count++;
	ssd->channel_head[channel].chip_head[chip].erase_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page += ssd->parameter->page_block;

	return SUCCESS;

}