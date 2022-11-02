/*****************************************************************************************************************************
This project was supported by the National Basic Research 973 Program of China under Grant No.2011CB302301
Huazhong University of Science and Technology (HUST)   Wuhan National Laboratory for Optoelectronics

FileName： pagemap.h
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
*断言,当打开文件失败时，输出“open 文件名 error”
*************************************************/
void file_assert(int error, char* s)
{
	if (error == 0) return;
	printf("open %s error\n", s);
	getchar();
	exit(-1);
}

/*****************************************************
*断言,当申请内存空间失败时，输出“malloc 变量名 error”
******************************************************/
void alloc_assert(void* p, char* s)//断言
{
	if (p != NULL) return;
	printf("malloc %s error\n", s);
	getchar();
	exit(-1);
}

/*********************************************************************************
*断言
*A，读到的time_t，device，lsn，size，ope都<0时，输出“trace error:.....”
*B，读到的time_t，device，lsn，size，ope都=0时，输出“probable read a blank line”
**********************************************************************************/
void trace_assert(_int64 time_t, int device, unsigned int lsn, int size, int ope)//断言
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
*函数的功能是根据物理页号ppn查找该物理页所在的channel，chip，die，plane，block，page
*得到的channel，chip，die，plane，block，page放在结构location中并作为返回值
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

	page_plane = ssd->parameter->page_block * ssd->parameter->block_plane;	/*每个plane中的page数*/
	page_die = page_plane * ssd->parameter->plane_die;						/*每个die中的page数*/
	page_chip = page_die * ssd->parameter->die_chip;						/*每个chip中的page数*/
	page_channel = page_chip * ssd->parameter->chip_channel[0];				/*channel[0]中的page数*/

	/*******************************************************************************
	*page_channel是一个channel中page的数目， ppn/page_channel就得到了在哪个channel中
	*用同样的办法可以得到chip，die，plane，block，page
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
*这个函数的功能是根据参数channel，chip，die，plane，block，page，找到该物理页号
*函数的返回值就是这个物理页号
******************************************************************************/
unsigned int find_ppn(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane, unsigned int block, unsigned int page)
{
	unsigned int ppn = 0;
	unsigned int i = 0;
	int page_plane = 0, page_die = 0, page_chip = 0;
	int page_channel[100];                  /*这个数组存放的是每个channel的page数目*/

#ifdef DEBUG
	printf("enter find_psn,channel:%d, chip:%d, die:%d, plane:%d, block:%d, page:%d\n", channel, chip, die, plane, block, page);
#endif

	/*********************************************
	*计算出plane，die，chip，channel中的page的数目
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
	*计算物理页号ppn，ppn是channel，chip，die，plane，block，page中page个数的总和
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
*函数功能是获得一个读子请求的状态
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

	if ((err = fopen_s(&(ssd->tracefile), ssd->tracefilename, "r")) != 0)      /*打开trace文件从中读取请求*/
	{
		printf("the trace file can't open\n");
		return NULL;
	}
	/*检查SSD通道数目是否正确*/
	if (ssd->parameter->channel_number % CHANNEL_NUM != 0)
	{
		printf("the SSD is not suit for the parity strip\n");
		return NULL;
	}

	full_page = ~(0xffffffff << (ssd->parameter->subpage_page));
	/*计算出这个ssd的最大逻辑扇区号*/
	largest_lsn = (unsigned int)((ssd->parameter->chip_num * ssd->parameter->die_chip * ssd->parameter->plane_die * ssd->parameter->block_plane * ssd->parameter->page_block * ssd->parameter->subpage_page) * (1 - ssd->parameter->overprovide));//*(1-ssd->parameter->aged_ratio);

	largest_lsn = largest_lsn / (ssd->parameter->subpage_page * BAND_WITDH) * (ssd->parameter->subpage_page * (BAND_WITDH - MAX_EC_MODLE));//原始数据最大扇区
	ssd->free_superblock_num--; //除了当前申请的suoerblock之外，剩余的空闲个数
	while (fgets(buffer_request, 200, ssd->tracefile))
	{
		sscanf_s(buffer_request, "%I64u %d %d %d %d", &time, &device, &lsn, &size, &ope);
		fl++;
		trace_assert(time, device, lsn, size, ope);                         /*断言，当读到的time，device，lsn，size，ope不合法时就会处理*/
		request_num++;
		/*if(request_num == 326)//335
			printf("-2");*/

		lsn = lsn % largest_lsn;                                    /*防止获得的lsn比最大的lsn还大*/
		add_size = 0;                                                     /*add_size是这个请求已经预处理的大小*/

		if (ope == 1)                                                      /*这里只是读请求的预处理，需要提前将相应位置的信息进行相应修改*/
		{
			while (add_size < size)
			{
				sub_size = ssd->parameter->subpage_page - (lsn % ssd->parameter->subpage_page);	/*计算可能要读取的subpage的个数*/
				if (add_size + sub_size >= size)                             /*只有当一个请求的大小小于一个page的大小时或者是处理一个请求的最后一个page时会出现这种情况*/
				{
					sub_size = size - add_size;
					add_size += sub_size;
				}

				if ((sub_size > ssd->parameter->subpage_page) || (add_size > size))/*当预处理一个子大小时，这个大小大于一个page或是已经处理的大小大于size就报错*/
				{
					printf("pre_process sub_size:%d\n", sub_size);
				}

				/*******************************************************************************************************
				*利用逻辑扇区号lsn计算出逻辑页号lpn
				*判断这个dram中映射表map中在lpn位置的状态
				*A，这个状态==0，表示以前没有写过，现在需要直接将ub_size大小的子页写进去写进去
				*B，这个状态>0，表示，以前有写过，这需要进一步比较状态，因为新写的状态可以与以前的状态有重叠的扇区的地方
				********************************************************************************************************/
				lpn = lsn / ssd->parameter->subpage_page;
				new_page_flag = 0;
				if (ssd->dram->map->map_entry[lpn].state == 0)                 //状态为0的情况
				{
					new_page_flag = 1;
					pre_process_sub_request++;
					if (pre_process_sub_request > largest_lsn / 4 * (1 - ssd->parameter->aged_ratio))
						break_flag = 1;
					//获得利用get_ppn_for_pre_process函数为每个读子请求获取预处理的ppn，当条带写满的时候，则产生校验数据，并将superpage大小一次性写到flash
					pre_process_for_read_request(ssd, lsn, sub_size);

				}
				else if (ssd->dram->map->map_entry[lpn].state > 0)           //状态不为0的情况
				{
					//得到新的状态，并与原来的状态相或得到一个新状态
					map_entry_new = set_entry_state(ssd, lsn, sub_size);
					map_entry_old = ssd->dram->map->map_entry[lpn].state;
					modify = map_entry_new | map_entry_old;

					//修改相关参数及统计信息		
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
				lsn = lsn + sub_size;				//下个子请求的起始位置
				add_size += sub_size;			//已经处理了的add_size大小变化
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
	// 在channel0，chip0，die0，plane0中寻找是否存在活跃超级块
	if (find_superblock_for_pre_read(ssd, 0, 0, 0, 0) == FAILURE)
	{
		printf("the read operation is expand the capacity of SSD\n");
		return 0;
	}
	active_superblock = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].active_block; //获取当前超级块号
	ec_mode = ssd->band_head[active_superblock].ec_modle; //获取将要写的超级块对应的EC模式


	current_superpage = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page + 1; //获取将要写的超级页	
	if (current_superpage >= (int)(ssd->parameter->page_block))
	{
		ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page = 0;
		printf("error! the last write page larger than 64!!\n");
		return ERROR;
	}
	first_parity = BAND_WITDH - (((current_superpage + 1) * ec_mode) % BAND_WITDH); //找出第一个校验的位置
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
	//如果该条带中所有数据页到齐，则计算校验页状态，产生校验页，并一次性将该superpage大小读请求预写到闪存中
	if (size(ssd->strip_bit) == BAND_WITDH)
	{
		//计算校验页状态
		for (i = 0; i < BAND_WITDH; i++)
		{
			parity_state |= ssd->dram->superpage_buffer[i].state;
		}
		//产生校验数据
		for (i = first_parity; i < first_parity + ec_mode; i++)
		{
			ssd->dram->superpage_buffer[i % BAND_WITDH].lpn = -2;
			ssd->dram->superpage_buffer[i % BAND_WITDH].size = size(parity_state);
			ssd->dram->superpage_buffer[i % BAND_WITDH].state = parity_state;
		}
		//将当前superpage大小的读请求预先写到闪存中
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

	// 在channel0，chip0，die0，plane0中寻找是否存在活跃超级块
	if (find_superblock_for_pre_read(ssd, 0, 0, 0, 0) == FAILURE)
	{
		printf("the read operation is expand the capacity of SSD\n");
		return 0;
	}
	active_superblock = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].active_block; //获取当前超级块号
	ec_mode = ssd->band_head[active_superblock].ec_modle; //获取将要写的超级块对应的EC模式


	current_superpage = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page + 1; //获取将要写的超级页	
	if (current_superpage >= (int)(ssd->parameter->page_block))
	{
		ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page = 0;
		printf("error! the last write page larger than 64!!\n");
		return ERROR;
	}
	first_parity = BAND_WITDH - (((current_superpage + 1) * ec_mode) % BAND_WITDH); //找出第一个校验的位置

	//计算校验页状态
	for (i = 0; i < BAND_WITDH; i++)
	{
		parity_state |= ssd->dram->superpage_buffer[i].state;
	}
	//产生校验数据
	for (i = first_parity; i < first_parity + ec_mode; i++)
	{
		ssd->dram->superpage_buffer[i % BAND_WITDH].lpn = -2;
		ssd->dram->superpage_buffer[i % BAND_WITDH].size = size(parity_state);
		ssd->dram->superpage_buffer[i % BAND_WITDH].state = parity_state;
	}
	//将当前superpage大小的读请求预先写到闪存中
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
*函数功能是为预处理函数获取物理页号ppn
*获取页号分为动态获取和静态获取
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
	if (ssd->parameter->allocation_scheme == 0)                           //动态方式下获取ppn
	{
		if (ssd->parameter->dynamic_allocation == 0)                      //表示全动态方式下，也就是channel，chip，die，plane，block等都是动态分配
		{
			channel = ssd->token;
#ifdef USE_EC

			//band_num = ssd->current_band; //无用
			ssd->strip_bits[0] = ssd->strip_bits[0] | (1 << ssd->token);
			ssd->chip_num = ssd->channel_head[channel].token;
			//printf("strip+bit = %d\n",ssd->strip_bit);
			// 获取当前chip、die、plane的位置以供寻找活跃块
			chip = ssd->channel_head[channel].token;
			ssd->channel_head[channel].token = (chip + 1) % ssd->parameter->chip_channel[0];
			die = ssd->channel_head[channel].chip_head[chip].token;
			ssd->channel_head[channel].chip_head[chip].token = (die + 1) % ssd->parameter->die_chip;
			plane = ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token = (plane + 1) % ssd->parameter->plane_die;


			//将令牌设置为下一个通道
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

			if (((1 + ssd->token) / ssd->parameter->channel_number) == (ssd->token / ssd->parameter->channel_number))//如果是同一个条带
			{
				if (p_ch == (1 + ssd->token) % ssd->parameter->channel_number)
					token_flag = 1;
			}
			else//不在同一个条带，而且下一个条带的校验位置在通道 0 ；
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
		else if (ssd->parameter->dynamic_allocation == 1)                 //表示半动态方式，channel静态给出，package，die，plane动态分配                 
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
	else if (ssd->parameter->allocation_scheme == 1)                       //表示静态分配，同时也有0,1,2,3,4,5这6中不同静态分配方式
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

	//根据上述分配方法找到channel，chip，die，plane后，再在这个里面找到active_block，接着获得ppn
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
	ssd->current_band[0] = get_band_id_from_ppn(ssd, ppn); //根据ppn获取band_id
	//printf("band_id=%d data_ch=%d chip=%d die=%d plane=%d  ",ssd->current_band, channel, chip, die, plane);
	if (ssd->current_band[0] == 1) {
		count++;
	}
#endif

	return ppn;
}

#ifdef USE_EC
/***************************************************************************************************
*函数功能是在所给的channel，chip，die，plane里面找到一个active_block然后再在这个block里面找到一个页，
*再利用find_ppn找到ppn。
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
	first_parity = ssd->parameter->channel_number - ssd->band_head[band_num].ec_modle; //该条带中第一个校验块的通道号
/*
#ifdef DEBUG
	printf("enter get_ppn,channel:%d, chip:%d, die:%d, plane:%d\n",channel,chip,die,plane);
#endif
*/
	full_page = ~(0xffffffff << (ssd->parameter->subpage_page));
	lpn = sub->lpn;

	/*************************************************************************************
	*利用函数find_active_block在channel，chip，die，plane找到活跃block
	*并且修改这个channel，chip，die，plane，active_block下的last_write_page和free_page_num
	**************************************************************************************/
	if (find_active_block(ssd, channel, chip, die, plane) == FAILURE)
	{
		printf("get_ppn()\tERROR :there is no free page in channel:%d, chip:%d, die:%d, plane:%d\n", channel, chip, die, plane);
		return NULL;
	}
	active_block = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page + 1; //刚写入的page偏移
	ppn = find_ppn(ssd, channel, chip, die, plane, active_block, page);
	band_num = get_band_id_from_ppn(ssd, ppn);
	//找到是记录中的哪个条带
	for (i = 0; i < 4; i++)
	{
		if (abs((int)band_num - ssd->current_band[i]) < 2)
		{
			break;
		}
	}
	//如果不在记录中，则条带号为新的条带，则说明条带记录有问题，需修改
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
	page = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page; //刚写入的page偏移
	// 修改请求的ppn,location信息
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
		//数据页时，建立lpn2ppn的映射关系
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
		else                                                                            /*这个逻辑页进行了更新，需要将原来的页置为失效*/
		{
			old_ppn = ssd->dram->map->map_entry[lpn].pn;
			location = find_location(ssd, old_ppn);  //获取旧页的位置
			if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn != lpn)
			{
				printf("\nError in get_ppn()\n");
			}
			// 将旧页置为无效
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state = 0;             /*表示某一页失效，同时标记valid和free状态都为0*/
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state = 0;              /*表示某一页失效，同时标记valid和free状态都为0*/
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn = 0;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;

			/*******************************************************************************************
			*该block中全是invalid的页，可以直接删除，就在创建一个可擦除的节点，挂在location下的plane下面
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
		//修改ssd的program_count,free_page等变量
		ssd->program_count++;
		ssd->channel_head[channel].program_count++;
		ssd->channel_head[channel].chip_head[chip].program_count++;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].lpn=lpn;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].valid_state=state;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].free_state=((~state)&full_page);
		*/

		// 若条带中写满数据页，则产生校验请求挂在相应通道上
		if (size(ssd->strip_bits[i]) == first_parity)
		{
			//printf("creat parity sub_request!\n");
			p_state = 0;
			// 获取校验页的状态
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
				//插入校验请求到对应通道的写子请求队列中
				insert_sub_to_queue_head(ssd, parity_sub, p_ch);
				ssd->write_sub_request++;
			}
			//services_2_write(ssd,p_ch,&ssd->channel_head[p_ch].busy_flag,&ssd->change_current_t_flag);
		}
	}
	else
	{	//校验数据

		state = sub->state;
		location = sub->location;
		if (channel == first_parity)
		{
			//pbn_offset为当前通道中的pbn偏移，条带中不同的物理块的pbn = channel_id * block_channel + pbn_offset
			pbn_offset = (ssd->parameter->block_plane * ssd->parameter->plane_die * ssd->parameter->die_chip) * location->chip
				+ (ssd->parameter->block_plane * ssd->parameter->plane_die) * location->die
				+ (ssd->parameter->block_plane) * location->plane
				+ location->block;
			for (i = 0; i < ssd->parameter->channel_number; ++i) {
				ssd->dram->map->pbn[ssd->current_band[0]][i] = i * ssd->band_num + pbn_offset;
			}
		}
	}

	//修改SSD信息
	ssd->program_count++;
	ssd->channel_head[channel].program_count++;
	ssd->channel_head[channel].chip_head[chip].program_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].lpn = lpn;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].valid_state = state;
	//printf("channel = %d\tstate = %d\n", channel, state);
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].free_state = ((~state) & full_page);

	//修改令牌
	//ssd->token = (ssd->token + 1) % ssd->parameter->channel_number;
	//ssd->chip_num = ssd->channel_head[location->channel].token;
	ssd->channel_head[channel].token = (chip + 1) % ssd->parameter->chip_channel[0];
	ssd->channel_head[channel].chip_head[chip].token = (die + 1) % ssd->parameter->die_chip;
	ssd->channel_head[channel].chip_head[chip].die_head[die].token = (plane + 1) % ssd->parameter->plane_die;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[page].free_state == 15)
		printf("\tfree_state error\n");

	if (ssd->parameter->active_write == 0)                                            /*如果没有主动策略，只采用gc_hard_threshold，并且无法中断GC过程*/
	{                                                                               /*如果plane中的free_page的数目少于gc_hard_threshold所设定的阈值就产生gc操作*/
		//channel = 0; //按条带GC，只需查看通道0的空闲页
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

	//如果记录中的第一个条带已写完（数据页+校验页），则将更新条带记录（包括条带号和条带位图）
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
*函数功能是在所给的channel，chip，die，plane里面找到一个active_block然后再在这个block里面找到一个页，
*再利用find_ppn找到ppn。
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
	*利用函数find_active_block在channel，chip，die，plane找到活跃block
	*并且修改这个channel，chip，die，plane，active_block下的last_write_page和free_page_num
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
		else                                                                            /*这个逻辑页进行了更新，需要将原来的页置为失效*/
		{
			ppn = ssd->dram->map->map_entry[lpn].pn;
			location = find_location(ssd, ppn);
			if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn != lpn)
			{
				printf("\nError in get_ppn()\n");
			}

			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state = 0;             /*表示某一页失效，同时标记valid和free状态都为0*/
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state = 0;              /*表示某一页失效，同时标记valid和free状态都为0*/
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn = 0;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;

			/*******************************************************************************************
			*该block中全是invalid的页，可以直接删除，就在创建一个可擦除的节点，挂在location下的plane下面
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
	sub->ppn = ssd->dram->map->map_entry[lpn].pn; 									 /*修改sub子请求的ppn，location等变量*/
#endif 

	ssd->program_count++;                                                           /*修改ssd的program_count,free_page等变量*/
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


	if (ssd->parameter->active_write == 0)                                            /*如果没有主动策略，只采用gc_hard_threshold，并且无法中断GC过程*/
	{                                                                               /*如果plane中的free_page的数目少于gc_hard_threshold所设定的阈值就产生gc操作*/
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
*这个函数功能是在给定的channel、chip、die、plane中为gc操作寻找新的ppn，
*因为在gc操作中需要找到新的物理块存放原来物理块上的数据
*在gc中寻找新物理块的函数，不会引起循环的gc操作
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
* 朱志明 于2011年7月28日修改
*函数的功能就是erase_operation擦除操作，把channel，chip，die，plane下的block擦除掉
*也就是初始化这个block的相关参数，eg：free_page_num=page_block，invalid_page_num=0，last_write_page=-1，erase_count++
*还有这个block下面的每个page的相关参数也要修改。
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
*这个函数的功能是处理INTERLEAVE_TWO_PLANE，INTERLEAVE，TWO_PLANE，NORMAL下的擦除的操作。
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
		((command != INTERLEAVE_TWO_PLANE) && (command != INTERLEAVE) && (command != TWO_PLANE) && (command != NORMAL)))     /*如果没有擦除操作，或者command不对，返回错误*/
	{
		return ERROR;
	}

	/************************************************************************************************************
	*处理擦除操作时，首先要传送擦除命令，这是channel，chip处于传送命令的状态，即CHANNEL_TRANSFER，CHIP_ERASE_BUSY
	*下一状态是CHANNEL_IDLE，CHIP_IDLE。
	*************************************************************************************************************/
	block1 = ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[plane1].erase_node->block;

	ssd->channel_head[channel].current_state = CHANNEL_TRANSFER;
	ssd->channel_head[channel].current_time = ssd->current_time;
	ssd->channel_head[channel].next_state = CHANNEL_IDLE;

	ssd->channel_head[channel].chip_head[chip].current_state = CHIP_ERASE_BUSY;
	ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
	ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;

	/*****************************************************************************************
	*高级命令INTERLEAVE_TWO_PLANE的处理
	*在每一个die中，
	*先找到第一个存在可直接删除块的plane，记录其第一个block的块号block0，再擦除block0
	*然后再将其它plane的第一个可直接删除块的块号block与block0进行对比，如果相同则进行擦除
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
					erase_operation(ssd, channel, chip, die, plane, block);     /*真实的擦除操作的处理*/
					free(direct_erase_node);
					direct_erase_node = NULL;
					ssd->direct_erase_count++;
				}

			}
		}

		ssd->interleave_mplane_erase_count++;                             /*发送了一个interleave two plane erase命令,并计算这个处理的时间，以及下一个状态的时间*/
		ssd->channel_head[channel].next_state_predict_time = ssd->current_time + 18 * ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tWB;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->channel_head[channel].next_state_predict_time - 9 * ssd->parameter->time_characteristics.tWC + ssd->parameter->time_characteristics.tBERS;

	}
	/*****************************************************************
	*高级命令INTERLEAVE的处理
	*对参数指定的die1，只对参数指定的plane1中的可直接删除块进行擦除
	*对其他die的每一个plane的第一个可直接删除块进行擦除
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
	*高级命令TWO_PLANE的处理
	*在参数指定的die1中查找每个plane的第一个可直接删除块，
	*如果其block与block1相同，则对其进行擦除
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
	else if (command == NORMAL)                                             /*普通命令NORMAL的处理*/
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
	{//当参数command不是INTERLEAVE_TWO_PLANE，INTERLEAVE，TWO_PLANE，NORMAL之一的时候可能会出现这种情况
		return FAILURE;
	}
	else
	{
		return SUCCESS;
	}
}



