
#include <stdlib.h>
#include <stdio.h>
#include "ssd.h"
#include "initialize.h"
#include "pagemap.h"
#include "flash.h"
#include "avlTree.h"
#include "states.h"

int copy_sub_request( struct sub_request*sub1,struct sub_request *tmp)
{
#ifdef USE_WHITE_PARITY
	tmp->begin_time = sub1->begin_time;
	tmp->complete_time = sub1->complete_time;
	tmp->current_state =  sub1->current_state;
	tmp->current_time = sub1->current_time;
	tmp->location = sub1->location;
	tmp->lpn = sub1->lpn;
	//tmp->next_node 
	tmp->next_state = sub1->next_state;
	tmp->next_state_predict_time = sub1->next_state_predict_time;
	tmp->next_subs = sub1->next_subs;
	tmp->operation = sub1->operation;
	tmp->ppn = sub1->ppn;
	tmp->read_old = sub1->read_old;
	tmp->size = sub1 ->size;
	tmp->state = sub1->state;
	tmp->type = sub1->type;
	tmp->update = sub1->update; 
#endif 
	return 1;

}

int swap_sub_request( struct sub_request*sub1,struct sub_request *sub2)
{
	struct sub_request *tmp = NULL;
	tmp = (struct sub_request*)malloc(sizeof(struct sub_request));
	alloc_assert(tmp,"tmp");
	memset(tmp,0,sizeof(struct sub_request));

	copy_sub_request(sub1,tmp);
	copy_sub_request(sub2,sub1);
	copy_sub_request(tmp,sub2);
	free(tmp);
	tmp = NULL;
	return SUCCESS;
}