/*******************************************************************************************************************
*GC操作由某个plane的free块少于阈值进行触发，当某个plane被触发时，GC操作占据这个plane所在的die，因为die是一个独立单元。
*对一个die的GC操作，尽量做到四个plane同时erase，利用interleave erase操作。GC操作应该做到可以随时停止（移动数据和擦除
*时不行，但是间隙时间可以停止GC操作），以服务新到达的请求，当请求服务完后，利用请求间隙时间，继续GC操作。可以设置两个
*GC阈值，一个软阈值，一个硬阈值。软阈值表示到达该阈值后，可以开始主动的GC操作，利用间歇时间，GC可以被新到的请求中断；
*当到达硬阈值后，强制性执行GC操作，且此GC操作不能被中断，直到回到硬阈值以上。
*在这个函数里面，找出这个die所有的plane中，有没有可以直接删除的block，要是有的话，利用interleave two plane命令，删除
*这些block，否则有多少plane有这种直接删除的block就同时删除，不行的话，最差就是单独这个plane进行删除，连这也不满足的话，
*直接跳出，到gc_parallelism函数进行进一步GC操作。该函数寻找全部为invalid的块，直接删除，找到可直接删除的返回1，没有找
*到返回-1。
*********************************************************************************************************************/
int gc_direct_erase(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane)
{
	unsigned int lv_die = 0, lv_plane = 0;                                                           /*为避免重名而使用的局部变量 Local variables*/
	unsigned int interleaver_flag = FALSE, muilt_plane_flag = FALSE;
	unsigned int normal_erase_flag = TRUE;

	struct direct_erase* direct_erase_node1 = NULL;
	struct direct_erase* direct_erase_node2 = NULL;

	direct_erase_node1 = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
	if (direct_erase_node1 == NULL)
	{
		return FAILURE;
	}
	//找道plane中第一个可以直接擦除的block，不满足的从erase_node链表中删除
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
	*Two_plane:对两个plane中相对位置的页进行并行读写
	*当能处理TWOPLANE高级命令时，就在相应的channel，chip，die中两个不同的plane找到可以执行TWOPLANE操作的block
	*并置muilt_plane_flag为TRUE
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
	*Interleave:同一块芯片不同dies上同时执行多个pages的读/写/擦除
	*当能处理INTERLEAVE高级命令时，就在相应的channel，chip找到可以执行INTERLEAVE的两个block
	*并置interleaver_flag为TRUE
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
	*A，如果既可以执行twoplane的两个block又有可以执行interleaver的两个block，那么就执行INTERLEAVE_TWO_PLANE的高级命令擦除操作
	*B，如果只有能执行interleaver的两个block，那么就执行INTERLEAVE高级命令的擦除操作
	*C，如果只有能执行TWO_PLANE的两个block，那么就执行TWO_PLANE高级命令的擦除操作
	*D，没有上述这些情况，那么就只能够执行普通的擦除操作了
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

	if ((normal_erase_flag == TRUE))                              /*不是每个plane都有可以直接删除的block，只对当前plane进行普通的erase操作，或者只能执行普通命令*/
	{
		if (erase_planes(ssd, channel, chip, die, plane, NORMAL) == SUCCESS)
		{
			return SUCCESS;
		}
		else
		{
			return FAILURE;                                     /*目标的plane没有可以直接删除的block，需要寻找目标擦除块后在实施擦除操作*/
		}
	}
	return SUCCESS;
}