int white_parity_gc(struct ssd_info *ssd)
{
	unsigned int i;
	struct gc_operation *gc_node=NULL,*gc_p=NULL;
	unsigned int chip,die,plane,active_bloclk=-1,block ;
	unsigned int channel = 0,invalid_page,band_invalid_page,largest_band_invalid_page;
	unsigned int free_page;
	int lpn;
	unsigned int state ;
	struct sub_request*sub,*sub_w=NULL;
	struct direct_erase *new_direct_erase=NULL,*direct_erase_node=NULL;
	unsigned int flag_direct_erase =0;

		if(ssd->channel_head[0].gc_command != NULL)
		{
			gc_node = ssd->channel_head[0].gc_command;//每擦除一个块   查询是否有相应的gc请求，有就删除。

		}

	if(gc_node == NULL)
		return FAILURE;
	chip=gc_node->chip;
	die=gc_node->die;
	plane=gc_node->plane;


	for(i=0;i<ssd->parameter->channel_number;i++)
	{
		if((ssd->channel_head[i].current_state==CHANNEL_IDLE)||(ssd->channel_head[i].next_state==CHANNEL_IDLE&&ssd->channel_head[i].next_state_predict_time<=ssd->current_time))
			if((ssd->channel_head[i].chip_head[chip].current_state==CHIP_IDLE)||((ssd->channel_head[i].chip_head[chip].next_state==CHIP_IDLE)&&(ssd->channel_head[i].chip_head[chip].next_state_predict_time<=ssd->current_time))){
				flag_direct_erase=gc_direct_erase(ssd,i,chip,die,plane);
				//printf("channel = %d\tgc_direct_erase\n", i);
			}
	}

#ifdef USE_WHITE_PARITY
	if(gc_node->deal == 1)
		return FAILURE;
#endif

	active_bloclk = -1;
	block = -1 ;
	invalid_page = 0;
	largest_band_invalid_page = 0;
	for(i = 0;i<ssd->parameter->block_plane;i++)
	{
		band_invalid_page =0;
		for(channel=0;channel<ssd->parameter->channel_number;channel++)
		{
			active_bloclk = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block ;
			if(i!=active_bloclk)
				band_invalid_page = band_invalid_page + ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[i].invalid_page_num;
		}
		if(band_invalid_page > largest_band_invalid_page)
		{
			block = i ;
			largest_band_invalid_page = band_invalid_page;
		}
	}
	if(block ==-1 )
		return FAILURE;

	//printf("block = %d\tlagest_band_invalid_page = %d\n", block, largest_band_invalid_page);
#ifdef USE_WHITE_PARITY
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
	gc_node->deal = 1;
	gc_node->block = block;
	
	// 将该条带中的有效页进行迁移（产生读请求），写请求在读取之后产生
	for(i = 0;i<ssd->parameter->page_block;i++)
	{
		
		for(channel = 0;channel < ssd->parameter->channel_number;channel++)
		{
			free_page = 0;
			if((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state&PG_SUB)==0x0000000f)
				free_page++;
			if(free_page!=0)
			{
				printf("\ntoo much free page. \t %d\t .%d\t%d\t%d\t%d\t\n",free_page,channel,chip,die,plane);
			}
			if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lpn != -2)//该页不是校验页；
			{
				if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state>0) //该页数据有效；需要将该页进行有效数据迁移；
				{
					lpn = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lpn;
					state = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state;
					if(state ==0 )
						printf(" gc read  state == 0\n");
					sub = creat_sub_request(ssd,lpn,size(state),state,NULL,READ);
					
					if(sub->state == 0)
						printf("gc read sub->state ==0\n");
					sub->type = GC_SUB;//该请求的类型是GC时产生的请求；
					//通过判断该块中无效页的数量是否为0 来判断是否执行擦除操作。
					//sub_w = creat_sub_request(ssd,lpn,size(state),state,NULL,WRITE);//同时将读请求和写请求挂到ssd请求队列上。读请求优先处理。但是可能在不同的通道上，导致写请求先处理了；
					//读完该数据之后，产生些请求，修改原数据页相关信息。
					//sub_w->type = GC_SUB;

				}
			}
			else//该页为校验页所在位置，修改该页相关信息，模拟擦除该页；
			{
				ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].lpn = 0;
				ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].free_state = 0;
				ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[i].valid_state = 0;
				ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num++;
			}
		}
	}

	for(channel = 0;channel<ssd->parameter->channel_number;channel++)
	{
		if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num == ssd->parameter->page_block)
		{
			new_direct_erase=(struct direct_erase *)malloc(sizeof(struct direct_erase));
			alloc_assert(new_direct_erase,"new_direct_erase");
			memset(new_direct_erase,0, sizeof(struct direct_erase));

			if((channel==3)&&(chip==1)&&(die==3)&&(plane==0)&&(block==22))
				{
					channel = 3;
					i=0;
					while (i<ssd->parameter->page_block)
					{
						if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[41].valid_state !=0)
							i = i;
							i++;
					}
			}

			new_direct_erase->block=block;
			new_direct_erase->next_node=NULL;
			direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
			if (direct_erase_node==NULL)
			{
				ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node=new_direct_erase;
			} 
			else
			{
				new_direct_erase->next_node=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
				ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node=new_direct_erase;
			}/*erase_operation(ssd,channel,chip,die,plane,block);*/
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].can_erase_block++;
			
			//printf("channel = %d have new_direct_erase\n", channel);
		}
	}

	delete_gc_node(ssd,0,gc_node);
#endif 
	return SUCCESS;
}


int insert_sub_to_queue_head(struct ssd_info *ssd,struct sub_request *parity_sub,unsigned int channel)
{
	if(ssd->channel_head[channel].subs_w_head == NULL)
	{
		ssd->channel_head[channel].subs_w_head = parity_sub;
		ssd->channel_head[channel].subs_w_tail = parity_sub;
	}
	else
	{
		parity_sub->next_node = ssd->channel_head[channel].subs_w_head;
		ssd->channel_head[channel].subs_w_head = parity_sub;
	}
	return 0;
}
#ifdef USE_EC
void pre_write_white_parity(struct ssd_info* ssd)
{
	unsigned int band_num=0;
	unsigned int p_ch = 0;//校验位的通道号
	unsigned int ppn = 0,pbn_offset=0;
	//int page_channel[100];
	unsigned int i;
	unsigned int state = 0;
	unsigned int page_chip = 0,full_page;
	unsigned int band_chip=0,band_die=0,band_plane=0,band_block=0,band_channel=0,band_page=0;
	struct local* location = NULL;
	unsigned int first_parity_ch = 0;
	int map_flag = 0; //一个条带的信息只需映射一次，因此用map_flag来避免重复映射
	unsigned int page;

	first_parity_ch = ssd->parameter->channel_number - ssd->band_head[ssd->current_band[0]].ec_modle; // 该条带中第一个校验位的个数
	p_ch = ssd->token;
	if(p_ch == first_parity_ch){
		// band_xxx表示每个xxx中包含的条带的个数
		band_plane = ssd->parameter->block_plane; 
		band_die = band_plane * ssd->parameter->plane_die;
		band_chip = band_die * ssd->parameter->die_chip;
		band_channel = ssd->band_num;
		

		band_channel = ssd->current_band[0];
		band_chip = band_channel/ssd->parameter->chip_channel[0];
		band_die = band_chip/ssd->parameter->die_chip;
		band_plane = band_die/ssd->parameter->plane_die;
		band_block = band_plane/ssd->parameter->block_plane;
		band_page = band_block/ssd->parameter->page_block;/**/

		//依次写入校验页
		for(; p_ch < ssd->parameter->channel_number; ++p_ch){	
			//printf("p_ch=%d  ",p_ch);

			full_page=~(0xffffffff<<(ssd->parameter->subpage_page));

			location = (struct local*)malloc(sizeof(struct local));
			alloc_assert(location,"location");
			memset(location,0, sizeof(struct local));
/*
			//该部分是采用顺序块写入的方式
			// 校验页的位置
			location->channel = p_ch;
			location->chip = ssd->current_band / band_chip;
			location->die = (ssd->current_band % band_chip) / band_die;
			location->plane = (ssd->current_band % band_chip % band_die) / band_plane;

*/
			ssd->strip_bits[0] = ssd->strip_bits[0]|(1<<ssd->token);
			
			location->channel = ssd->token;
			//将令牌设置为下一个通道
			ssd->token = (ssd->token + 1) % ssd->parameter->channel_number;
			location->chip=ssd->channel_head[location->channel].token;
			ssd->channel_head[location->channel].token=(location->chip+1)%ssd->parameter->chip_channel[0];
			location->die=ssd->channel_head[location->channel].chip_head[location->chip].token;
			ssd->channel_head[location->channel].chip_head[location->chip].token=(location->die+1)%ssd->parameter->die_chip;
			location->plane=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].token;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].token=(location->plane+1)%ssd->parameter->plane_die;

/*
			//printf("strip_bit = %d\n", ssd->strip_bit);
			location->channel =  p_ch;
			location->chip = band_channel%(ssd->parameter->chip_channel[0]);
			location->die = band_chip%ssd->parameter->die_chip;
			location->plane = band_die%ssd->parameter->plane_die;
			*/
			if(!find_active_block(ssd,location->channel,location->chip,location->die,location->plane)){
				printf("There is no active block!\n");
				return;
			}
			// 找到plane中的一个活动块
			location->block = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].active_block;
			// 写入该校验页，并获取写入的ppn位置
			write_page(ssd,location->channel,location->chip,location->die,location->plane,location->block,&ppn); 
			page = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].last_write_page;
			//printf("channel = %d\tchip = %d\tdie = %d\tplane = %d\tactive_block = %d\tpage = %d\n", location->channel, location->chip, location->die, location->plane, location->block, page);
			//free(location);
			location = NULL;

			// 获取ppn的location信息
			location = find_location(ssd,ppn); 
			// 如果这是block中的第一个page，那么将这个条带的相关信息添加到校验映射表中。
			if(location->page == 0 && map_flag == 0){
				map_flag = 1; 
				//pbn_offset为当前通道中的pbn偏移，条带中不同的物理块的pbn = channel_id * block_channel + pbn_offset
				pbn_offset = (ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip)*location->chip
						   + (ssd->parameter->block_plane*ssd->parameter->plane_die)*location->die
					       + (ssd->parameter->block_plane)*location->plane 
					       + location->block;
				for(i = 0; i < ssd->parameter->channel_number; ++i){
					ssd->dram->map->pbn[ssd->current_band[0]][i] = i * ssd->band_num + pbn_offset;
				}
			}
			/*
			// 设置该通道下一个将要写的位置，即除了channel之外修改chip、die和plane的位置。
			// 条带为具有跨通道相同ID的块组成，则当当前活动块为plane最后一个块写满时时，则将plane加1，同理设置die和chip的token
			//当该活跃块写满时，检查设置plane，die和chip的位置
			if(location->page == ssd->parameter->page_block - 1){
				if((location->block == ssd->parameter->block_plane - 1) && (location->plane == ssd->parameter->plane_die - 1) && (location->die == ssd->parameter->die_chip - 1))
					ssd->channel_head[location->channel].token = (location->chip+1)%ssd->parameter->chip_channel[0];
				if((location->block == ssd->parameter->block_plane - 1) && (location->plane == ssd->parameter->plane_die - 1))
					ssd->channel_head[location->channel].chip_head[location->chip].token = (location->die+1)%ssd->parameter->die_chip;
				if(location->block == ssd->parameter->block_plane - 1)
					ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].token = (location->plane+1)%ssd->parameter->plane_die;
			}
			*/