/*****************************************************************
*功能:将给定location所指向的物理页，移动到同一plane的不同block中
******************************************************************/
Status move_page(struct ssd_info* ssd, struct local* location, unsigned int* transfer_size)
{
	struct local* new_location = NULL;
	unsigned int free_state = 0, valid_state = 0;
	unsigned int lpn = 0, old_ppn = 0, ppn = 0;

	lpn = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn;
	valid_state = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state;
	free_state = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state;
	old_ppn = find_ppn(ssd, location->channel, location->chip, location->die, location->plane, location->block, location->page);      /*记录这个有效移动页的ppn，对比map或者额外映射关系中的ppn，进行删除和添加操作*/
	ppn = get_ppn_for_gc(ssd, location->channel, location->chip, location->die, location->plane);                /*找出来的ppn一定是在发生gc操作的plane中,才能使用copyback操作，为gc操作获取ppn*/

	new_location = find_location(ssd, ppn);                                                                   /*根据新获得的ppn获取new_location*/

	/*********************************************************************************************************
	*Copyback：同一个plane中一个page复制到另一个page而不占用I/O总线。但要求该命令的源page和目标page同奇同偶
	**********************************************************************************************************/
	if ((ssd->parameter->advanced_commands & AD_COPYBACK) == AD_COPYBACK)
	{
		if (ssd->parameter->greed_CB_ad == 1)                                                                /*贪婪地使用高级命令*/
		{
			ssd->copy_back_count++;
			ssd->gc_copy_back++;
			while (old_ppn % 2 != ppn % 2)
			{//个人观点:因为page的分配是按顺序的，所以该循环只会执行一次，可以改成if
				ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].free_state = 0;
				ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].lpn = 0;
				ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].page_head[new_location->page].valid_state = 0;
				ssd->channel_head[new_location->channel].chip_head[new_location->chip].die_head[new_location->die].plane_head[new_location->plane].blk_head[new_location->block].invalid_page_num++;

				free(new_location);
				new_location = NULL;

				ppn = get_ppn_for_gc(ssd, location->channel, location->chip, location->die, location->plane);    /*找出来的ppn一定是在发生gc操作的plane中，并且满足奇偶地址限制,才能使用copyback操作*/
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

	if (old_ppn == ssd->dram->map->map_entry[lpn].pn)                                                     /*修改映射表*/
	{
		ssd->dram->map->map_entry[lpn].pn = ppn;
	}

	free(new_location);
	new_location = NULL;

	return SUCCESS;
}