/*
			//将令牌设置为下一个通道
			ssd->token = (ssd->token + 1) % ssd->parameter->channel_number;

			//chip/die/plane令牌token加一
			ssd->token = (ssd->token + 1) % ssd->parameter->channel_number;
			//ssd->chip_num = ssd->channel_head[location->channel].token;
			ssd->channel_head[location->channel].token=(location->chip+1)%ssd->parameter->chip_channel[0];
			ssd->channel_head[location->channel].chip_head[location->chip].token=(location->die+1)%ssd->parameter->die_chip;
			//plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].token=(location->plane+1)%ssd->parameter->plane_die;
*/
			// 计算该页的有效子页的状态，并将该状态作为校验页的state
			for(i = 0; i < ssd->parameter->channel_number - ssd->band_head[ssd->current_band[0]].ec_modle; ++i){
				state = state|ssd->channel_head[i].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state;
			}
			/*
			if(state == 0)
				printf("pre state == 0\n");
			*/
			// 修改SSD的信息
			/*ssd->program_count++;
			ssd->channel_head[location->channel].program_count++;
			ssd->channel_head[location->channel].chip_head[location->chip].program_count++;
			*/
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn = -2;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=state;
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=((~state)&full_page);

			free(location);
			location=NULL;

			
		}
		//ssd->token = 0;
		//printf("\n");
		ssd->strip_bits[0] = 0;
	}
}
	
#else
void pre_write_white_parity(struct ssd_info* ssd)
{
	unsigned int band_num=0;
	unsigned int p_ch = 0;//校验位的通道号
	unsigned int ppn = 0,pbn=0;
	//int page_channel[100];
	unsigned int i = 0;
	unsigned int state = 0;
	unsigned int page_chip = 0,full_page;
	unsigned int band_chip=0,band_die=0,band_plane=0,band_block=0,band_channel=0,band_page=0;
	struct local* location = NULL;
	//unsigned int chip=0,die=0,plane=0,block=0;
	if(ssd->page_num !=0)
		band_num = (ssd->page_num-1)/PARITY_SIZE;
	else
		band_num = ssd->parameter->page_block-1;

	p_ch = PARITY_SIZE - band_num%(PARITY_SIZE+1);
	/*如果当前活动通道为一个条带中的最后一个page或者是在校验位为最后一个page而活动通道在倒数第二个page、，，，but ssd->token表示的是下一次的活动通道
	if((((ssd->token+1)%(PARITY_SIZE+1) == PARITY_SIZE)&&(ssd->token%(PARITY_SIZE+1) == p_ch))||(ssd->token%(PARITY_SIZE+1) == PARITY_SIZE))*/
	//如果这个条带已经写入了PARITY_SIZE个数据量，那么写入校验位。
	if(ssd->page_num % PARITY_SIZE == 0)
	{
		/*if(band_num == 63)
			printf("66");*/

		
		if(size(ssd->strip_bit) != PARITY_SIZE)
			printf("strip failed\n");
		else
			ssd->strip_bit = 0;

		/*page_chip = ssd->parameter->die_chip* ssd->parameter->plane_die*ssd->parameter->block_plane*ssd->parameter->page_block;
		while(i<ssd->parameter->channel_number)
		{
			page_channel[i]=ssd->parameter->chip_channel[i]*page_chip;
		    i++;
		}
		i=0;
		while(i<p_ch)
		{
			ppn = ppn + page_channel[i];
			i++;
		}

		ppn = ppn + band_num;
		//start wirte the parity page
		location = find_location(ssd,ppn);*/

		full_page=~(0xffffffff<<(ssd->parameter->subpage_page));

		band_channel = band_num;///ssd->parameter->channel_number;
		band_chip = band_channel/ssd->parameter->chip_channel[0];
		band_die = band_chip/ssd->parameter->die_chip;
		band_plane = band_die/ssd->parameter->plane_die;
		band_block = band_plane/ssd->parameter->block_plane;
		band_page = band_block/ssd->parameter->page_block;/**/

		location = (struct local*)malloc(sizeof(struct local));
		alloc_assert(location,"location");
		memset(location,0, sizeof(struct local));
		
		location->channel =  p_ch;
		location->chip = band_channel%(ssd->parameter->chip_channel[0]);
		location->die = band_chip%ssd->parameter->die_chip;
		location->plane = band_die%ssd->parameter->plane_die;
		find_active_block(ssd,location->channel,location->chip,location->die,location->plane);
		location->block = ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].active_block;
		
		write_page(ssd,location->channel,location->chip,location->die,location->plane,location->block,&ppn);
		free(location);
		location = NULL;
		
		location = find_location(ssd,ppn);
		//如果这是block中的第一个page  那么将这个条带的相关信息添加到校验映射表中。
		if(location->page == 0)
		{
			i = 0;
			while (i<PARITY_SIZE+1)
			{
				pbn = //(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_channel[0])*location->channel
					(ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip)*location->chip
					+ (ssd->parameter->block_plane*ssd->parameter->plane_die)*location->die
					+ (ssd->parameter->block_plane)*location->plane 
					+ location->block;
				ssd->dram->map->pbn[pbn][i] =pbn;
				i++;
			}
			
		}
		/*将除通道以外chip/die/plane令牌token加一*/
		ssd->channel_head[location->channel].token=(location->chip+1)%ssd->parameter->chip_channel[0];
		ssd->channel_head[location->channel].chip_head[location->chip].token=(location->die+1)%ssd->parameter->die_chip;
		//plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].token=(location->plane+1)%ssd->parameter->plane_die;
		i=0;
		while(i<PARITY_SIZE)
		{
			if(i != p_ch)
			{
				state = state|ssd->channel_head[i].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state;
			}
			i++;
		}
		if(state == 0)
			printf("pre state == 0");
		ssd->program_count++;
		ssd->channel_head[location->channel].program_count++;
		ssd->channel_head[location->channel].chip_head[location->chip].program_count++;		
		//ssd->dram->map->map_entry[lpn].pn=ppn;	
		//ssd->dram->map->map_entry[lpn].state=set_entry_state(ssd,lsn,sub_size);   //0001
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn = -2;
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=state;
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=((~state)&full_page);
					
		free(location);
		location=NULL;
		
	}
	
}
#endif