/*******************************************************************************************************************************************
*目标的plane没有可以直接删除的block，需要寻找目标擦除块后在实施擦除操作，用在不能中断的gc操作中，成功删除一个块，返回1，没有删除一个块返回-1
*在这个函数中，不用考虑目标channel,die是否是空闲的,擦除invalid_page_num最多的block
********************************************************************************************************************************************/
int uninterrupt_gc(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane)
{
	unsigned int i = 0, invalid_page = 0;
	unsigned int block, active_block, transfer_size, free_page, page_move_count = 0;                           /*记录失效页最多的块号*/
	struct local* location = NULL;
	unsigned int total_invalid_page_num = 0;

	if (find_active_block(ssd, channel, chip, die, plane) != SUCCESS)                                           /*获取活跃块*/
	{
		printf("\n\n Error in uninterrupt_gc().\n");
		return ERROR;
	}
	active_block = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;

	invalid_page = 0;
	transfer_size = 0;
	block = -1;
	for (i = 0; i < ssd->parameter->block_plane; i++)                                                           /*查找最多invalid_page的块号，以及最大的invalid_page_num*/
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
	for (i = 0; i < ssd->parameter->page_block; i++)		                                                     /*逐个检查每个page，如果有有效数据的page需要移动到其他地方存储*/
	{
		if ((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state & PG_SUB) == 0x0000000f)
		{
			free_page++;
		}
		if (free_page != 0)
		{
			printf("\ntoo much free page. \t %d\t .%d\t%d\t%d\t%d\t\n", free_page, channel, chip, die, plane);
		}
		if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state > 0) /*该页是有效页，需要copyback操作*/
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
			move_page(ssd, location, &transfer_size);                                                   /*真实的move_page操作*/
			page_move_count++;

			free(location);
			location = NULL;
		}
	}
	ssd->total_gc_move_page_count = ssd->total_gc_move_page_count + page_move_count;
	erase_operation(ssd, channel, chip, die, plane, block);	                                              /*执行完move_page操作后，就立即执行block的擦除操作*/

	ssd->channel_head[channel].current_state = CHANNEL_GC;
	ssd->channel_head[channel].current_time = ssd->current_time;
	ssd->channel_head[channel].next_state = CHANNEL_IDLE;
	ssd->channel_head[channel].chip_head[chip].current_state = CHIP_ERASE_BUSY;
	ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
	ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;

	/***************************************************************
	*在可执行COPYBACK高级命令与不可执行COPYBACK高级命令这两种情况下，
	*channel下个状态时间的计算，以及chip下个状态时间的计算
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
*目标的plane没有可以直接删除的block，需要寻找目标擦除块后在实施擦除操作，用在可以中断的gc操作中，成功删除一个块，返回1，没有删除一个块返回-1
*在这个函数中，不用考虑目标channel,die是否是空闲的
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

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[gc_node->block].invalid_page_num != ssd->parameter->page_block)     /*还需要执行copyback操作*/
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

		return 1;                                                                      /*该gc操作完成，返回1，可以将channel上的gc请求节点删除*/
	}

	printf("there is a problem in interrupt_gc\n");
	return 1;
}