void delete_r_sub_from_channel(struct ssd_info*ssd,unsigned int channel,struct sub_request *sub_ch)
{
	struct sub_request *sub = NULL;
	if(ssd->channel_head[channel].subs_r_head != NULL)
	{
		if(sub_ch != ssd->channel_head[channel].subs_r_head)
		{
			sub = ssd->channel_head[channel].subs_r_head;
			while (sub_ch != sub->next_node)
			{
				sub = sub->next_node;
				if(sub == NULL)
					break;
			}
			if(sub != NULL)
			{
				if(sub_ch->next_node != NULL)
				{
					sub->next_node = sub_ch->next_node;
				}
				else
				{
					sub->next_node = NULL;
					ssd->channel_head[channel].subs_r_tail = sub;
				}
			}
		}
		else
		{
			if(sub_ch != ssd->channel_head[channel].subs_r_tail)
				ssd->channel_head[channel].subs_r_head = sub_ch->next_node;
			else
			{
				ssd->channel_head[channel].subs_r_head=NULL;
				ssd->channel_head[channel].subs_r_tail=NULL;
			}
		}
	}




}


struct sub_request *creat_parity_sub_request(struct ssd_info *ssd,unsigned int start_lpn ,unsigned int lpn,unsigned int parity_size,unsigned int parity_state,struct request*req,unsigned int operation)
{
	struct sub_request* sub=NULL,* sub_r=NULL,*sub_tmp= NULL;
	struct channel_info * p_ch=NULL;
	struct local * loc=NULL;
	unsigned int flag=0;
	//unsigned int i;
	unsigned int parity_lpn;
	unsigned int sub_r_state =0,sub_r_size =0;

	unsigned int band,band_last_lpn;


	sub = (struct sub_request*)malloc(sizeof(struct sub_request));                        /*申请一个子请求的结构*/
	alloc_assert(sub,"sub_request");
	memset(sub,0, sizeof(struct sub_request));

	if(sub==NULL)
	{
		return NULL;
	}
	sub->location=NULL;
	sub->next_node=NULL;
	sub->next_subs=NULL;
	sub->update=NULL;

#ifdef PARITY_IN_REQ
	if(req!=NULL)
	{
		sub->next_subs = req->subs;
		req->subs = sub;
	}
#endif
	band = lpn/(PARITY_SIZE+1);
	band_last_lpn = band_last_lpn = (1+band)*(1+PARITY_SIZE)-1;
	parity_lpn = band_last_lpn - band%(PARITY_SIZE+1);

	if(band_last_lpn == 283639)
		printf("\n");
	if(operation == READ)
		return NULL;
	else if (operation == WRITE)
	{
		if(band_last_lpn - start_lpn > (PARITY_SIZE/2))
			flag =1 ;//read the old data page
		else
		   flag =0;//read the old data page and parity page ;
		if(band_last_lpn - start_lpn >= (PARITY_SIZE/2))
		{
			if((start_lpn!=0)&&(band_last_lpn - start_lpn != PARITY_SIZE))
				while( --start_lpn > (band_last_lpn - (PARITY_SIZE+1)))
				{
					sub_r_state = (ssd->dram->map->map_entry[start_lpn].state&0x7fffffff);
				    sub_r_size  = size(ssd->dram->map->map_entry[start_lpn].state&0x7fffffff);
					sub_r = creat_sub_request(ssd,start_lpn,sub_r_size,sub_r_state,NULL,READ);
					if(sub_r != NULL)
						{
							sub_r->read_old = sub_tmp;
							sub_tmp = sub_r;
							sub_r = NULL;
						}

				}
		}
		else
		{
			while ((start_lpn != parity_lpn)&&(start_lpn <= lpn))
			{

				sub_r_state =  (ssd->dram->map->map_entry[start_lpn].state&0x7fffffff);
				sub_r_size  = size(ssd->dram->map->map_entry[start_lpn].state&0x7fffffff);
				sub_r = creat_sub_request(ssd,start_lpn,sub_r_size,sub_r_state,NULL,READ);
				start_lpn++;
				if(sub_r != NULL)
				{
					sub_r->read_old = sub_tmp;
					sub_tmp = sub_r;
					sub_r = NULL;
				}

			}	
				sub_r_state =  (ssd->dram->map->map_entry[parity_lpn].state&0x7fffffff);
				sub_r_size  = size(ssd->dram->map->map_entry[parity_lpn].state&0x7fffffff);
				sub_r = creat_sub_request(ssd,parity_lpn,sub_r_size,sub_r_state,NULL,READ);
				if(sub_r != NULL)
				{
					sub_r->read_old = sub_tmp;
					sub_tmp = sub_r;
					sub_r = NULL;
				}

		}

	
		

		sub->read_old = sub_tmp;
		sub->ppn=0;
		sub->operation = WRITE;
		sub->location=(struct local *)malloc(sizeof(struct local));
		alloc_assert(sub->location,"sub->location");
		memset(sub->location,0, sizeof(struct local));	
	
		sub->current_state=SR_WAIT;
		sub->current_time=ssd->current_time;
		sub->lpn=parity_lpn;
		sub->size=parity_size;
		sub->state=parity_state;
		sub->begin_time=ssd->current_time;

		if (allocate_location(ssd ,sub)==ERROR)
		{
			free(sub->location);
			sub->location=NULL;
			free(sub);
			sub=NULL;
			return NULL;
		}
	}
	else
	{
		free(sub->location);
		sub->location=NULL;
		free(sub);
		sub=NULL;
		printf("\nERROR ! Unexpected command.\n");
		return NULL;
	}
	return sub;

}

void pre_process_test(struct ssd_info *ssd,int type)
{
	FILE *fp =NULL;
	unsigned int channel, chip, die, plane, block, page, band_num=0,i, j;
	unsigned int block_channel = 0;

		
	if(type == 2 )
		fp =fopen("deal_function_test.txt","w");
	else if(type == 1)
		fp =fopen("pre_function_test.txt","w");
	if(fp == NULL)
	{
		printf("open pre_function_test falided\n");
		return;
	}

	fprintf(fp,"plane:\t\t\t");
	for(i = 0; i < BAND_WITDH; i++)
	{
		fprintf(fp, "%9d\t", i);
	}
	fprintf(fp, "\nlpn\n");
	for(block = 0; block < ssd->parameter->block_plane; block++)
	{
		fprintf(fp, "superblock = %d\n", block);
		for(page = 0; page < ssd->parameter->page_block; page++)
		{
			fprintf(fp, "band_num = %d\t", block * ssd->parameter->page_block + page);
			for(channel = 0; channel < ssd->parameter->channel_number; channel++)
			{
				for(chip = 0; chip < ssd->parameter->chip_channel[0]; chip++)
				{
					for(die = 0; die < ssd->parameter->die_chip; die++)
					{
						for(plane = 0; plane < ssd->parameter->plane_die; plane++)
						{
							fprintf(fp, "%9d\t", ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn);
						}
					}
				}
			}
			fprintf(fp, "\n");
		}
		
	}

	fprintf(fp,"\nthe PARITY MAP is :\n");
	for(i=0;i < ssd->parameter->block_plane;i++)
	{
		for(j = 0; j < BAND_WITDH; j++){
			fprintf(fp, "%5d", ssd->dram->map->pbn[i][j]);
		}
		fprintf(fp, "\n");
	}
	fclose(fp);
}