/*************************************************************
*功能:将gc_node从channel中的gc链(链头是gc_command)删除
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
*这个函数的功能是处理channel的每个gc操作
****************************************/
Status gc_for_channel(struct ssd_info* ssd, unsigned int channel)
{
	int flag_direct_erase = 1, flag_gc = 1, flag_invoke_gc = 1;
	unsigned int chip, die, plane, flag_priority = 0;
	unsigned int current_state = 0, next_state = 0;
	long long next_state_predict_time = 0;
	struct gc_operation* gc_node = NULL, * gc_p = NULL;

	/*******************************************************************************************
	*查找每一个gc_node，获取gc_node所在的chip的当前状态，下个状态，下个状态的预计时间
	*如果当前状态是空闲，或是下个状态是空闲而下个状态的预计时间小于当前时间，并且是不可中断的gc
	*那么就flag_priority令为1，否则为0
	********************************************************************************************/
	gc_node = ssd->channel_head[channel].gc_command;
	while (gc_node != NULL)
	{
		current_state = ssd->channel_head[channel].chip_head[gc_node->chip].current_state;
		next_state = ssd->channel_head[channel].chip_head[gc_node->chip].next_state;
		next_state_predict_time = ssd->channel_head[channel].chip_head[gc_node->chip].next_state_predict_time;
		if ((current_state == CHIP_IDLE) || ((next_state == CHIP_IDLE) && (next_state_predict_time <= ssd->current_time)))
		{
			if (gc_node->priority == GC_UNINTERRUPT)                                     /*这个gc请求是不可中断的，优先服务这个gc操作*/
			{
				flag_priority = 1;
				break;
			}
		}
		gc_node = gc_node->next_node;
	}
	if (flag_priority != 1)                                                              /*没有找到不可中断的gc请求，首先执行队首的gc请求*/
	{
		gc_node = ssd->channel_head[channel].gc_command;
		while (gc_node != NULL)
		{
			current_state = ssd->channel_head[channel].chip_head[gc_node->chip].current_state;
			next_state = ssd->channel_head[channel].chip_head[gc_node->chip].next_state;
			next_state_predict_time = ssd->channel_head[channel].chip_head[gc_node->chip].next_state_predict_time;
			/******************************************************************
			*需要gc操作的目标chip是空闲的，
			*或chip的下一个状态是空闲的且下一状态的预计时间小于ssd的当前时间，
			*才可以进行gc操作
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
	*不可中断的gc请求，
	*优先调用gc_direct_erase擦除给定的channel,chip,die,plane下可以直接删除的block，
	*如果没有可以直接擦除的block则调用uninterrupt_gc擦除给定的channel,chip,die,plane下invalid_page_num最多的block
	*在完成擦除操作之后，需要将当前gc请求节点移出请求队列
	**************************************************************************************************************/
	if (gc_node->priority == GC_UNINTERRUPT)
	{
		flag_direct_erase = gc_direct_erase(ssd, channel, chip, die, plane);
		if (flag_direct_erase != SUCCESS)
		{
			flag_gc = uninterrupt_gc(ssd, channel, chip, die, plane);                         /*当一个完整的gc操作完成时（已经擦除一个块，回收了一定数量的flash空间），返回1，将channel上相应的gc操作请求节点删除*/
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
	*可中断的gc请求，
	*需要首先确认该channel上没有子请求在这个时刻需要使用这个channel，没有的话，在执行gc操作，有的话，不执行gc操作
	*优先调用gc_direct_erase擦除给定的channel,chip,die,plane下可以直接删除的block，
	*如果没有可以直接擦除的block则调用uninterrupt_gc擦除给定的channel,chip,die,plane下invalid_page_num最多的block
	*在完成擦除操作之后，需要将当前gc请求节点移出请求队列
	**************************************************************************************************************/
	else
	{
		flag_invoke_gc = decide_gc_invoke(ssd, channel);                                  /*判断是否有子请求需要channel，如果有子请求需要这个channel，那么这个gc操作就被中断了*/

		if (flag_invoke_gc == 1)
		{
			flag_direct_erase = gc_direct_erase(ssd, channel, chip, die, plane);
			if (flag_direct_erase == -1)
			{
				flag_gc = interrupt_gc(ssd, channel, chip, die, plane, gc_node);             /*当一个完整的gc操作完成时（已经擦除一个块，回收了一定数量的flash空间），返回1，将channel上相应的gc操作请求节点删除*/
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
*flag用来标记gc函数是在ssd整个都是idle的情况下被调用的（1），还是确定了channel，chip，die，plane被调用（0）
*进入gc函数，需要判断是否是不可中断的gc操作，如果是，需要将一整块目标block完全擦除后才算完成；如果是可中断的，
*在进行GC操作前，需要判断该channel，die是否有子请求在等待操作，如果没有则开始一步一步的操作，找到目标
*块后，一次执行一个copyback操作，跳出gc函数，待时间向前推进后，再做下一个copyback或者erase操作
*进入gc函数不一定需要进行gc操作，需要进行一定的判断，当处于硬阈值以下时，必须进行gc操作；当处于软阈值以下时，
*需要判断，看这个channel上是否有子请求在等待(有写子请求等待就不行，gc的目标die处于busy状态也不行)，如果
*有就不执行gc，跳出，否则可以执行一步操作
************************************************************************************************************/
unsigned int gc(struct ssd_info* ssd, unsigned int channel, unsigned int flag)
{
	unsigned int i;
	int flag_direct_erase = 1, flag_gc = 1, flag_invoke_gc = 1;
	unsigned int flag_priority = 0;
	struct gc_operation* gc_node = NULL, * gc_p = NULL;

	if (flag == 1)	/*整个ssd都是IDEL的情况*/
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
	else		/*只需针对某个特定的channel，chip，die进行gc请求的操作(只需对目标die进行判定，看是不是idle）*/
	{
		if ((ssd->parameter->allocation_scheme == 1) || ((ssd->parameter->allocation_scheme == 0) && (ssd->parameter->dynamic_allocation == 1)))
		{
			if ((ssd->channel_head[channel].subs_r_head != NULL) || (ssd->channel_head[channel].subs_w_head != NULL))    /*队列上有请求，先服务请求*/
			{
				return 0;
			}
		}

		gc_for_channel(ssd, channel);
		return SUCCESS;
	}
}



/**********************************************************
*判断channel是否被子请求占用，或是否有子请求等待
*如果没有，返回1就可以发送gc操作
*如果有返回0，就不能执行gc操作，gc操作被中断
***********************************************************/
int decide_gc_invoke(struct ssd_info* ssd, unsigned int channel)
{
	struct sub_request* sub;
	struct local* location;

	if ((ssd->channel_head[channel].subs_r_head == NULL) && (ssd->channel_head[channel].subs_w_head == NULL))    /*这里查找读写子请求是否需要占用这个channel，不用的话才能执行GC操作*/
	{
		return 1;                                                                        /*表示当前时间这个channel没有子请求需要占用channel*/
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
				*这个读请求是处于等待状态，
				*如果他的目标chip处于idle，或chip的下一状态是idle且下一状态的预计时间小于系统当前时间
				*则不能执行gc操作，返回0
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
				*这个读请求的下一状态是数据传输状态，
				*如果他的目标chip处于的下一状态的预计时间小于系统当前时间
				*则不能执行gc操作，返回0
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


// 在获取新的superblock时，检测空闲superblock的剩余个数是否满足GC要求，若满足则先GC
int gc_for_superblock(struct ssd_info* ssd, struct request* req)
{
	int gc_superblock_number;  //GC的superblock号
	unsigned int gc_subpage_num; //GC中迁移有效子页的个数
	//贪婪寻找无效页最多的superblock
	gc_superblock_number = find_superblock_for_gc(ssd);
	if (gc_superblock_number == -1)
	{
		printf("There is no invalid page in every superblock!\n");
		getchar();
		return FAILURE;
	}
	ouput_bad_block(ssd);
	//处理该superblock中的有效数据，（读，写）
	gc_subpage_num = process_for_gc(ssd, req, gc_superblock_number);

	ssd->band_head[gc_superblock_number].pe_cycle++;

	////当P/E周期达到4/5的上限阈值时，将EC模式（可靠性）调到最大
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

	//统计第i个superblock（active_superblock除外)的无效页数目
	largest_superblock_invalid_page = 0;
	for (i = 0; i < ssd->parameter->block_plane; i++)
	{
		if (ssd->band_head[i].advance_gc_flag == 1) {
			//优先gc出现高磨损块的条带
			block = i;
			break;
		}
		superblock_invalid_page = 0;
		active_superblock = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].active_block;
		//统计第i个superblock的无效页数目
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
		//找出最多无效页的superblock块号
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
				valid_subpage_num = 0; //统计该chip需要读取多少page
				ssd->channel_head[channel].chip_head[chip].next_state_predict_time += ssd->parameter->time_characteristics.tR;
				for (die = 0; die < ssd->parameter->die_chip; die++)
				{
					for (plane = 0; plane < ssd->parameter->plane_die; plane++)
					{
						valid_state = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].valid_state;
						if (valid_state > 0)
						{
							lpn = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn;
							// 有效的用户数据
							if (lpn != -2 && lpn != -1)
							{
								ssd->total_gc_move_page_count++;
								//将每个sub_page进行迁移
								valid_subpage_num += size(valid_state);
								gc_subpage_num += size(valid_state);


								ssd->dram->map->map_entry[lpn].state = 0;
								ssd->dram->map->map_entry[lpn].pn = 0;       //值需要修改
								// 按照逻辑，应该在if(valid_sub_page_num > 0)之后才能进行写（读后写），放在此处不会产生
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