unsigned int  spread_lpn(struct ssd_info * ssd,unsigned int lpn)
{	
	unsigned int add_flag;
	unsigned int band,band_position;
	band = lpn/PARITY_SIZE;
	band_position = PARITY_SIZE - band%(PARITY_SIZE+1);
	add_flag = (lpn%PARITY_SIZE >= band_position);
	/*if(band ==188369)
		printf("band ==188369");*/
	lpn = lpn + band + add_flag;
	return lpn;
}

unsigned int  spread_lsn(struct ssd_info * ssd,unsigned int lsn)
{	
	unsigned int add_flag;
	unsigned int band,lpn,band_position;
	lpn = lsn/ssd->parameter->subpage_page;
	band = lpn/PARITY_SIZE;
	band_position = PARITY_SIZE - band%(PARITY_SIZE+1);
	add_flag = (lpn%PARITY_SIZE >= band_position);
/*	if(band ==188369)
		printf("band ==188369");*/
	return (lsn + (add_flag+band)*ssd->parameter->subpage_page);
}


unsigned int shrink_lpn(struct ssd_info *ssd,unsigned int lpn)
{
	
	unsigned int add_flag;
	unsigned int band,band_position;
	band = lpn/(PARITY_SIZE+1);
	band_position = PARITY_SIZE - band%(PARITY_SIZE+1);
	add_flag = (lpn%(1+PARITY_SIZE) >= band_position);
	lpn = lpn - band - add_flag;
	return lpn;
}


unsigned int shrink_lsn(struct ssd_info *ssd,unsigned int lsn)
{
	
	unsigned int add_flag;
	unsigned int band,lpn,band_position;
	lpn = lsn/ssd->parameter->subpage_page;
	band = lpn/(PARITY_SIZE+1);
	band_position = PARITY_SIZE - band%(PARITY_SIZE+1);
	add_flag = (lpn%(1+PARITY_SIZE) >= band_position);
	
	return (lsn - (add_flag + band )*ssd->parameter->subpage_page);
}


// pre_write_parity_page is instaced before hte shrink_lsn ,so the strip size is PARITY?1
void pre_write_parity_page(struct ssd_info *ssd,unsigned int lsn)
{

	unsigned int i,state = 0;
	unsigned int band;
	unsigned int lpn,llsn,band_last_lpn;
	unsigned int ppn,full_page;

	struct local *location = NULL;
	full_page=~(0xffffffff<<(ssd->parameter->subpage_page));
	band = lsn/(ssd->parameter->subpage_page*(1+PARITY_SIZE));
	band_last_lpn = (1+band)*(1+PARITY_SIZE)-1;

	lpn = band_last_lpn - band%(PARITY_SIZE+1);

	for(i=0;i<=ssd->parameter->subpage_page;i++)
		{
			if(band_last_lpn-i != lpn)
				state = state | ssd->dram->map->map_entry[band_last_lpn-i].state;
		}

	if(ssd->dram->map->map_entry[lpn].state!=0)
		ppn = ssd->dram->map->map_entry[lpn].pn;
	else
		{
			llsn = lpn*ssd->parameter->subpage_page;
			ppn = get_ppn_for_pre_process(ssd,lsn);
		}
	location = find_location(ssd,ppn);
	ssd->program_count++;
	ssd->channel_head[location->channel].program_count++;
	ssd->channel_head[location->channel].chip_head[location->chip].program_count++;		
	ssd->dram->map->map_entry[lpn].pn=ppn;	
	ssd->dram->map->map_entry[lpn].state=state;   //0001
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=lpn;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=ssd->dram->map->map_entry[lpn].state;
	ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=((~ssd->dram->map->map_entry[lpn].state)&full_page);
	
	free(location);
	location=NULL;
	
}

/*void generate_parity(struct ssd_info*ssd,struct sub_request * sub)
{
	unsigned int i;
	i = 0;

}*/