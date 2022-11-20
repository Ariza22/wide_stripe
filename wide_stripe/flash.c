/*****************************************************************************************************************************
This project was supported by the National Basic Research 973 Program of China under Grant No.2011CB302301
Huazhong University of Science and Technology (HUST)   Wuhan National Laboratory for Optoelectronics

FileName： flash.c
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

#include "flash.h"
#include "ssd.h"
#include "pagemap.h"
#include "initialize.h"
#include "raid.h"
#include "states.h"
#include "recover.h"

/****************************************************************************************
*这个函数只作用于写请求
*当写请求要写的物理页不为空闲且无法覆盖已写的子页的时候在写之前需要先读取该页中的内容
*为写请求的更新操作产生读请求，并将读请求挂载到对应channel的读请求队列的尾部
*根据参数文件中的信息，采用动态或静态的方式为写请求分配channel，chip，die，plane
*将写请求挂载到对应channel的写请求队列的尾部
*****************************************************************************************/
Status allocate_location(struct ssd_info * ssd ,struct sub_request *sub_req)
{
	struct sub_request * update=NULL;		//update表示因写入操作需要更新而产生的读请求
	unsigned int channel_num=0,chip_num=0,die_num=0,plane_num=0;
	struct local *location=NULL;

	channel_num=ssd->parameter->channel_number;
	chip_num=ssd->parameter->chip_channel[0];
	die_num=ssd->parameter->die_chip;
	plane_num=ssd->parameter->plane_die;
    
	
	if (ssd->parameter->allocation_scheme==0)                                          /*动态分配的情况*/
	{
		/******************************************************************
		*在动态分配中，因为页的更新操作使用不了copyback操作，
		*需要产生一个读请求，并且只有这个读请求完成后才能进行这个页的写操作
		*******************************************************************/
		if (ssd->dram->map->map_entry[sub_req->lpn].state!=0)    
		{//lpn对应的物理页中存放有数据
			if ((sub_req->state&ssd->dram->map->map_entry[sub_req->lpn].state)!=ssd->dram->map->map_entry[sub_req->lpn].state)
			{//将要写入的子页不包含当前物理页中的所有子页
				ssd->read_count++;
				ssd->update_read_count++;
#ifdef USE_WHITE_PARITY
				ssd->read_sub_request++;
#endif
				/***************************************************
				*产生新的读请求，并且挂到channel的subs_r_tail队列尾
				****************************************************/
				update=(struct sub_request *)malloc(sizeof(struct sub_request));
				alloc_assert(update,"update");
				memset(update,0, sizeof(struct sub_request));

				if(update==NULL)
				{
					return ERROR;
				}
				update->location=NULL;
				update->next_node=NULL;
				update->next_subs=NULL;
				update->update=NULL;						
				location = find_location(ssd,ssd->dram->map->map_entry[sub_req->lpn].pn);
				update->location=location;
				update->begin_time = ssd->current_time;
				update->current_state = SR_WAIT;
				update->current_time=0x7fffffffffffffff;
				update->next_state = SR_R_C_A_TRANSFER;
				update->next_state_predict_time=0x7fffffffffffffff;
				update->lpn = sub_req->lpn;
				update->state=((ssd->dram->map->map_entry[sub_req->lpn].state^sub_req->state)&0x7fffffff);
				update->size=size(update->state);
				update->ppn = ssd->dram->map->map_entry[sub_req->lpn].pn;
				update->operation = READ;
				
				if (ssd->channel_head[location->channel].subs_r_tail!=NULL)            
				{
						ssd->channel_head[location->channel].subs_r_tail->next_node=update;
						ssd->channel_head[location->channel].subs_r_tail=update;
				} 
				else
				{
					ssd->channel_head[location->channel].subs_r_tail=update;
					ssd->channel_head[location->channel].subs_r_head=update;
				}
			}
		}
		/***************************************
		*以下是动态分配的几种情况
		*0：全动态分配
		*1：表示channel定package，die，plane动态
		****************************************/
		switch(ssd->parameter->dynamic_allocation)
		{
			case 0:
			{
				sub_req->location->channel=-1;
				sub_req->location->chip=-1;
				sub_req->location->die=-1;
				sub_req->location->plane=-1;
				sub_req->location->block=-1;
				sub_req->location->page=-1;

				if (ssd->subs_w_tail!=NULL)
				{
					ssd->subs_w_tail->next_node=sub_req;
					ssd->subs_w_tail=sub_req;
				} 
				else
				{
					ssd->subs_w_tail=sub_req;
					ssd->subs_w_head=sub_req;
				}

				if (update!=NULL)
				{
					sub_req->update=update;
				}

				break;
			}
			case 1:
			{
				 
				sub_req->location->channel=sub_req->lpn%ssd->parameter->channel_number;
				sub_req->location->chip=-1;
				sub_req->location->die=-1;
				sub_req->location->plane=-1;
				sub_req->location->block=-1;
				sub_req->location->page=-1;

				if (update!=NULL)
				{
					sub_req->update=update;
				}

				break;
			}
			case 2:
			{
				break;
			}
			case 3:
			{
				break;
			}
		}

	}
	else                                                                          
	{	/***************************************************************************
		*是静态分配方式，所以可以将这个子请求的最终channel，chip，die，plane全部得出
		*总共有0,1,2,3,4,5,这六种静态分配方式。
		****************************************************************************/
		switch (ssd->parameter->static_allocation)
		{
			case 0:         //no striping static allocation
			{
				sub_req->location->channel=(sub_req->lpn/(plane_num*die_num*chip_num))%channel_num;
				sub_req->location->chip=sub_req->lpn%chip_num;
				sub_req->location->die=(sub_req->lpn/chip_num)%die_num;
				sub_req->location->plane=(sub_req->lpn/(die_num*chip_num))%plane_num;
				break;
			}
			case 1:
			{
				sub_req->location->channel=sub_req->lpn%channel_num;
				sub_req->location->chip=(sub_req->lpn/channel_num)%chip_num;
				sub_req->location->die=(sub_req->lpn/(chip_num*channel_num))%die_num;
				sub_req->location->plane=(sub_req->lpn/(die_num*chip_num*channel_num))%plane_num;
							
				break;
			}
			case 2:
			{
				sub_req->location->channel=sub_req->lpn%channel_num;
				sub_req->location->chip=(sub_req->lpn/(plane_num*channel_num))%chip_num;
				sub_req->location->die=(sub_req->lpn/(plane_num*chip_num*channel_num))%die_num;
				sub_req->location->plane=(sub_req->lpn/channel_num)%plane_num;
				break;
			}
			case 3:
			{
				sub_req->location->channel=sub_req->lpn%channel_num;
				sub_req->location->chip=(sub_req->lpn/(die_num*channel_num))%chip_num;
				sub_req->location->die=(sub_req->lpn/channel_num)%die_num;
				sub_req->location->plane=(sub_req->lpn/(die_num*chip_num*channel_num))%plane_num;
				break;
			}
			case 4:  
			{
				sub_req->location->channel=sub_req->lpn%channel_num;
				sub_req->location->chip=(sub_req->lpn/(plane_num*die_num*channel_num))%chip_num;
				sub_req->location->die=(sub_req->lpn/(plane_num*channel_num))%die_num;
				sub_req->location->plane=(sub_req->lpn/channel_num)%plane_num;
							
				break;
			}
			case 5:   
			{
				sub_req->location->channel=sub_req->lpn%channel_num;
				sub_req->location->chip=(sub_req->lpn/(plane_num*die_num*channel_num))%chip_num;
				sub_req->location->die=(sub_req->lpn/channel_num)%die_num;
				sub_req->location->plane=(sub_req->lpn/(die_num*channel_num))%plane_num;
							
				break;
			}
			default : return ERROR;
		
		}
		if (ssd->dram->map->map_entry[sub_req->lpn].state!=0)
		{	
			/*这个写回的子请求的逻辑页不可以覆盖之前被写回的数据，需要产生读请求*/ 
			if ((sub_req->state&ssd->dram->map->map_entry[sub_req->lpn].state)!=ssd->dram->map->map_entry[sub_req->lpn].state)  
			{
				ssd->read_count++;
				ssd->update_read_count++;
				update=(struct sub_request *)malloc(sizeof(struct sub_request));
				alloc_assert(update,"update");
				memset(update,0, sizeof(struct sub_request));
				
				if(update==NULL)
				{
					return ERROR;
				}
				update->location=NULL;
				update->next_node=NULL;
				update->next_subs=NULL;
				update->update=NULL;						
				location = find_location(ssd,ssd->dram->map->map_entry[sub_req->lpn].pn);
				update->location=location;
				update->begin_time = ssd->current_time;
				update->current_state = SR_WAIT;
				update->current_time=0x7fffffffffffffff;
				update->next_state = SR_R_C_A_TRANSFER;
				update->next_state_predict_time=0x7fffffffffffffff;
				update->lpn = sub_req->lpn;
				update->state=((ssd->dram->map->map_entry[sub_req->lpn].state^sub_req->state)&0x7fffffff);
				update->size=size(update->state);
				update->ppn = ssd->dram->map->map_entry[sub_req->lpn].pn;
				update->operation = READ;
				
				if (ssd->channel_head[location->channel].subs_r_tail!=NULL)
				{
					ssd->channel_head[location->channel].subs_r_tail->next_node=update;
					ssd->channel_head[location->channel].subs_r_tail=update;
				} 
				else
				{
					ssd->channel_head[location->channel].subs_r_tail=update;
					ssd->channel_head[location->channel].subs_r_head=update;
				}
			}

			if (update!=NULL)
			{
				sub_req->update=update;

				sub_req->state=(sub_req->state|update->state);
				sub_req->size=size(sub_req->state);
			}

 		}
	}

	/*将写请求挂载到对应channel的写请求队列的尾部 ，对于静态分配和半动态分配的情况，都需要将写请求挂到通道写请求队列尾部。*/
	if ((ssd->parameter->allocation_scheme!=0)||(ssd->parameter->dynamic_allocation!=0))
	{
		if (ssd->channel_head[sub_req->location->channel].subs_w_tail!=NULL)
		{
			ssd->channel_head[sub_req->location->channel].subs_w_tail->next_node=sub_req;
			ssd->channel_head[sub_req->location->channel].subs_w_tail=sub_req;
		} 
		else
		{
			ssd->channel_head[sub_req->location->channel].subs_w_tail=sub_req;
			ssd->channel_head[sub_req->location->channel].subs_w_head=sub_req;
		}
	}
	return SUCCESS;					
}	


/*******************************************************************************
*insert2buffer这个函数是专门为写请求分配子请求服务的在buffer_management中被调用。
********************************************************************************/
#ifdef NO_SUPERPAGE
struct ssd_info * insert2buffer(struct ssd_info *ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req)      
{
	int write_back_count,flag=0;                                                             /*flag表示为写入新数据腾空间是否完成，0表示需要进一步腾，1表示已经腾空*/
	unsigned int i,lsn,hit_flag,add_flag,sector_count,active_region_flag=0,free_sector=0;
	struct buffer_group *buffer_node=NULL,*pt,*new_node=NULL,key;
	struct sub_request *sub_req=NULL,*update=NULL;
	
	
	unsigned int sub_req_state=0, sub_req_size=0,sub_req_lpn=0;

	#ifdef DEBUG
	printf("enter insert2buffer,  current time:%I64u, lpn:%d, state:%d,\n",ssd->current_time,lpn,state);
	#endif

	sector_count=size(state);		/*需要写到buffer的sector个数*/
	key.group=lpn;
	buffer_node= (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);    /*在平衡二叉树中寻找buffer node*/ 
    
	/************************************************************************************************
	*没有命中。
	*第一步根据这个lpn有多少子页需要写到buffer，去除已写回的lsn，为该lpn腾出位置，
	*首先即要计算出free sector（表示还有多少可以直接写的buffer节点）。
	*如果free_sector>=sector_count，即有多余的空间够lpn子请求写，不需要产生写回请求
	*否则，没有多余的空间供lpn子请求写，这时需要释放一部分空间，产生写回请求。就要creat_sub_request()
	*************************************************************************************************/
	if(buffer_node==NULL)
	{
		free_sector=ssd->dram->buffer->max_buffer_sector-ssd->dram->buffer->buffer_sector_count;   
		if(free_sector>=sector_count)
		{//有多余的空间够lpn子请求写，不需要产生写回请求
			flag=1;    
		}
		if(flag==0)     
		{//没有多余的空间供lpn子请求写，这时需要释放一部分空间，产生写回请求
			write_back_count=sector_count-free_sector;
			ssd->dram->buffer->write_miss_hit=ssd->dram->buffer->write_miss_hit+write_back_count;
			while(write_back_count>0)
			{
				sub_req=NULL;
				sub_req_state=ssd->dram->buffer->buffer_tail->stored; 
				sub_req_size=size(ssd->dram->buffer->buffer_tail->stored);
				sub_req_lpn=ssd->dram->buffer->buffer_tail->group;
				sub_req=creat_sub_request(ssd,sub_req_lpn,sub_req_size,sub_req_state,req,WRITE);
				
				/**********************************************************************************
				*req不为空，表示这个insert2buffer函数是在buffer_management中调用，传递了request进来
				*req为空，表示这个函数是在process函数中处理一对多映射关系的读的时候，需要将这个读出
				*的数据加到buffer中，这可能产生实时的写回操作，需要将这个实时的写回操作的子请求挂在
				*这个读请求的总请求上
				***********************************************************************************/
				if(req!=NULL)                                             
				{
				}
				else    
				{
					sub_req->next_subs=sub->next_subs;
					sub->next_subs=sub_req;
				}
                
				/*********************************************************************
				*写请求插入到了平衡二叉树，这时就要修改dram的buffer_sector_count；
				*维持平衡二叉树调用avlTreeDel()和AVL_TREENODE_FREE()函数；
				*为了维持LRU算法，需要将位于LRU队列队尾的节点从LRU队列中删除
				**********************************************************************/
				ssd->dram->buffer->buffer_sector_count=ssd->dram->buffer->buffer_sector_count-sub_req->size;
				pt = ssd->dram->buffer->buffer_tail;
				avlTreeDel(ssd->dram->buffer, (TREE_NODE *) pt);
				if(ssd->dram->buffer->buffer_head->LRU_link_next == NULL)
				{
					ssd->dram->buffer->buffer_head = NULL;
					ssd->dram->buffer->buffer_tail = NULL;
				}
				else
				{
					ssd->dram->buffer->buffer_tail=ssd->dram->buffer->buffer_tail->LRU_link_pre;
					ssd->dram->buffer->buffer_tail->LRU_link_next=NULL;
				}
				pt->LRU_link_next=NULL;
				pt->LRU_link_pre=NULL;
				AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE *) pt);
				pt = NULL;
				
				write_back_count=write_back_count-sub_req->size;                            /*因为产生了实时写回操作，需要将主动写回操作区域增加*/
			}
		}
		
		/******************************************************************************
		*生成一个buffer node，根据这个页的情况分别赋值个各个成员，
		*添加到LRU队列队首和以平衡二叉树方式管理的buffer中
		*******************************************************************************/
		new_node=NULL;
		new_node=(struct buffer_group *)malloc(sizeof(struct buffer_group));
		alloc_assert(new_node,"buffer_group_node");
		memset(new_node,0, sizeof(struct buffer_group));
		
		new_node->group=lpn;
		new_node->stored=state;
		new_node->dirty_clean=state;
		new_node->LRU_link_pre = NULL;
		new_node->LRU_link_next=ssd->dram->buffer->buffer_head;
		if(ssd->dram->buffer->buffer_head != NULL)
		{
			ssd->dram->buffer->buffer_head->LRU_link_pre=new_node;
		}
		else
		{
			ssd->dram->buffer->buffer_tail = new_node;
		}
		ssd->dram->buffer->buffer_head=new_node;
		new_node->LRU_link_pre=NULL;
		avlTreeAdd(ssd->dram->buffer, (TREE_NODE *) new_node);
		ssd->dram->buffer->buffer_sector_count += sector_count;
	}
	/****************************************************************************************
	*在buffer中命中的情况
	*算然命中了，但是命中的只是lpn，有可能新来的写请求，只是需要写lpn这一page的某几个sub_page
	*这时需要进一步的判断
	*****************************************************************************************/
	else
	{
		for(i=0;i<ssd->parameter->subpage_page;i++)
		{
			/*************************************************************
			*判断state第i位是不是1
			*并且判断第i个sector是否存在buffer中，1表示存在，0表示不存在。
			**************************************************************/
			if((state>>i)%2!=0)                                                         
			{
				lsn=lpn*ssd->parameter->subpage_page+i;
				hit_flag=0;
				hit_flag=(buffer_node->stored)&(0x00000001<<i);
				
				if(hit_flag!=0)				/*命中了，需要将该节点移到buffer的队首，并且将命中的lsn进行标记*/
				{	
					active_region_flag=1;		/*用来记录在这个buffer node中的lsn是否被命中，用于后面对阈值的判定*/

					if(req!=NULL)
					{
						if(ssd->dram->buffer->buffer_head!=buffer_node)     
						{//将buffer_node节点移动到buffer队列的队首				
							if(ssd->dram->buffer->buffer_tail==buffer_node)
							{				
								ssd->dram->buffer->buffer_tail=buffer_node->LRU_link_pre;
								buffer_node->LRU_link_pre->LRU_link_next=NULL;					
							}				
							else if(buffer_node != ssd->dram->buffer->buffer_head)
							{					
								buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;				
								buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
							}				
							buffer_node->LRU_link_next=ssd->dram->buffer->buffer_head;	
							ssd->dram->buffer->buffer_head->LRU_link_pre=buffer_node;
							buffer_node->LRU_link_pre=NULL;				
							ssd->dram->buffer->buffer_head=buffer_node;					
						}					
						ssd->dram->buffer->write_hit++;
						req->complete_lsn_count++;                                        /*关键 当在buffer中命中时 就用req->complete_lsn_count++表示往buffer中写了数据。*/					
					}
					else
					{
					}				
				}			
				else                 			
				{
					/************************************************************************************************************
					*该lsn没有命中，但是节点在buffer中，需要将这个lsn加到buffer的对应节点中
					*从buffer的末端找一个节点，将一个已经写回的lsn从节点中删除(如果找到的话)，更改这个节点的状态，同时将这个新的
					*lsn加到相应的buffer节点中，该节点可能在buffer头，不在的话，将其移到头部。如果没有找到已经写回的lsn，在buffer
					*节点找一个group整体写回，将这个子请求挂在这个请求上。可以提前挂在一个channel上。
					*第一步:将buffer队尾的已经写回的节点删除一个，为新的lsn腾出空间，这里需要修改队尾某节点的stored状态这里还需要
					*       增加，当没有可以之间删除的lsn时，需要产生新的写子请求，写回LRU最后的节点。
					*第二步:将新的lsn加到所述的buffer节点中。
					*************************************************************************************************************/	
					ssd->dram->buffer->write_miss_hit++;
					
					if(ssd->dram->buffer->buffer_sector_count>=ssd->dram->buffer->max_buffer_sector)
					{
						if (buffer_node==ssd->dram->buffer->buffer_tail)                  /*如果命中的节点是buffer中最后一个节点，交换最后两个节点*/
						{
							pt = ssd->dram->buffer->buffer_tail->LRU_link_pre;
							ssd->dram->buffer->buffer_tail->LRU_link_pre=pt->LRU_link_pre;
							ssd->dram->buffer->buffer_tail->LRU_link_pre->LRU_link_next=ssd->dram->buffer->buffer_tail;
							ssd->dram->buffer->buffer_tail->LRU_link_next=pt;
							pt->LRU_link_next=NULL;
							pt->LRU_link_pre=ssd->dram->buffer->buffer_tail;
							ssd->dram->buffer->buffer_tail=pt;
							
						}
						sub_req=NULL;
						sub_req_state=ssd->dram->buffer->buffer_tail->stored; 
						sub_req_size=size(ssd->dram->buffer->buffer_tail->stored);
						sub_req_lpn=ssd->dram->buffer->buffer_tail->group;
						sub_req=creat_sub_request(ssd,sub_req_lpn,sub_req_size,sub_req_state,req,WRITE);

						if(req!=NULL)           
						{
							
						}
						else if(req==NULL)   
						{
							sub_req->next_subs=sub->next_subs;
							sub->next_subs=sub_req;
						}

						ssd->dram->buffer->buffer_sector_count=ssd->dram->buffer->buffer_sector_count-sub_req->size;
						pt = ssd->dram->buffer->buffer_tail;	
						avlTreeDel(ssd->dram->buffer, (TREE_NODE *) pt);
							
						/************************************************************************/
						/* 改:  挂在了子请求，buffer的节点不应立即删除，                        */
						/*      需等到写回了之后才能删除									             */
						/************************************************************************/
						if(ssd->dram->buffer->buffer_head->LRU_link_next == NULL)
						{
							ssd->dram->buffer->buffer_head = NULL;
							ssd->dram->buffer->buffer_tail = NULL;
						}
						else
						{
							ssd->dram->buffer->buffer_tail=ssd->dram->buffer->buffer_tail->LRU_link_pre;
							ssd->dram->buffer->buffer_tail->LRU_link_next=NULL;
						}
						pt->LRU_link_next=NULL;
						pt->LRU_link_pre=NULL;
						AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE *) pt);
						pt = NULL;	
					}

					/*第二步:将新的lsn加到所述的buffer节点中*/	
					add_flag=0x00000001<<(lsn%ssd->parameter->subpage_page);
					
					if(ssd->dram->buffer->buffer_head!=buffer_node)                      /*如果该buffer节点不在buffer的队首，需要将这个节点提到队首*/
					{				
						if(ssd->dram->buffer->buffer_tail==buffer_node)
						{					
							buffer_node->LRU_link_pre->LRU_link_next=NULL;					
							ssd->dram->buffer->buffer_tail=buffer_node->LRU_link_pre;
						}			
						else						
						{			
							buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;						
							buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;								
						}								
						buffer_node->LRU_link_next=ssd->dram->buffer->buffer_head;			
						ssd->dram->buffer->buffer_head->LRU_link_pre=buffer_node;
						buffer_node->LRU_link_pre=NULL;	
						ssd->dram->buffer->buffer_head=buffer_node;							
					}					
					buffer_node->stored=buffer_node->stored|add_flag;		
					buffer_node->dirty_clean=buffer_node->dirty_clean|add_flag;	
					ssd->dram->buffer->buffer_sector_count++;
				}			

			}
		}
	}

	return ssd;
}
#endif
int release_2_buffer = 0;
int buffer_node_miss = 0;
int not_miss_node_but_miss_lsn = 0;
struct ssd_info * insert2buffer(struct ssd_info *ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req)      
{
	int write_back_count,flag=0;                                                             /*flag表示为写入新数据腾空间是否完成，0表示需要进一步腾，1表示已经腾空*/
	unsigned int i,lsn,hit_flag,add_flag,sector_count,active_region_flag=0,free_sector=0;
	struct buffer_group *buffer_node=NULL,*pt,*new_node=NULL,key;
	struct sub_request *sub_req=NULL,*update=NULL;
	unsigned int sub_req_state=0, sub_req_size=0,sub_req_lpn=0, parity_state = 0;

	unsigned int active_superblock, active_superpage;
	int first_parity;
	int ec_mode;
	int page_mask = 0;
	unsigned int pos; 
	unsigned int hit_state, miss_hit_state;
	int release_buffer_flag = 0;
	/*if (lpn == 322027)
		printf("yes");*/
	#ifdef DEBUG
	printf("enter insert2buffer,  current time:%I64u, lpn:%d, state:%d,\n",ssd->current_time,lpn,state);
	#endif

	sector_count=size(state);		/*需要写到buffer的sector个数*/
	key.group=lpn;
	buffer_node= (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);    /*在平衡二叉树中寻找buffer node*/ 
	/************************************************************************************************
	*没有命中。
	*第一步根据这个lpn有多少子页需要写到buffer，去除已写回的lsn，为该lpn腾出位置，
	*首先即要计算出free sector（表示还有多少可以直接写的buffer节点）。
	*如果free_sector>=sector_count，即有多余的空间够lpn子请求写，不需要产生写回请求
	*否则，没有多余的空间供lpn子请求写，这时需要释放一部分空间，产生写回请求。就要creat_sub_request()
	*************************************************************************************************/
	//printf("free_sector = %d\n", ssd->dram->buffer->max_buffer_sector-ssd->dram->buffer->buffer_sector_count);
	if(buffer_node==NULL)
	{
		//printf("miss_buffer_node\tfree_sector = %d\n", ssd->dram->buffer->max_buffer_sector-ssd->dram->buffer->buffer_sector_count);
		buffer_node_miss += size(state);
		ssd->dram->buffer->write_miss_hit += size(state);
		free_sector=ssd->dram->buffer->max_buffer_sector-ssd->dram->buffer->buffer_sector_count; 
		if(size(state) > free_sector)
		{
			//printf("xiashua\n");
			release_buffer_flag = 1;
		}
/*
		if(free_sector>=size(state))
		{//有多余的空间够lpn子请求写，不需要产生写回请求
			flag=1;    
		}
		//没有多余的空间供lpn子请求写，这时需要释放一部分空间（一次性释放一个超级页的大小），产生写回请求
		if(flag==0)     
		{
			//当superpage_buffer中的数据写回flash之后，才接收下次从buffer中下刷的数据
			if(ssd->strip_bit == 0)
			{
				//获取将要写的条带的ec模式
				if(find_active_block(ssd, 0, 0, 0, 0) == FAILURE)
				{
					printf("get_ppn()\tERROR :there is no free page in channel:0, chip:0, die:0, plane:0\n");	
					return NULL;
				}

				active_superblock = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].active_block; //获取当前超级块号
				ec_mode = ssd->band_head[active_superblock].ec_modle;
				//此处原本应该在写实际的闪存页时才将last_write_page和free_page_num进行修改的，但因为此处已经为请求分配具体的物理页，为了防止下次分配请求的时候查找错误的active_block和page，在此处就修改（后续实际写闪存页的时候不需再次修改，否则会发生错误）
				active_superpage = ++(ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page); //获取将要写的超级页
				ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].free_page_num--;
				if(active_superpage>=(int)(ssd->parameter->page_block))
				{
					ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page=0;
					printf("insert2buffer();   error! the last write page larger than 64!!\n");
					return NULL;
				}
				first_parity = BAND_WITDH - (((active_superpage + 1) * ec_mode) % BAND_WITDH); //找出第一个校验的位置
				for(i = first_parity; i < first_parity + ec_mode; i++)
				{
					page_mask = page_mask | (1 << (i % BAND_WITDH));
				}
				//先将buffer中的最后band_width - ec_mode个buffer node迁移到superpage_buffer中，然后产生校验，并一次性将superpage大小写到闪存
				for(i = 0; i < BAND_WITDH - ec_mode; i++)
				{
					pos = find_first_zero(ssd, page_mask);
					page_mask = page_mask | (1 << (pos % BAND_WITDH));

					pt = ssd->dram->buffer->buffer_tail;
					//从二叉树中删除该节点
					avlTreeDel(ssd->dram->buffer, (TREE_NODE *) pt); 
					// 从LRU中删除该节点
					if(ssd->dram->buffer->buffer_head->LRU_link_next == NULL)
					{
						ssd->dram->buffer->buffer_head = NULL;
						ssd->dram->buffer->buffer_tail = NULL;
					}
					else
					{
						ssd->dram->buffer->buffer_tail=ssd->dram->buffer->buffer_tail->LRU_link_pre;
						ssd->dram->buffer->buffer_tail->LRU_link_next=NULL;
					}
					pt->LRU_link_next=NULL;
					pt->LRU_link_pre=NULL;
					//将该节点的存放在superpage中
					ssd->dram->superpage_buffer[pos].state = pt->stored; 
					ssd->dram->superpage_buffer[pos].size = size(pt->stored);
					ssd->dram->superpage_buffer[pos].lpn = pt->group;
					//计算校验数据的请求状态
					parity_state |= ssd->dram->superpage_buffer[pos].state;
					//释放该节点
					AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE *) pt);
					pt = NULL;
					//修改buffer中的缓冲区缓存的sector数
					ssd->dram->buffer->buffer_sector_count -= ssd->dram->superpage_buffer[pos].size;
				}
				//产生校验数据
				for(i = first_parity; i < first_parity + ec_mode; i++)
				{
					ssd->dram->superpage_buffer[i % BAND_WITDH].lpn = -2;
					ssd->dram->superpage_buffer[i % BAND_WITDH].size = size(parity_state);
					ssd->dram->superpage_buffer[i % BAND_WITDH].state = parity_state;
				}
				//为整个superpage创建子请求，挂在到相应的通道中
				for(i = 0; i < BAND_WITDH; i++)
				{
					sub_req=NULL;
					sub_req_state = ssd->dram->superpage_buffer[i].state; 
					sub_req_size = ssd->dram->superpage_buffer[i].size;
					sub_req_lpn = ssd->dram->superpage_buffer[i].lpn;
					if((sub_req = creat_write_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, pos, active_superblock, active_superpage)) == NULL)
					{
						printf("insert2buffer()\tWrite subrequest creation failed!\n");
						return NULL;
					}
					if(req == NULL)                                                
					{
						sub_req->next_subs = sub->next_subs;
						sub->next_subs = sub_req;
					}
				}
			}
			else
			{
				
			}
			
		}
		//生成一个buffer node，根据这个页的情况分别赋值个各个成员，添加到LRU队列队首和以平衡二叉树方式管理的buffer中
		new_node=NULL;
		new_node=(struct buffer_group *)malloc(sizeof(struct buffer_group));
		alloc_assert(new_node,"buffer_group_node");
		memset(new_node,0, sizeof(struct buffer_group));

		new_node->group=lpn;
		new_node->stored=state;
		new_node->dirty_clean=state;
		new_node->LRU_link_pre = NULL;
		new_node->LRU_link_next=ssd->dram->buffer->buffer_head;
		if(ssd->dram->buffer->buffer_head != NULL)
		{
			ssd->dram->buffer->buffer_head->LRU_link_pre=new_node;
		}
		else
		{
			ssd->dram->buffer->buffer_tail = new_node;
		}
		ssd->dram->buffer->buffer_head=new_node;
		new_node->LRU_link_pre=NULL;
		avlTreeAdd(ssd->dram->buffer, (TREE_NODE *) new_node);
		ssd->dram->buffer->buffer_sector_count += sector_count;
*/
		
	}
	/****************************************************************************************
	*在buffer中命中的情况
	*虽然命中了，但是命中的只是lpn，有可能新来的写请求，只是需要写lpn这一page的某几个sub_page
	*这时需要进一步的判断
	*****************************************************************************************/
	else
	{
		hit_state = state & buffer_node->stored;
		miss_hit_state = (state ^ buffer_node->stored) & state;
		//存在命中的lsn
		if(hit_state != 0)
		{
			if(req!=NULL)
			{
				//将buffer_node节点移动到buffer队列的队首
				if(ssd->dram->buffer->buffer_head!=buffer_node)     
				{				
					if(ssd->dram->buffer->buffer_tail==buffer_node)
					{				
						ssd->dram->buffer->buffer_tail=buffer_node->LRU_link_pre;
						buffer_node->LRU_link_pre->LRU_link_next=NULL;					
					}				
					else if(buffer_node != ssd->dram->buffer->buffer_head)
					{					
						buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;				
						buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
					}				
					buffer_node->LRU_link_next=ssd->dram->buffer->buffer_head;	
					ssd->dram->buffer->buffer_head->LRU_link_pre=buffer_node;
					buffer_node->LRU_link_pre=NULL;				
					ssd->dram->buffer->buffer_head=buffer_node;					
				}					
				ssd->dram->buffer->write_hit += size(hit_state);
				req->complete_lsn_count += size(hit_state);                                        /*关键 当在buffer中命中时 就用req->complete_lsn_count++表示往buffer中写了数据。*/					
			}
			else
			{
			}		
		}
		if(miss_hit_state != 0)
		{
			//printf("mot_miss_buffer_but_miss_lsn\tfree_sector = %d\tmiss_bit_state = %d\n", ssd->dram->buffer->max_buffer_sector-ssd->dram->buffer->buffer_sector_count, size(miss_hit_state));
			not_miss_node_but_miss_lsn += size(miss_hit_state);
			ssd->dram->buffer->write_miss_hit += size(miss_hit_state);
			free_sector = ssd->dram->buffer->max_buffer_sector - ssd->dram->buffer->buffer_sector_count;
			//如果未命中的lsn能被buffer全部缓存，则不需产生写回请求腾空间，否则产生写请求
			if(free_sector >= size(miss_hit_state))
			{
				if(ssd->dram->buffer->buffer_head!=buffer_node)                      /*如果该buffer节点不在buffer的队首，需要将这个节点提到队首*/
				{				
					if(ssd->dram->buffer->buffer_tail==buffer_node)
					{					
						buffer_node->LRU_link_pre->LRU_link_next=NULL;					
						ssd->dram->buffer->buffer_tail=buffer_node->LRU_link_pre;
					}			
					else						
					{			
						buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;						
						buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;								
					}								
					buffer_node->LRU_link_next=ssd->dram->buffer->buffer_head;			
					ssd->dram->buffer->buffer_head->LRU_link_pre=buffer_node;
					buffer_node->LRU_link_pre=NULL;	
					ssd->dram->buffer->buffer_head=buffer_node;							
				}					
				buffer_node->stored |= miss_hit_state;		
				buffer_node->dirty_clean |= miss_hit_state;	
				ssd->dram->buffer->buffer_sector_count +=size(miss_hit_state);
			}
			else
			{
				release_buffer_flag = 1; //未命中的lsn不能被buffer全部服务，则释放之后服务
				//printf("........xiashau\n");
			}

		}
	}

	if(release_buffer_flag == 1)
	{
		release_2_buffer++;
		ssd = buffer_2_superpage_buffer(ssd, sub, req);
	}
	// 释放buffer后，有足够的空间服务写请求
	// 未命中lpn，则产生新的buffer_node挂到buffer队列尾；命中lpn时，修改命中的buffer_node
	if(buffer_node == NULL)
	{
		//生成一个buffer node，根据这个页的情况分别赋值个各个成员，添加到LRU队列队首和以平衡二叉树方式管理的buffer中
		new_node=NULL;
		new_node=(struct buffer_group *)malloc(sizeof(struct buffer_group));
		alloc_assert(new_node,"buffer_group_node");
		memset(new_node,0, sizeof(struct buffer_group));

		new_node->group=lpn;
		new_node->stored=state;
		new_node->dirty_clean=state;
		new_node->LRU_link_pre = NULL;
		new_node->LRU_link_next=ssd->dram->buffer->buffer_head;
		if(ssd->dram->buffer->buffer_head != NULL)
		{
			ssd->dram->buffer->buffer_head->LRU_link_pre=new_node;
		}
		else
		{
			ssd->dram->buffer->buffer_tail = new_node;
		}
		ssd->dram->buffer->buffer_head=new_node;
		new_node->LRU_link_pre=NULL;
		avlTreeAdd(ssd->dram->buffer, (TREE_NODE *) new_node);
		ssd->dram->buffer->buffer_sector_count += size(state);
	}
	else if(release_buffer_flag == 1) //命中了buffer但是未命中lsn
	{
			buffer_node->stored |= miss_hit_state;		
			buffer_node->dirty_clean |= miss_hit_state;	
			ssd->dram->buffer->buffer_sector_count +=size(miss_hit_state);
	}

	return ssd;
}

int write_flash_sub_count = 0;
// data_buffer不够写时，将部分数据迁移到superpage_buffer中
struct ssd_info *buffer_2_superpage_buffer(struct ssd_info *ssd, struct sub_request *sub, struct request *req)
{
	struct buffer_group *pt = NULL;
	struct sub_request *sub_req=NULL;
	unsigned int sub_req_state=0, sub_req_size=0,sub_req_lpn=0, parity_state = 0;
	unsigned int active_superblock, active_superpage;
	int i, j, k, l, first_parity;
	int ec_mode;
	int page_mask = 0;
	unsigned int pos; 

	int wear_num, high_wear_num, idle_num, healthy_num;
	unsigned long long wear_flag, high_wear_flag, idle_flag, healthy_flag;

	//获取将要写的条带的ec模式
	while (1) {
		if (find_superblock_for_write(ssd, 0, 0, 0, 0, req) == FAILURE)
		{
			printf("get_ppn()\tERROR :there is no free page in channel:0, chip:0, die:0, plane:0\n");
			getchar();
			return NULL;
		}

		active_superblock = ssd->active_block; //获取当前超级块号
		if (ssd->band_head[active_superblock].pe_cycle % 100 == 0) { // 检测频率
			// 更新磨损块表，调整条带组织
			update_block_wear_state(ssd, active_superblock, ssd->band_head[active_superblock].pe_cycle - 100);
		}

		wear_num = 0;
		high_wear_num = 0;
		idle_num = 0;
		healthy_num = 0;
		wear_flag = 0;
		high_wear_flag = 0; 
		idle_flag = 0;
		healthy_flag = 0;
		for (int i = 0; i < BAND_WITDH; i++)
		{
			switch (ssd->dram->wear_map->wear_map_entry[i * ssd->parameter->block_plane + active_superblock].wear_state) {
				case 1:
				{
					wear_num++;
					wear_flag |= 1ll << i;
					break;
				}
				case 2:
				{
					high_wear_num++;
					high_wear_flag |= 1ll << i;
					// 高磨损块不予分配，成为闲置块
					ssd->dram->wear_map->wear_map_entry[i * ssd->parameter->block_plane + active_superblock].wear_state = 3;
					break;
				}
				case 3:
				{
					idle_num++;
					idle_flag |= 1ll << i;
					break;
				}
				default: {
				}
			}
		}

		healthy_num = BAND_WITDH - wear_num - high_wear_num - idle_num;
		healthy_flag = ~(idle_flag | wear_flag | high_wear_flag);
		//没有磨损块时默认选择最后一块健康块作为校验块
		if (wear_num == 0)
		{
			wear_num = 1;
			int index;
			for (index = BAND_WITDH - 1; index >= 0; index--) {
				if ((healthy_flag | (1ll << index)) == healthy_flag) {
					wear_flag |= 1ll << index;
					break;
				}
			}
			healthy_num--;
			healthy_flag &= ~(1ll << index);
		}
		// 健康块少于磨损块，无法使用WARD组织，
		if (healthy_num < wear_num) {
			// 弃用该超级块，使用下个超级块
			ssd->active_block++;
			ssd->band_head[active_superblock].bad_flag = 1;
			ssd->free_superblock_num--;
			continue;
		}
		// 使用该超级块，进入条带组织
		break;
	}

	int now_length = healthy_num + wear_num; //高磨损块不参与条带组织
	for (int i = 0; i < wear_num; i++) {
		int parity_state = 0;
		int band_width = (now_length + wear_num - i - 1) / (wear_num - i); // 向上取整
		//printf("band_width:%d\n", band_width);
		now_length -= band_width;
		for (int j = 0; j < band_width - 1; j++) {
			int pos = find_first_one(ssd, healthy_flag);
			healthy_flag &= ~(1ll << pos);

			pt = ssd->dram->buffer->buffer_tail;
			//从二叉树中删除该节点
			avlTreeDel(ssd->dram->buffer, (TREE_NODE*)pt);
			// 从LRU中删除该节点
			if (ssd->dram->buffer->buffer_head->LRU_link_next == NULL)
			{
				ssd->dram->buffer->buffer_head = NULL;
				ssd->dram->buffer->buffer_tail = NULL;
			}
			else
			{
				ssd->dram->buffer->buffer_tail = ssd->dram->buffer->buffer_tail->LRU_link_pre;
				ssd->dram->buffer->buffer_tail->LRU_link_next = NULL;
			}
			pt->LRU_link_next = NULL;
			pt->LRU_link_pre = NULL;
			//将该节点的存放在superpage中
			ssd->dram->superpage_buffer[pos].state = pt->stored;
			ssd->dram->superpage_buffer[pos].size = size(pt->stored);
			ssd->dram->superpage_buffer[pos].lpn = pt->group;
			ssd->dram->buffer->buffer_sector_count -= size(pt->stored);
			//计算校验数据的请求状态
			parity_state |= ssd->dram->superpage_buffer[pos].state;
			//释放该节点
			AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE*)pt);
			pt = NULL;
			//修改buffer中的缓冲区缓存的sector数
		}
		int parity_pos = find_first_one(ssd, wear_flag);
		wear_flag &= ~(1ll << parity_pos);
		ssd->dram->superpage_buffer[parity_pos].lpn = -2;
		ssd->dram->superpage_buffer[parity_pos].size = size(parity_state);
		ssd->dram->superpage_buffer[parity_pos].state = parity_state;
	}


	for(i = 0; i < ssd->parameter->channel_number; i++)
	{
		for(j = 0; j < ssd->parameter->chip_channel[0]; j++)
		{
			for(k = 0; k < ssd->parameter->die_chip; k++)
			{
				for(l = 0; l < ssd->parameter->plane_die; l++)
				{
					ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[active_superblock].last_write_page++;
					ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[active_superblock].free_page_num--;
				}
			}
		}
	}
	active_superpage = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page;
	
	
	if(active_superpage>=(int)(ssd->parameter->page_block))
	{
		ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page=0;
		printf("insert2buffer();   error! the last write page larger than 64!!\n");
		return NULL;
	}

	//为整个超级页创建请求
	unsigned long long write_flag = idle_flag | high_wear_flag;
	for(i = 0; i < wear_num + healthy_num; i++)
	{
		int write_pos = find_first_zero(ssd, write_flag);
		write_flag |= 1ll << write_pos;
		sub_req=NULL;
		sub_req_state = ssd->dram->superpage_buffer[write_pos].state;
		sub_req_size = ssd->dram->superpage_buffer[write_pos].size;
		sub_req_lpn = ssd->dram->superpage_buffer[write_pos].lpn;
		ssd->write_need_space += healthy_num;
		ssd->write_used_space += BAND_WITDH;

		if((sub_req = creat_write_sub_request(ssd, sub_req_lpn, sub_req_size, sub_req_state, req, write_pos, active_superblock, active_superpage)) == NULL)
		{
			printf("insert2buffer()\tWrite subrequest creation failed!\n");
			return NULL;
		}
		if(req == NULL && sub != NULL)                                                
		{
			sub_req->next_subs = sub->next_subs;
			sub->next_subs = sub_req;
		}
		write_flash_sub_count++;
	}

	ssd->write_time = ssd->current_time + CODE_LAT;
	memset(ssd->dram->superpage_buffer, 0, BAND_WITDH * sizeof(struct sub_request));
	return ssd;
}

// 根据uper更新磨损块表
struct ssd_info* update_block_wear_state(struct ssd_info* ssd, int active_superblock, int pe_cycle) {
	unsigned int channel, chip, die, plane, page;
	for (channel = 0; channel < ssd->parameter->channel_number; channel++) {
		for (chip = 0; chip < ssd->parameter->chip_channel[0]; chip++) {
			for (die = 0; die < ssd->parameter->die_chip; die++) {
				for (plane = 0; plane < ssd->parameter->plane_die; plane++) {
					int block_id = (((channel * ssd->parameter->chip_channel[0] + chip) * ssd->parameter->die_chip + die) * ssd->parameter->plane_die + plane) * ssd->parameter->block_plane + active_superblock;
					double rber = ssd->dram->rber_table[block_id * 61 + pe_cycle / 100];
					if (rber >= 0.005 && rber < 0.006) {
						if (ssd->dram->wear_map->wear_map_entry[block_id].wear_state == 0)
							ssd->dram->wear_map->wear_map_entry[block_id].wear_state = 1;
					}
					else if (rber >= 0.006) {
						if (ssd->dram->wear_map->wear_map_entry[block_id].wear_state <= 1)
							ssd->dram->wear_map->wear_map_entry[block_id].wear_state = 2;
					}
					
				}
			}
		}
	}
	return ssd;
}

struct sub_request * creat_write_sub_request(struct ssd_info * ssd,unsigned int lpn,int sub_size,unsigned int state,struct request * req, unsigned int pos, unsigned int block, unsigned int page)
{
	struct sub_request* sub=NULL,* sub_r=NULL;
	struct channel_info * p_ch=NULL;
	struct local * loc=NULL;
	unsigned int flag=0;
	unsigned int plane_channel, plane_chip, plane_die;
	unsigned int channel, chip, die, plane;
	struct sub_request *update = NULL;
	struct local *location = NULL;
	unsigned int old_ppn;

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
	
	//计算出该请求要分配到的位置
	plane_die = ssd->parameter->plane_die;
	plane_chip = plane_die * ssd->parameter->die_chip;
	plane_channel = plane_chip * ssd->parameter->chip_channel[0];

	/*channel = pos / plane_channel;
	chip = (pos % plane_channel) / plane_chip;
	die = (pos % plane_chip) / plane_die;
	plane = pos % plane_die;*/
	channel = pos % 4;
	chip = (pos / (2 * 2 * 4)) % 4;
	die = (pos / 4) % 2;
	plane = (pos / (2 * 4)) % 2;

    //sub的location                       
	sub->location=(struct local *)malloc(sizeof(struct local));
	alloc_assert(sub->location,"sub->location");
	memset(sub->location,0, sizeof(struct local));
	sub->location->channel = channel;
	sub->location->chip = chip;
	sub->location->die = die;
	sub->location->plane = plane;
	sub->location->block = block;
	sub->location->page = page;

	//sub的其他字段
	sub->ppn = find_ppn(ssd, channel, chip, die, plane, block, page);
	sub->operation = WRITE;
	sub->current_state=SR_WAIT;
	sub->current_time=ssd->current_time;
	sub->lpn=lpn;
	sub->size=sub_size;
	sub->state=state;
	sub->begin_time = ssd->current_time;
#ifdef CALCULATION
	sub->begin_time += CODE_LAT;
#endif

	//把该请求插入到req的sub队列头
	if(req!=NULL)
	{
		sub->next_subs = req->subs;
		req->subs = sub;
	}
	/*
	if(pos == 0)
	{
		if(req!=NULL)
		{
			sub->next_subs = req->subs;
			req->subs = sub;
		}
	}*/
	ssd->write_sub_request++;

	//查看这个写请求是否产生更新请求
	if(lpn != -2 && lpn!=-3)
	{
		if (ssd->dram->map->map_entry[sub->lpn].state!=0)    
		{
			
			//先断开旧页的ppn_2_lpn的映射关系
			old_ppn = ssd->dram->map->map_entry[sub->lpn].pn;
			location = find_location(ssd, old_ppn);  //获取旧页的位置
			if(ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn != -1)
			{
				//ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=0;             
				//ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=0;             
				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=-3;
				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;
			}
			if(location != NULL)
			{
				free(location);
				location = NULL;
			}
			
			//将要写入的子页不包含当前物理页中的所有子页，则产生更新请求
			if ((sub->state&ssd->dram->map->map_entry[sub->lpn].state)!=ssd->dram->map->map_entry[sub->lpn].state)
			{
				ssd->read_count++;
				ssd->update_read_count++;
				ssd->read_sub_request++;

				//产生新的读请求，并且挂到channel的subs_r_tail队列尾
				update=(struct sub_request *)malloc(sizeof(struct sub_request));
				alloc_assert(update,"update");
				memset(update,0, sizeof(struct sub_request));

				if(update==NULL)
				{
					return NULL;
				}
				update->location=NULL;
				update->next_node=NULL;
				update->next_subs=NULL;
				update->update=NULL;						
				location = find_location(ssd,ssd->dram->map->map_entry[sub->lpn].pn);
				update->location=location;
				update->begin_time = ssd->current_time;
				update->current_state = SR_WAIT;
				update->current_time=0x7fffffffffffffff;
				update->next_state = SR_R_C_A_TRANSFER;
				update->next_state_predict_time=0x7fffffffffffffff;
				update->lpn = sub->lpn;
				update->state=((ssd->dram->map->map_entry[sub->lpn].state^sub->state)&0x7fffffff);
				update->size=size(update->state);
				update->ppn = ssd->dram->map->map_entry[sub->lpn].pn;
				update->operation = READ;

				if (ssd->channel_head[location->channel].subs_r_tail!=NULL)            
				{
					ssd->channel_head[location->channel].subs_r_tail->next_node=update;
					ssd->channel_head[location->channel].subs_r_tail=update;
				} 
				else
				{
					ssd->channel_head[location->channel].subs_r_tail=update;
					ssd->channel_head[location->channel].subs_r_head=update;
				}
			}
		}
		// 将更新请求挂到sub的update队列上
		if (update!=NULL)
		{
			sub->update=update;

			sub->state=(sub->state|update->state);
			sub->size=size(sub->state);
		}
	}
	
	// 将写子请求挂到对应的通道上
	if (ssd->channel_head[sub->location->channel].subs_w_tail!=NULL)
	{
		ssd->channel_head[sub->location->channel].subs_w_tail->next_node=sub;
		ssd->channel_head[sub->location->channel].subs_w_tail=sub;
	} 
	else
	{
		ssd->channel_head[sub->location->channel].subs_w_tail=sub;
		ssd->channel_head[sub->location->channel].subs_w_head=sub;
	}

	return sub;
}
struct sub_request *creat_read_sub_request(struct ssd_info *ssd, unsigned int lpn, int size, struct request *req)
{
	struct sub_request* sub=NULL,* sub_r=NULL;
	struct channel_info * p_ch=NULL;
	struct local * loc=NULL;
	unsigned int flag=0;

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

	//把该请求插入到req的sub队列头
	
	/*************************************************************************************
	*在读操作的情况下，有一点非常重要就是要预先判断读子请求队列中是否有与这个子请求相同的，
	*有的话，新子请求就不必再执行了，将新的子请求直接赋为完成
	*没有的话，将子请求挂载到对应channel的读请求队列的尾部
	**************************************************************************************/
	if(ssd->dram->map->map_entry[lpn].pn == -1)
	{
		printf("Error, the ppn of the read subrequest is -1\n");
		getchar();
		return NULL;
	}

	loc = find_location(ssd,ssd->dram->map->map_entry[lpn].pn);
	sub->location = loc;
	sub->begin_time = ssd->current_time;
	sub->current_state = SR_WAIT;
	sub->current_time = 0x7fffffffffffffff;
	sub->next_state = SR_R_C_A_TRANSFER;
	sub->next_state_predict_time = 0x7fffffffffffffff;
	sub->lpn = lpn;
	sub->size = size;                                                               /*需要计算出该子请求的请求大小*/
	sub->ppn = ssd->dram->map->map_entry[lpn].pn;
	sub->operation = READ;
	sub->state = (ssd->dram->map->map_entry[lpn].state & 0x7fffffff);
	sub->type = FAULT_SUB;
	if(req != NULL)
	{
		sub->next_subs = req->subs;
		req->subs = sub;
	}
#ifdef RECOVERY
#ifdef ACTIVE_RECOVERY

#else
	//当读闪存遇到闪存页读错误时，则需要降级读取,将该请求记录在恢复操作数据结构中，不需将该请求挂在通道读子请求队列中
#ifdef BROKEN_PAGE
	if(ssd->channel_head[loc->channel].chip_head[loc->chip].die_head[loc->die].plane_head[loc->plane].blk_head[loc->block].page_head[loc->page].bad_page_flag == TRUE)
#endif
#ifdef BROKEN_BLOCK
	if(ssd->channel_head[loc->channel].chip_head[loc->chip].die_head[loc->die].plane_head[loc->plane].blk_head[loc->block].bad_block_flag == TRUE)
#endif
	{
		//printf("...............................................................Start recovering data!\n");
		//printf("lpn = %d\n\n", sub->lpn);


		ssd = creat_read_sub_for_recover_page(ssd, sub, req);
		/*free(sub->location);
		sub->location=NULL;
		free(sub);
		sub=NULL;*/
		return sub;
	}
#endif
#endif


	p_ch = &ssd->channel_head[loc->channel];
	sub_r = p_ch->subs_r_head;                                                      
	/***********************************************************************
	*以下几行包括flag用于判断该读子请求队列中是否有与这个子请求相同的，
	*1. 有的话，将新的子请求直接赋为完成
	*2. 没有的话，将子请求挂载到对应channel的读请求队列的尾部
	************************************************************************/
	flag = 0;
	while(sub_r!=NULL)
	{
		//如果在通道的请求队列中找到该请求，则flag标志为1
		if (sub_r->ppn == sub->ppn)
		{
			flag = 1;  
			break;
		}
		sub_r = sub_r->next_node;
	}
	//若未找到，则将该读子请求添加到相应通道的读子请求队列尾，若找到，则直接服务该读子请求
	if (flag == 0)
	{
		if (p_ch->subs_r_tail != NULL)
		{
			p_ch->subs_r_tail->next_node = sub;
			p_ch->subs_r_tail = sub;
		} 
		else
		{
			p_ch->subs_r_head = sub;
			p_ch->subs_r_tail = sub;
		}
		ssd->read_sub_request++;  //记录读闪存的子请求数
	}
	else
	{
		sub->current_state = SR_R_DATA_TRANSFER;
		sub->current_time = ssd->current_time;
		sub->next_state = SR_COMPLETE;
		sub->next_state_predict_time = ssd->current_time + 1000;
		sub->complete_time = ssd->current_time + 1000;
	}
	return sub;
}

/**************************************************************************************
*函数的功能是寻找活跃快，应为每个plane中都只有一个活跃块，只有这个活跃块中才能进行操作
***************************************************************************************/
Status  find_superblock_for_write(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane, struct request *req)
{
	unsigned int active_block=0;
	unsigned int free_page_num=0;
	unsigned int count=0;
	unsigned int free_superblock_num = 0, i;
	active_block=ssd->active_block;
	if (ssd->dram->wear_map->wear_map_entry[active_block].wear_state >= 2) {
		for (channel = 0; channel < ssd->parameter->channel_number; channel++) {
			for (chip = 0; chip < ssd->parameter->chip_channel[0]; chip++) {
				for (die = 0; die < ssd->parameter->die_chip; die++) {
					for (plane = 0; plane < ssd->parameter->plane_die; plane++) {
						int block_id = (((channel * ssd->parameter->chip_channel[0] + chip) * ssd->parameter->die_chip + die) * ssd->parameter->plane_die + plane) * ssd->parameter->block_plane + active_block;
						if (ssd->dram->wear_map->wear_map_entry[block_id].wear_state < 2)
							goto label;
					}
				}
			}
		}
	}
label:
	//last_write_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
	//寻找active_block(从当前active_block开始，顺序查找，找到第一个free_page_num不为0的block)
	free_page_num = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
	while((free_page_num == 0 || ssd->band_head[active_block].bad_flag == 1)&&(count<ssd->parameter->block_plane))
	{
		active_block=(active_block+1)%ssd->parameter->block_plane;	
		free_page_num=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
		count++;
	}
	ssd->active_block=active_block;
	
	if(count < ssd->parameter->block_plane)
	{
		//当寻找到新的superblock时，检测空闲的superblock是否满足GC要求
		if(count != 0)
		{
			ssd->free_superblock_num--;
			//统计空闲superblock的个数
			/*
			for(i = 0; i < ssd->parameter->block_plane; i++)
			{
				if(ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[i].last_write_page == -1)
				{
					free_superblock_num++;
				}
			}*/
			// 空闲的superblock数小于设置的gc阈值时，进行GC操作
			if(ssd->free_superblock_num < ssd->parameter->block_plane * ssd->parameter->gc_hard_threshold)
			{
				printf("GC\n");
				if(gc_for_superblock(ssd, req) == FAILURE)
				{
					printf("GC is FAILURE\n");
					getchar();
					return FAILURE;
				}
			}
		}
		return SUCCESS;
	}
	else
	{
		return FAILURE;
	}
}

Status  find_superblock_for_pre_read(struct ssd_info* ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int plane)
{
	unsigned int active_block = 0;
	unsigned int free_page_num = 0;
	unsigned int count = 0;
	active_block = ssd->active_block;
	if (ssd->dram->wear_map->wear_map_entry[active_block].wear_state >= 2) {
		for (channel = 0; channel < ssd->parameter->channel_number; channel++) {
			for (chip = 0; chip < ssd->parameter->chip_channel[0]; chip++) {
				for (die = 0; die < ssd->parameter->die_chip; die++) {
					for (plane = 0; plane < ssd->parameter->plane_die; plane++) {
						int block_id = (((channel * ssd->parameter->chip_channel[0] + chip) * ssd->parameter->die_chip + die) * ssd->parameter->plane_die + plane) * ssd->parameter->block_plane + active_block;
						if (ssd->dram->wear_map->wear_map_entry[block_id].wear_state < 2)
							goto label;
					}
				}
			}
		}
	}
	label:
	free_page_num=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
	//last_write_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
	//寻找active_block(从当前active_block开始，顺序查找，找到第一个free_page_num不为0的block)
	while((free_page_num == 0)&&(count<ssd->parameter->block_plane))
	{
		ssd->active_block = (ssd->active_block +1)%ssd->parameter->block_plane;
		free_page_num=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[ssd->active_block].free_page_num;
		count++;
	}
	
	if(count < ssd->parameter->block_plane)
	{
		if(count != 0)
		{
			ssd->free_superblock_num--;
		}
		return SUCCESS;
	}
	else
	{
		return FAILURE;
	}
}

Status  find_active_block(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane)
{
	unsigned int active_block=0;
	unsigned int free_page_num=0;
	unsigned int count=0;
	active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	free_page_num=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
	//last_write_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
	//寻找active_block(从当前active_block开始，顺序查找，找到第一个free_page_num不为0的block)
	while((free_page_num == 0)&&(count<ssd->parameter->block_plane))
	{
		active_block=(active_block+1)%ssd->parameter->block_plane;	
		free_page_num=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
		count++;
	}
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block=active_block;

	if(count < ssd->parameter->block_plane)
	{
		return SUCCESS;
	}
	else
	{
		return FAILURE;
	}
}


/*************************************************
*这个函数的功能就是一个模拟一个实实在在的写操作
*就是更改这个page的相关参数，以及整个ssd的统计参数
**************************************************/
Status write_page(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,unsigned int active_block,unsigned int *ppn)
{
	//这里不对last_write_page和free_page_num做出改变，在之前已经统一修改
	int last_write_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page;	
	if(last_write_page>=(int)(ssd->parameter->page_block))
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page=0;
		printf("error! the last write page larger than 64!!\n");
		return ERROR;
	}

	//ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--; 
	//ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[last_write_page].written_count++;
	//ssd->write_flash_count++;    
	*ppn=find_ppn(ssd,channel,chip,die,plane,active_block,last_write_page);

	return SUCCESS;
}

/**********************************************************
*这个函数的功能是根据lpn，size，state创建子请求
*并将子请求挂载到对应channel的读请求队列或写请求队列的队尾
***********************************************************/
struct sub_request * creat_sub_request(struct ssd_info * ssd,unsigned int lpn,int size,unsigned int state,struct request * req,unsigned int operation)
{
	struct sub_request* sub=NULL,* sub_r=NULL;
	struct channel_info * p_ch=NULL;
	struct local * loc=NULL;
	unsigned int flag=0;

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
	//把该请求插入到req的sub队列头
	if(req!=NULL)
	{
		sub->next_subs = req->subs;
		req->subs = sub;
	}
	
	/*************************************************************************************
	*在读操作的情况下，有一点非常重要就是要预先判断读子请求队列中是否有与这个子请求相同的，
	*有的话，新子请求就不必再执行了，将新的子请求直接赋为完成
	*没有的话，将子请求挂载到对应channel的读请求队列的尾部
	**************************************************************************************/
	if (operation == READ)
	{	

		loc = find_location(ssd,ssd->dram->map->map_entry[lpn].pn);
		sub->location=loc;
		sub->begin_time = ssd->current_time;
		sub->current_state = SR_WAIT;
		sub->current_time=0x7fffffffffffffff;
		sub->next_state = SR_R_C_A_TRANSFER;
		sub->next_state_predict_time=0x7fffffffffffffff;
		sub->lpn = lpn;
		sub->size=size;                                                               /*需要计算出该子请求的请求大小*/

		p_ch = &ssd->channel_head[loc->channel];	
		sub->ppn = ssd->dram->map->map_entry[lpn].pn;
		sub->operation = READ;
		sub->state=(ssd->dram->map->map_entry[lpn].state&0x7fffffff);
		sub_r=p_ch->subs_r_head;                                                      
		/***********************************************************************
		*以下几行包括flag用于判断该读子请求队列中是否有与这个子请求相同的，
		*有的话，将新的子请求直接赋为完成
		*没有的话，将子请求挂载到对应channel的读请求队列的尾部
		************************************************************************/
		flag=0;
		while (sub_r!=NULL)
		{
			if (sub_r->ppn==sub->ppn)
			{
				flag=1;
				break;
			}
			sub_r=sub_r->next_node;
		}
		if (flag==0)
		{
			if (p_ch->subs_r_tail!=NULL)
			{
				p_ch->subs_r_tail->next_node=sub;
				p_ch->subs_r_tail=sub;
			} 
			else
			{
				p_ch->subs_r_head=sub;
				p_ch->subs_r_tail=sub;
			}
#ifdef USE_WHITE_PARITY
		ssd->read_sub_request++;
#endif 
		}
		else
		{
			sub->current_state = SR_R_DATA_TRANSFER;
			sub->current_time=ssd->current_time;
			sub->next_state = SR_COMPLETE;
			sub->next_state_predict_time=ssd->current_time+1000;
			sub->complete_time=ssd->current_time+1000;
		}
	}
	/*************************************************************************************
	*写请求的情况下，就需要利用到函数allocate_location(ssd ,sub)来处理静态分配和动态分配了
	*将写请求挂到对应channel的写请求队列的队尾的功能也在allocate_location(ssd ,sub)中完成
	**************************************************************************************/
	else if(operation == WRITE)
	{                                
		sub->ppn=0;
		sub->operation = WRITE;
		sub->location=(struct local *)malloc(sizeof(struct local));
		alloc_assert(sub->location,"sub->location");
		memset(sub->location,0, sizeof(struct local));
#ifdef USE_WHITE_PARITY
		ssd->write_sub_request++;
#endif
		sub->current_state=SR_WAIT;
		sub->current_time=ssd->current_time;
		sub->lpn=lpn;
		sub->size=size;
		sub->state=state;
		sub->begin_time=ssd->current_time;
      
		if (allocate_location(ssd ,sub)==ERROR)//分配位置出错  free掉该请求的相关信息。
		{
			free(sub->location);
			sub->location=NULL;
			free(sub);
			sub=NULL;
			return NULL;
		}
			
	}
	else//既不是读请求，也不是写请求   ，free掉该请求信息。不做任何处理。
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

/******************************************************
*函数的功能是在给出的channel，chip，die上面寻找读子请求
*这个子请求的ppn要与相应的plane的寄存器里面的ppn相符
*******************************************************/
struct sub_request * find_read_sub_request(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die)
{
	unsigned int plane=0;
	unsigned int address_ppn=0;
	struct sub_request *sub=NULL,* p=NULL;

	for(plane=0;plane<ssd->parameter->plane_die;plane++)
	{
		address_ppn=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].add_reg_ppn;
		/*if(address_ppn == 917937)
			printf("error\n");*/
		if(address_ppn!=-1)
		{
			sub=ssd->channel_head[channel].subs_r_head;
			if(sub == NULL)
				continue;
			if(sub->ppn==address_ppn)
			{
				return sub;
			}
			while((sub->ppn!=address_ppn)&&(sub->next_node!=NULL))
			{
				if(sub->next_node->ppn==address_ppn)
				{
					return sub->next_node;
				}
				sub=sub->next_node;
			}
		}
	}
	return NULL;
}

/*******************************************************************************
*函数的功能是寻找写子请求。
*分两种情况1，要是是完全动态分配就在ssd->subs_w_head队列上找
*2，要是不是完全动态分配那么就在ssd->channel_head[channel].subs_w_head队列上查找
********************************************************************************/
struct sub_request * find_write_sub_request(struct ssd_info * ssd, unsigned int channel)
{
	struct sub_request * sub=NULL,* p=NULL;
	struct sub_request*sub_r = NULL,*sub_gc = NULL;
	unsigned int break_flag = 0;
	unsigned int update_flag =0;
	unsigned int read_old_flag =0;
	
	if ((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==0))    /*是完全的动态分配*/
	{
		sub=ssd->subs_w_head;
#ifdef USE_WHITE_PARITY
		//该通道的写子请求队列队头请求是校验写请求，直接返回该请求
		/*if(ssd->channel_head[channel].subs_w_head != NULL)
			if(ssd->channel_head[channel].subs_w_head->lpn = -2)
			{
				sub = ssd->channel_head[channel].subs_w_head;
				return sub ;
			}
		*/
		//由于get_ppn的不正确写入，导致通道上的写子请求积累，则无论是校验数据还是原数据都先在通道的写子请求队列上查询，若未积累请求，则从ssd的写子请求队列中查找
		if(ssd->channel_head[channel].subs_w_head != NULL){
			sub = ssd->channel_head[channel].subs_w_head;
			return sub;
		}
#ifdef GC
		if((ssd->request_queue!=NULL))//(ssd->gc_request < ssd->parameter->channel_number*5)&&
			if(sub != NULL) //ssd的写子请求队列有写请求
			{
				//循环查找写子请求队列，找到第一个不为GC请求的写子请求
				while(sub->type == GC_SUB)
				{
					p = sub ;
					sub = sub->next_node;
					if(sub == NULL)
					{
						sub = ssd->subs_w_head;
						break;
					}
				}
				//找到了不为GC的写子请求，则将sub提到队首，head接到tail；
				if(sub != ssd->subs_w_head)
				{
					p->next_node = NULL;
					ssd->subs_w_tail->next_node = ssd->subs_w_head;
					ssd->subs_w_head = sub ;
					ssd->subs_w_tail = p;
				}
			}

#endif
#endif
		

		while(sub!=NULL)        							
		{// 查找current_state为SR_WAIT，且没有更新操作的写子请求
#ifdef USE_WHITE_PARITY
			if(sub->type == GC_SUB)
			{
				if(ssd->dram->map->map_entry[sub->lpn].pn != 0)//说明该请求被别的请求处理了
				{
					//删除该写请求，
#ifdef USE_WHITE_PARITY
					ssd->write_sub_request--;
#endif
					if (sub!=ssd->subs_w_head)
					{
						if (sub!=ssd->subs_w_tail)
						{
							p->next_node=sub->next_node;
						}
						else
						{
							ssd->subs_w_tail=p;
							ssd->subs_w_tail->next_node=NULL;
						}
					} 
					else
					{
						if (sub->next_node!=NULL)
						{
							ssd->subs_w_head=sub->next_node;
						} 
						else
						{
							ssd->subs_w_head=NULL;
							ssd->subs_w_tail=NULL;
						}
					}
					sub_gc = sub->next_node;

					free(sub->location);
					sub->location = NULL;
					free(sub);
					sub = NULL;
					if(sub_gc != NULL)
						sub = sub_gc;
					else
					{
						return NULL;
					}
				}
			}
#endif
			if(sub->current_state==SR_WAIT)								
			{
				if (sub->update!=NULL)                                                      /*如果有需要提前读出的页*/
				{
					if ((sub->update->current_state==SR_COMPLETE)||((sub->update->next_state==SR_COMPLETE)&&(sub->update->next_state_predict_time<=ssd->current_time)))   //被更新的页已经被读出
					{
						break;
					}
				} 
				else
				{
					break;
				}						
			}
			p=sub;
			sub=sub->next_node;							
		}

		if (sub==NULL)                                                                      /*如果没有找到可以服务的子请求，返回NULL*/
		{
			return NULL;
		}

		// 将sub从ssd->subs_w_head队列中移出
		if (sub!=ssd->subs_w_head)
		{
			if (sub!=ssd->subs_w_tail)
			{
				p->next_node=sub->next_node;
			}
			else
			{
				ssd->subs_w_tail=p;
				ssd->subs_w_tail->next_node=NULL;
			}
		} 
		else
		{
			if (sub->next_node!=NULL)
			{
				ssd->subs_w_head=sub->next_node;
			} 
			else
			{
				ssd->subs_w_head=NULL;
				ssd->subs_w_tail=NULL;
			}
		}
		sub->next_node=NULL;

		// 将sub添加到channel下写子请求队列的队尾
		if (ssd->channel_head[channel].subs_w_tail!=NULL)
		{
			ssd->channel_head[channel].subs_w_tail->next_node=sub;
			ssd->channel_head[channel].subs_w_tail=sub;
		} 
		else
		{
			ssd->channel_head[channel].subs_w_tail=sub;
			ssd->channel_head[channel].subs_w_head=sub;
		}
	}
	/**********************************************************
	*除了全动态分配方式，其他方式的请求已经分配到特定的channel，
	*就只需要在channel上找出准备服务的子请求
	***********************************************************/
	else            
	{
		sub=ssd->channel_head[channel].subs_w_head;

		while(sub!=NULL)        						
		{		
			if(sub->lpn == 1825889)
				printf("");
			if(sub->current_state==SR_WAIT)								
			{// 查找current_state为SR_WAIT，且没有更新操作的写子请求
				update_flag = 1;
				read_old_flag = 1;
				if (sub->update!=NULL)    
				{
					update_flag = 0;
					if ((sub->update->current_state==SR_COMPLETE)||((sub->update->next_state==SR_COMPLETE)&&(sub->update->next_state_predict_time<=ssd->current_time)))   //被更新的页已经被读出
					{
#ifdef USE_BLACK_PARITY
						update_flag = 1;
#else
						break;
#endif 
					}
				} 
				#ifndef PARITY_BEFORE_SUB
#ifdef USE_BLACK_PARITY
				if(sub->read_old != NULL)
				{
					sub_r = sub;
					while(sub_r->read_old != NULL)
					{
						read_old_flag = 0;
						if((sub_r->read_old->current_state == SR_COMPLETE)||((sub_r->read_old->next_state == SR_COMPLETE)&&(sub_r->read_old->next_state_predict_time <= ssd->current_time)))
						{
							sub_r = sub_r->read_old;
							read_old_flag = 1 ;
							/*if(sub_r == NULL)
								break;*/
						}
						else
							break;
					}
				}
#endif 
				#endif
#ifdef USE_BLACK_PARITY
				if((update_flag)&&(read_old_flag))
					break;
#else
				else
				{
					break;
				}		
#endif 
			}
			p=sub;
			sub=sub->next_node;	
			break_flag = 0;
		}

		if (sub==NULL)
		{
			return NULL;
		}
	}
	
	return sub;
}

/*********************************************************************************************
*专门为读子请求服务的函数
*1，读子请求的当前状态是SR_R_C_A_TRANSFER
*2，读子请求的当前状态是SR_COMPLETE或者下一状态是SR_COMPLETE并且下一状态到达的时间比当前时间小
**********************************************************************************************/
Status services_2_r_cmd_trans_and_complete(struct ssd_info * ssd)
{
	unsigned int i=0;
	struct sub_request * sub=NULL, * p=NULL;
	int delete_flag = 0;
#ifdef RECOVERY
	struct recovery_operation* rec = NULL;
	unsigned int flag;
	long long recovery_time;
	unsigned int pos;
#ifdef ACTIVE_RECOVERY
	unsigned int j;
#endif
#endif
	for(i=0;i<ssd->parameter->channel_number;i++)		/*这个循环处理不需要channel的时间(读命令已经到达chip，chip由ready变为busy)，当读请求完成时，将其从channel的队列中取出*/
	{
		sub=ssd->channel_head[i].subs_r_head;

		while(sub!=NULL)
		{
			delete_flag = 0;
			if(sub->current_state==SR_R_C_A_TRANSFER)		/*读命令发送完毕，将对应的die置为busy，同时修改sub的状态; 这个部分专门处理读请求由当前状态为传命令变为die开始busy，die开始busy不需要channel为空，所以单独列出*/
			{
				if(sub->next_state_predict_time<=ssd->current_time)
				{
					go_one_step(ssd, sub,NULL, SR_R_READ,NORMAL);		/*状态跳变处理函数*/

				}
			}
			else if((sub->current_state==SR_COMPLETE)||((sub->next_state==SR_COMPLETE)&&(sub->next_state_predict_time<=ssd->current_time)))					
			{// 读子请求已经完成，将其从channel的读请求队列中删除
				if(sub!=ssd->channel_head[i].subs_r_head)		/*if the request is completed, we delete it from read queue */							
				{		
					if (sub->next_node == NULL) {
						p->next_node = NULL;
						ssd->channel_head[i].subs_r_tail = p;
					}
					else {
						p->next_node = sub->next_node;
						delete_flag = 1;
					}
				}			
				else					
				{	
					if (ssd->channel_head[i].subs_r_head!=ssd->channel_head[i].subs_r_tail)
					{
						ssd->channel_head[i].subs_r_head=sub->next_node;
					} 
					else
					{
						ssd->channel_head[i].subs_r_head=NULL;
						ssd->channel_head[i].subs_r_tail=NULL;
					}							
				}
#ifdef RECOVERY
				if (sub->type == RECOVER_SUB)
				{
					rec = ssd->recovery_head;
					//因为阻塞导致某些请求提前完成，则需轮询所有的恢复操作，确定是哪个恢复操作产生的子请求
					while (rec != NULL)
					{
						//和当前需要恢复的闪存页处于同一条带时，将恢复需要的读请求对应的完成标志置1
						if ((flag = is_same_superpage(rec->sub, sub)) == TRUE)
						{
							pos = get_pos_in_band(ssd, sub->ppn);
							rec->sub_r_complete_flag |= 1ll << pos;
							//printf("lpn = %d\tblock_for_recovery = %d\tcomplete_flag = %d\n", rec->sub->lpn, rec->block_for_recovery, rec->sub_r_complete_flag);
							//printf("broken_lpn = %d\trecovery_lpn = %d\tblock_for_recovery = %d\tcomplete_flag = %d\n", rec->sub->lpn, subs[i]->lpn, rec->block_for_recovery, rec->sub_r_complete_flag);
						}
						//如果当前恢复操作所需的读请求都完成了，则进行解码恢复，并将恢复的闪存页写入缓存
						if (rec->sub_r_complete_flag == rec->block_for_recovery)
						{
#ifdef CALCULATION
							recovery_time = sub->complete_time + 82000;
#else
							recovery_time = subs[i]->complete_time;
#endif
							/*printf("the page is recovered already, Write it into flash!\n");
							printf("lpn = %d\n\n", rec->sub->lpn);*/
#ifdef ACTIVE_RECOVERY
							active_write_recovery_page(ssd, rec, recovery_time);
#else
							write_recovery_page(ssd, rec, recovery_time);
#endif
						}
						//如果已经确定请求对应的恢复操作，则跳出循环（读子请求和恢复操作为多对一）
						if (flag == TRUE)
						{
							break;
						}
						rec = rec->next_node;
					}
				}
#endif
			}
			if(!delete_flag) // 连续出现多个要删掉的子请求时p应该为最前面未删除的那个子请求
				p=sub;
			sub=sub->next_node;
		}
	}
	
	return SUCCESS;
}

/**************************************************************************
*这个函数也是只处理读子请求，处理chip当前状态是CHIP_WAIT，
*或者下一个状态是CHIP_DATA_TRANSFER并且下一状态的预计时间小于当前时间的chip
***************************************************************************/
Status services_2_r_data_trans(struct ssd_info * ssd,unsigned int channel,unsigned int * channel_busy_flag, unsigned int * change_current_time_flag)
{
	int chip=0;
	unsigned int die=0,plane=0,address_ppn=0,die1=0;
	struct sub_request * sub=NULL, * p=NULL,*sub1=NULL;
	struct sub_request * sub_twoplane_one=NULL, * sub_twoplane_two=NULL;
	struct sub_request * sub_interleave_one=NULL, * sub_interleave_two=NULL;
	struct sub_request * sub_w=NULL;
	struct gc_operation * gc_node=NULL;
	struct direct_erase *new_direct_erase = NULL,*direct_erase_node= NULL;
	unsigned int break_flag = 0 ;
	for(chip=0;chip<ssd->channel_head[channel].chip;chip++)           			    
	{				       		      
			if((ssd->channel_head[channel].chip_head[chip].current_state==CHIP_WAIT)||((ssd->channel_head[channel].chip_head[chip].next_state==CHIP_DATA_TRANSFER)&&
				(ssd->channel_head[channel].chip_head[chip].next_state_predict_time<=ssd->current_time)))					       					
			{
				for(die=0;die<ssd->parameter->die_chip;die++)
				{
					//在channel chip die 中查询相关子请求，找到子请求后，会将子请求从该通道队列中删除。
					sub=find_read_sub_request(ssd,channel,chip,die);                   /*在channel,chip,die中找到读子请求*/
					if(sub!=NULL)
					{
						break;
					}
				}

				if(sub==NULL)
				{
					continue;
				}
				
				/**************************************************************************************
				*如果ssd支持高级命令，那么我们可以一起处理支持AD_TWOPLANE_READ，AD_INTERLEAVE的读子请求
				*1，有可能产生了two plane操作，在这种情况下，将同一个die上的两个plane的数据依次传出
				*2，有可能产生了interleave操作，在这种情况下，将不同die上的两个plane的数据依次传出
				*3，有可能产生了组合操作，在这种情况下，将不同die上的四个plane的数据依次传出
				***************************************************************************************/
				if (((ssd->parameter->advanced_commands & AD_TWOPLANE_READ) == AD_TWOPLANE_READ) && ((ssd->parameter->advanced_commands & AD_INTERLEAVE) == AD_INTERLEAVE)) {
					sub_twoplane_one = sub;
					sub_twoplane_two = NULL;
					/*为了保证找到的sub_twoplane_two与sub_twoplane_one不同，令add_reg_ppn=-1*/
					ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[sub->location->plane].add_reg_ppn = -1;
					sub_twoplane_two = find_read_sub_request(ssd, channel, chip, die);               /*在相同的channel,chip,die中寻找另外一个读子请求*/
					for (die1 = 0; die1 < ssd->parameter->die_chip; die1++)
					{
						if (die1 != die)
						{
							sub_interleave_one = find_read_sub_request(ssd, channel, chip, die1);    /*在相同的channel、chip，不同的die上面找另外一个读子请求*/
							if (sub_interleave_one != NULL)
							{
								break;
							}
						}
					}
					if (sub_interleave_one == NULL) {
						if (sub_twoplane_two == NULL) {
							go_one_step(ssd, sub_twoplane_one, NULL, SR_R_DATA_TRANSFER, NORMAL);

							*change_current_time_flag = 0;
							*channel_busy_flag = 1;
						}
						else
						{
							//printf("Use two_plane_read\n");
							go_one_step(ssd, sub_twoplane_one, sub_twoplane_two, SR_R_DATA_TRANSFER, TWO_PLANE);
							*change_current_time_flag = 0;
							*channel_busy_flag = 1;

						}
					}
					else {
						ssd->channel_head[channel].chip_head[chip].die_head[die1].plane_head[sub_interleave_one->location->plane].add_reg_ppn = -1;
						sub_interleave_two = find_read_sub_request(ssd, channel, chip, die1);               /*在相同的channel,chip,die中寻找另外一个读子请求*/
						if (sub_twoplane_two != NULL || sub_interleave_two != NULL) {
							go_one_step_interleave_twoplane(ssd, sub_twoplane_one, sub_twoplane_two, sub_interleave_one, sub_interleave_two, SR_R_DATA_TRANSFER);
							*change_current_time_flag = 0;
							*channel_busy_flag = 1;
						}
						else {
							go_one_step(ssd, sub_twoplane_one, sub_interleave_one, SR_R_DATA_TRANSFER, INTERLEAVE);
							*change_current_time_flag = 0;
							*channel_busy_flag = 1;
						}
					}
				}



				else if(((ssd->parameter->advanced_commands&AD_TWOPLANE_READ)==AD_TWOPLANE_READ)||((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE))
				{
					if ((ssd->parameter->advanced_commands&AD_TWOPLANE_READ)==AD_TWOPLANE_READ)     /*有可能产生了two plane操作，在这种情况下，将同一个die上的两个plane的数据依次传出*/
					{
						sub_twoplane_one=sub;
						sub_twoplane_two=NULL;                                                      
						                                                                            /*为了保证找到的sub_twoplane_two与sub_twoplane_one不同，令add_reg_ppn=-1*/
						ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[sub->location->plane].add_reg_ppn=-1;
						sub_twoplane_two=find_read_sub_request(ssd,channel,chip,die);               /*在相同的channel,chip,die中寻找另外一个读子请求*/


						/******************************************************
						*如果找到了那么就执行TWO_PLANE的状态转换函数go_one_step
						*如果没找到那么就执行普通命令的状态转换函数go_one_step
						******************************************************/
						if (sub_twoplane_two==NULL)
						{
							if ((ssd->parameter->advanced_commands & AD_INTERLEAVE) == AD_INTERLEAVE)      /*有可能产生了interleave操作，在这种情况下，将不同die上的两个plane的数据依次传出*/
							{
								sub_interleave_one = sub;
								sub_interleave_two = NULL;
								ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[sub->location->plane].add_reg_ppn = -1;

								for (die1 = 0; die1 < ssd->parameter->die_chip; die1++)
								{
									if (die1 != die)
									{
										sub_interleave_two = find_read_sub_request(ssd, channel, chip, die1);    /*在相同的channel、chip，不同的die上面找另外一个读子请求*/
										if (sub_interleave_two != NULL)
										{
											break;
										}
									}
								}
								if (sub_interleave_two == NULL)
								{
									go_one_step(ssd, sub_interleave_one, NULL, SR_R_DATA_TRANSFER, NORMAL);

									*change_current_time_flag = 0;
									*channel_busy_flag = 1;

								}
								else
								{
									go_one_step(ssd, sub_interleave_one, sub_interleave_two, SR_R_DATA_TRANSFER, INTERLEAVE);

									*change_current_time_flag = 0;
									*channel_busy_flag = 1;

								}
							}
							else {
								//printf("Can't use Two_plane_read commmand!\tCan't find the sub request.\n");
								go_one_step(ssd, sub_twoplane_one, NULL, SR_R_DATA_TRANSFER, NORMAL);
								*change_current_time_flag = 0;
								*channel_busy_flag = 1;
							}
						}
						else
						{
							//printf("Use two_plane_read\n");
							go_one_step(ssd, sub_twoplane_one,sub_twoplane_two, SR_R_DATA_TRANSFER,TWO_PLANE);
							*change_current_time_flag=0;  
							*channel_busy_flag=1;

						}
					} 
					else if ((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)      /*有可能产生了interleave操作，在这种情况下，将不同die上的两个plane的数据依次传出*/
					{
						sub_interleave_one=sub;
						sub_interleave_two=NULL;
						ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[sub->location->plane].add_reg_ppn=-1;
						
						for(die1=0;die1<ssd->parameter->die_chip;die1++)
						{	
							if(die1!=die)
							{
								sub_interleave_two=find_read_sub_request(ssd,channel,chip,die1);    /*在相同的channel、chip，不同的die上面找另外一个读子请求*/
								if(sub_interleave_two!=NULL)
								{
									break;
								}
							}
						}	
						if (sub_interleave_two==NULL)
						{
							go_one_step(ssd, sub_interleave_one,NULL, SR_R_DATA_TRANSFER,NORMAL);

							*change_current_time_flag=0;  
							*channel_busy_flag=1;

						}
						else
						{
							go_one_step(ssd, sub_interleave_one,sub_interleave_two, SR_R_DATA_TRANSFER,INTERLEAVE);
												
							*change_current_time_flag=0;   
							*channel_busy_flag=1;
							
						}
					}
				}
				else                                                                                 /*如果ssd不支持高级命令那么就执行一个一个的执行读子请求*/
				{
											
					go_one_step(ssd, sub,NULL, SR_R_DATA_TRANSFER,NORMAL);
					*change_current_time_flag=0;  
					*channel_busy_flag=1;
					
//					#ifdef USE_WHITE_PARITY
//					ssd->read_sub_request--;
//						if(sub->type == GC_SUB)
//						{
//							if(sub->state == 0)
//								printf("sub->state error \n");
//							if(sub->lpn == 4502800)
//								printf("gc read finished sub->lpn == 4502800");
//							//创建新的写请求，此时清除lpn2ppn的映射表和ppn2lpnd的映射表；
//							ssd->dram->map->map_entry[sub->lpn].pn = 0;
//							ssd->dram->map->map_entry[sub->lpn].state = 0;
//							sub_w = creat_sub_request(ssd,sub->lpn,sub->size,sub->state,NULL,WRITE);
//							sub_w->type = GC_SUB;
//
//
//							ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].die_head[sub->location->die].plane_head[sub->location->plane].blk_head[sub->location->block].page_head[sub->location->page].free_state=0;
//							ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].die_head[sub->location->die].plane_head[sub->location->plane].blk_head[sub->location->block].page_head[sub->location->page].lpn=0;
//							ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].die_head[sub->location->die].plane_head[sub->location->plane].blk_head[sub->location->block].page_head[sub->location->page].valid_state=0;
//							ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].die_head[sub->location->die].plane_head[sub->location->plane].blk_head[sub->location->block].invalid_page_num++;
//							if(ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].die_head[sub->location->die].plane_head[sub->location->plane].blk_head[sub->location->block].invalid_page_num == ssd->parameter->page_block)
//							{
//								/*erase_operation(ssd,channel ,chip, die,sub->location->plane,sub->location->block);	
//
//								ssd->channel_head[channel].current_state=CHANNEL_C_A_TRANSFER;									
//								ssd->channel_head[channel].current_time=ssd->current_time;										
//								ssd->channel_head[channel].next_state=CHANNEL_IDLE;								
//								ssd->channel_head[channel].next_state_predict_time=ssd->current_time+5*ssd->parameter->time_characteristics.tWC//考率读完之后才能进行擦除操作。
//																				  +(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;;
//
//								ssd->channel_head[channel].chip_head[chip].current_state=CHIP_ERASE_BUSY;								
//								ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;						
//								ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;							
//								ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tBERS
//																								  +(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;*/
//								direct_erase_node = ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].die_head[sub->location->die].plane_head[sub->location->plane].erase_node;
//								while (direct_erase_node!=NULL)
//								{
//									if(direct_erase_node->block!=sub->location->block)
//										direct_erase_node = direct_erase_node->next_node;
//									else
//									{
//										break_flag =1;
//										break;
//									}
//								}
//								if(break_flag ==0)
//								{
//									new_direct_erase=(struct direct_erase *)malloc(sizeof(struct direct_erase));
//									alloc_assert(new_direct_erase,"new_direct_erase");
//									memset(new_direct_erase,0, sizeof(struct direct_erase));
//
//									if((channel==3)&&(chip==1)&&(die==3)&&(plane==0)&&(sub->location->block==22))
//										channel = 3;
//
//									new_direct_erase->block=sub->location->block;
//									new_direct_erase->next_node=NULL;
//									direct_erase_node=ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].die_head[sub->location->die].plane_head[sub->location->plane].erase_node;
//									ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].die_head[sub->location->die].plane_head[sub->location->plane].can_erase_block++;
//									if (direct_erase_node==NULL)
//									{
//										ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].die_head[sub->location->die].plane_head[sub->location->plane].erase_node=new_direct_erase;
//									} 
//									else
//									{
//										new_direct_erase->next_node=ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].die_head[sub->location->die].plane_head[sub->location->plane].erase_node;
//										ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].die_head[sub->location->die].plane_head[sub->location->plane].erase_node=new_direct_erase;
//									}
//								}
//
//							/*	gc_node=ssd->channel_head[channel].gc_command;
//								while(gc_node != NULL)
//								{
//									if((gc_node->deal == 1)&&(gc_node->chip == chip)&&(gc_node->die == die)&&(gc_node->plane == sub->location->plane))
//									{
//										delete_gc_node(ssd,channel,gc_node);
//										break;
//									}
//									gc_node = gc_node->next_node;
//								}*/
//							}
//							//释放gc读子请求的内存空间。
//							ssd->total_gc_move_page_count++;
//							free(sub->location);
//							sub->location = NULL;
//							free(sub);
//							sub = NULL;
//						}
//#endif 

				}
				break;
			}

			if (*channel_busy_flag == 1)
			{
				break;
			}
	}
	return SUCCESS;
}
/*服务读请求*/
/*
Status statservice_2_read(struct ssd_info *ssd, unsigned int channel, unsigned int *channel_busy_flag, unsigned int *chg_cur_time_flag)
{
	unsigned int chip, die, plane;
	for(chip = 0; chip < ssd->parameter->chip_channel[0]; chip++)
	{
		if(ssd->channel_head[channel].chip_head[chip].current_state == CHIP_IDLE ||
		   (ssd->channel_head[channel].chip_head[chip].next_state == CHIP_IDLE && ssd->channel_head[channel].chip_head[chip].next_state_predict_time <= ssd->current_time))
		{
			for(die = 0; die < ssd->parameter->die_chip; die++)
			{
				for(plane = 0; plane < ssd->parameter->plane_die; plane++)
				{

				}
			}

		}

	}
}*/

/******************************************************
*这个函数也是只服务读子请求，并且处于等待状态的读子请求
*******************************************************/
int services_2_r_wait(struct ssd_info* ssd, unsigned int channel, unsigned int* channel_busy_flag, unsigned int* change_current_time_flag)
{
	unsigned int plane = 0, address_ppn = 0;
	struct sub_request* sub = NULL, * p = NULL, * p_gc = NULL;
	struct sub_request* sub_twoplane_one = NULL, * sub_twoplane_two = NULL;
	struct sub_request* sub_interleave_one = NULL, * sub_interleave_two = NULL, *sub_twoplane_three = NULL, *sub_twoplane_four = NULL;
	struct gc_operation* gc_node = NULL;
	struct direct_erase* new_direct_erase = NULL, * direct_erase_node = NULL;


	sub = ssd->channel_head[channel].subs_r_head;
	if (((ssd->parameter->advanced_commands & AD_TWOPLANE_READ) == AD_TWOPLANE_READ) && ((ssd->parameter->advanced_commands & AD_INTERLEAVE) == AD_INTERLEAVE ))
	{
		sub_twoplane_one = NULL;
		sub_twoplane_two = NULL;
		sub_twoplane_three = NULL;
		sub_twoplane_four = NULL;
		/*寻找能执行two_plane_interleave的四个读子请求*/
		find_interleave_twoplane_sub_request(ssd, channel, &sub_twoplane_one, &sub_twoplane_two, TWO_PLANE);
		find_interleave_twoplane_sub_request(ssd, channel, &sub_twoplane_one, &sub_twoplane_three, INTERLEAVE);
		if(sub_twoplane_three != NULL)
			find_interleave_twoplane_sub_request(ssd, channel, &sub_twoplane_three, &sub_twoplane_four, TWO_PLANE);

		if (sub_twoplane_three != NULL)
		{
			if(sub_twoplane_two != NULL || sub_twoplane_four != NULL) 		/*可以执行组合操作*/
				go_one_step_interleave_twoplane(ssd, sub_twoplane_one, sub_twoplane_two, sub_twoplane_three, sub_twoplane_four, SR_R_C_A_TRANSFER);
			else {
				go_one_step(ssd, sub_twoplane_one, sub_twoplane_three, SR_R_C_A_TRANSFER, INTERLEAVE);//执行interleave
			}
			*change_current_time_flag = 0;
			*channel_busy_flag = 1;		/*已经占用了这个周期的总线，不用执行die中数据的回传*/
		}
		else if (sub_twoplane_two != NULL) { //执行two_plane
			go_one_step(ssd, sub_twoplane_one, sub_twoplane_two, SR_R_C_A_TRANSFER, TWO_PLANE);
			*change_current_time_flag = 0;
			*channel_busy_flag = 1;		/*已经占用了这个周期的总线，不用执行die中数据的回传*/
		}
		else //普通命令
		{
			while (sub != NULL)		/*if there are read requests in queue, send one of them to target die*/
			{
				if (sub->current_state == SR_WAIT)
				{
					/*注意下个这个判断条件与services_2_r_data_trans中判断条件的不同*/
					if ((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].current_state == CHIP_IDLE) || ((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state == CHIP_IDLE) &&
						(ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state_predict_time <= ssd->current_time)))
					{
						go_one_step(ssd, sub, NULL, SR_R_C_A_TRANSFER, NORMAL);

						*change_current_time_flag = 0;
						*channel_busy_flag = 1;                                           /*已经占用了这个周期的总线，不用执行die中数据的回传*/
						break;
					}
					else
					{
						/*因为die的busy导致的阻塞*/
					}
				}
				sub = sub->next_node;
			}
		}
	}
	else if ((ssd->parameter->advanced_commands&AD_TWOPLANE_READ)==AD_TWOPLANE_READ)         /*to find whether there are two sub request can be served by two plane operation*/
	{
		sub_twoplane_one=NULL;
		sub_twoplane_two=NULL;

		/*寻找能执行two_plane的两个读子请求*/
		find_interleave_twoplane_sub_request(ssd,channel,&sub_twoplane_one,&sub_twoplane_two,TWO_PLANE);
	

		if (sub_twoplane_two!=NULL) 		/*可以执行two plane read 操作*/
		{
			go_one_step(ssd, sub_twoplane_one,sub_twoplane_two, SR_R_C_A_TRANSFER,TWO_PLANE);
			//printf("two_plane_read\n");		
			*change_current_time_flag=0;
			*channel_busy_flag=1;		/*已经占用了这个周期的总线，不用执行die中数据的回传*/
		} 
		else
		{
			while(sub!=NULL)		/*if there are read requests in queue, send one of them to target die*/			
			{		
				if(sub->current_state==SR_WAIT)									
				{	
					/*注意下个这个判断条件与services_2_r_data_trans中判断条件的不同*/
					if((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].current_state==CHIP_IDLE)||((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state==CHIP_IDLE)&&
						(ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state_predict_time<=ssd->current_time)))												
					{	
						go_one_step(ssd, sub,NULL, SR_R_C_A_TRANSFER,NORMAL);
									
						*change_current_time_flag=0;
						*channel_busy_flag=1;                                           /*已经占用了这个周期的总线，不用执行die中数据的回传*/
						break;										
					}	
					else
					{
						                                                                /*因为die的busy导致的阻塞*/
					}
				}						
				sub=sub->next_node;								
			}
		}
	}
	else if ((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)               /*to find whether there are two sub request can be served by INTERLEAVE operation*/
	{
		sub_interleave_one=NULL;
		sub_interleave_two=NULL;
		find_interleave_twoplane_sub_request(ssd,channel,&sub_interleave_one,&sub_interleave_two,INTERLEAVE);
		
		if (sub_interleave_two!=NULL)                                                  /*可以执行interleave read 操作*/
		{

			go_one_step(ssd, sub_interleave_one,sub_interleave_two, SR_R_C_A_TRANSFER,INTERLEAVE);
						
			*change_current_time_flag=0;
			*channel_busy_flag=1;                                                      /*已经占用了这个周期的总线，不用执行die中数据的回传*/
		} 
		else                                                                           /*没有满足条件的两个page，只能执行单个page的读*/
		{
			while(sub!=NULL)                                                           /*if there are read requests in queue, send one of them to target die*/			
			{		
				if(sub->current_state==SR_WAIT)									
				{	
					if((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].current_state==CHIP_IDLE)||((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state==CHIP_IDLE)&&
						(ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state_predict_time<=ssd->current_time)))												
					{	

						go_one_step(ssd, sub,NULL, SR_R_C_A_TRANSFER,NORMAL);
									
						*change_current_time_flag=0;
						*channel_busy_flag=1;                                          /*已经占用了这个周期的总线，不用执行die中数据的回传*/
						break;										
					}	
					else
					{
						                                                               /*因为die的busy导致的阻塞*/
					}
				}						
				sub=sub->next_node;								
			}
		}
	}

	/*******************************
	*ssd不能执行执行高级命令的情况下
	*******************************/
	else
	{
		while(sub!=NULL)                                                               /*if there are read requests in queue, send one of them to target chip*/			
		{		
			if(sub->current_state==SR_WAIT)									
			{	                                                                       
				if((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].current_state==CHIP_IDLE)||((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state==CHIP_IDLE)&&
					(ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state_predict_time<=ssd->current_time)))												
				{	

					go_one_step(ssd, sub,NULL, SR_R_C_A_TRANSFER,NORMAL);
							
					*change_current_time_flag=0;
					*channel_busy_flag=1;                                              /*已经占用了这个周期的总线，不用执行die中数据的回传*/
					break;										
				}	
				else
				{
					                                                                   /*因为die的busy导致的阻塞*/
				}
			}						
			sub=sub->next_node;								
		}
	}

	return SUCCESS;
}

/*********************************************************************
*当一个写子请求处理完后，要从请求队列上删除，这个函数就是执行这个功能。
**********************************************************************/
int delete_w_sub_request(struct ssd_info * ssd, unsigned int channel, struct sub_request * sub )
{
	struct sub_request * p=NULL;
	if (sub==ssd->channel_head[channel].subs_w_head)                                   /*将这个子请求从channel队列中删除*/
	{
		if (ssd->channel_head[channel].subs_w_head!=ssd->channel_head[channel].subs_w_tail)
		{
			ssd->channel_head[channel].subs_w_head=sub->next_node;
		} 
		else
		{
			ssd->channel_head[channel].subs_w_head=NULL;
			ssd->channel_head[channel].subs_w_tail=NULL;
		}
	}
	else
	{
		p=ssd->channel_head[channel].subs_w_head;
		while(p->next_node !=sub)
		{
			p=p->next_node;
		}

		if (sub->next_node!=NULL)
		{
			p->next_node=sub->next_node;
		} 
		else
		{
			p->next_node=NULL;
			ssd->channel_head[channel].subs_w_tail=p;
		}
	}
	
	return SUCCESS;	
}

/*
*函数的功能就是执行copyback命令的功能，
*/
Status copy_back(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die,struct sub_request * sub)
{
	int old_ppn=-1, new_ppn=-1;
	long long time=0;
	if (ssd->parameter->greed_CB_ad==1)                                               /*允许贪婪使用copyback高级命令*/
	{
		old_ppn=-1;
		if (ssd->dram->map->map_entry[sub->lpn].state!=0)                             /*说明这个逻辑页之前有写过，需要使用copyback+random input命令，否则直接写下去即可*/
		{
			if ((sub->state&ssd->dram->map->map_entry[sub->lpn].state)==ssd->dram->map->map_entry[sub->lpn].state)       
			{
				sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;	
			} 
			else
			{
				sub->next_state_predict_time=ssd->current_time+19*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				ssd->copy_back_count++;
				ssd->read_count++;
				ssd->update_read_count++;
				old_ppn=ssd->dram->map->map_entry[sub->lpn].pn;                       /*记录原来的物理页，用于在copyback时，判断是否满足同为奇地址或者偶地址*/
			}															
		} 
		else
		{
			sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		}
		sub->complete_time=sub->next_state_predict_time;		
		time=sub->complete_time;

		get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);

		if (old_ppn!=-1)                                                              /*采用了copyback操作，需要判断是否满足了奇偶地址的限制*/
		{
			new_ppn=ssd->dram->map->map_entry[sub->lpn].pn;
			while (old_ppn%2!=new_ppn%2)                                              /*没有满足奇偶地址限制，需要再往下找一页*/
			{
				get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);
				ssd->program_count--;
				ssd->write_flash_count--;
				ssd->waste_page_count++;
				new_ppn=ssd->dram->map->map_entry[sub->lpn].pn;
			}
		}
	} 
	else                                                                              /*不能贪婪的使用copyback高级命令*/
	{
		if (ssd->dram->map->map_entry[sub->lpn].state!=0)
		{
			if ((sub->state&ssd->dram->map->map_entry[sub->lpn].state)==ssd->dram->map->map_entry[sub->lpn].state)        
			{
				sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);
			} 
			else
			{
				old_ppn=ssd->dram->map->map_entry[sub->lpn].pn;                       /*记录原来的物理页，用于在copyback时，判断是否满足同为奇地址或者偶地址*/
				get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);
				new_ppn=ssd->dram->map->map_entry[sub->lpn].pn;
				if (old_ppn%2==new_ppn%2)
				{
					ssd->copy_back_count++;
					sub->next_state_predict_time=ssd->current_time+19*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				} 
				else
				{
					sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(size(ssd->dram->map->map_entry[sub->lpn].state))*ssd->parameter->time_characteristics.tRC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				}
				ssd->read_count++;
				ssd->update_read_count++;
			}
		} 
		else
		{
			sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
			get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);
		}
		sub->complete_time=sub->next_state_predict_time;		
		time=sub->complete_time;
	}
    
	/****************************************************************
	*执行copyback高级命令时，需要修改channel，chip的状态，以及时间等
	*****************************************************************/
	ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
	ssd->channel_head[channel].current_time=ssd->current_time;										
	ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
	ssd->channel_head[channel].next_state_predict_time=time;

	ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
	ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
	ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
	ssd->channel_head[channel].chip_head[chip].next_state_predict_time=time+ssd->parameter->time_characteristics.tPROG;
	
	return SUCCESS;
}

/*****************
*静态写操作的实现
******************/
Status static_write(struct ssd_info * ssd, unsigned int channel,unsigned int chip, unsigned int die,struct sub_request * sub)
{
	long long time=0;
	if (ssd->dram->map->map_entry[sub->lpn].state!=0)                                    /*说明这个逻辑页之前有写过，需要使用先读出来，再写下去，否则直接写下去即可*/
	{
		if ((sub->state&ssd->dram->map->map_entry[sub->lpn].state)==ssd->dram->map->map_entry[sub->lpn].state)   /*可以覆盖*/
		{
			sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		} 
		else
		{
			sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(size((ssd->dram->map->map_entry[sub->lpn].state^sub->state)))*ssd->parameter->time_characteristics.tRC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
			ssd->read_count++;
			ssd->update_read_count++;
		}
	} 
	else
	{
		sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
	}
	sub->complete_time=sub->next_state_predict_time;		
	time=sub->complete_time;

	get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);

    /****************************************************************
	*执行copyback高级命令时，需要修改channel，chip的状态，以及时间等
	*****************************************************************/
	ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
	ssd->channel_head[channel].current_time=ssd->current_time;										
	ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
	ssd->channel_head[channel].next_state_predict_time=time;

	ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
	ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
	ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
	ssd->channel_head[channel].chip_head[chip].next_state_predict_time=time+ssd->parameter->time_characteristics.tPROG;
	
	return SUCCESS;
}
int num = 0;
#ifdef USE_EC
unsigned int get_band_id_from_ppn(struct ssd_info *ssd, unsigned int ppn)
{
	unsigned int band_id;
	band_id = (ppn / ssd->parameter->page_block) % ssd->band_num;
	return band_id;
}
#endif
/********************
*写读请求的处理函数
*********************/
int services_2_read(struct ssd_info  *ssd, unsigned int channel)
{
	unsigned int chip, die, plane, plane0, plane1;
	struct sub_request *sub0, *sub1, *d_sub;
	unsigned int max_sub_num, i, count;
	unsigned int mp_flag, multi_plane_flag;
	struct sub_request *subs[4];
	
	max_sub_num=(ssd->parameter->die_chip)*(ssd->parameter->plane_die);
	
	for (chip = 0; chip < ssd->channel_head[channel].chip; chip++)
	{
		if(ssd->channel_head[channel].subs_r_head == NULL)
		{
			continue;
		}
		for(i = 0; i < max_sub_num; i++)
		{
			subs[i] = NULL;
		}
		if((ssd->channel_head[channel].chip_head[chip].current_state == CHIP_IDLE) || ((ssd->channel_head[channel].chip_head[chip].next_state == CHIP_IDLE) && (ssd->channel_head[channel].chip_head[chip].next_state_predict_time <= ssd->current_time)))
		{
			count = 0;
			for(die = 0; die < ssd->parameter->die_chip; die++)
			{
				mp_flag = 0;
				for(plane = 0; plane < ssd->parameter->plane_die / 2; plane++)
				{
					plane0 = plane * 2;
					plane1 = plane * 2 + 1;
					sub0 = get_first_read_sub(ssd, channel, chip, die, &plane0);
					sub1 = get_first_read_sub(ssd, channel, chip, die, &plane1);

					multi_plane_flag = check_multi_plane(ssd, sub0, sub1);
					if(multi_plane_flag == TRUE)
					{
						mp_flag = 1;
						ssd->m_plane_read_count++;
						if(multi_plane_read(ssd, sub0, sub1) == FAILURE)
						{
							printf("error!\tservice_2_read()\tmulti_plane_read()\n");
							return FAILURE;
						}
						subs[count++] = sub0;
						subs[count++] = sub1;
					}
				}
				//不能使用multi_plane
				if(mp_flag == 0)
				{
					d_sub = get_first_read_sub(ssd, channel, chip, die, NULL);
					/*
					if(d_sub == NULL)
					{
						if(sub0 != NULL)
							d_sub = sub0;
						if(sub1 != NULL)
							d_sub =sub1;
					}
					*/
					if(d_sub != NULL)
					{
						if(NAND_read(ssd, d_sub) == FAILURE)
						{
							printf("error!\tservice_2_read()\tNAND_read()\n");
							return FAILURE;
						}
						subs[count++] = d_sub;
					}

				}
			}
			if(count != 0)
			{
				ssd = compute_read_serve_time(ssd, channel, subs, count);
			}
		}
	}
	return SUCCESS;
}
// NAND multi_plane高级命令读操作
int multi_plane_read(struct ssd_info *ssd, struct sub_request *sub0, struct sub_request *sub1)
{
	if(NAND_read(ssd, sub0) == FAILURE || NAND_read(ssd, sub1) == FAILURE)
	{
		printf("error!\tmulti_plane_read\n");
		return FAILURE;
	}

	//修改状态
	return SUCCESS;
}
// NAND读操作
int NAND_read(struct ssd_info *ssd, struct sub_request *sub)
{
	// 实际读操作
	ssd->read_count++;
	return SUCCESS; 
}
// 计算读请求的服务时间
struct ssd_info *compute_read_serve_time(struct ssd_info *ssd, unsigned int channel, struct sub_request **subs, unsigned int sub_count)
{
	unsigned int chip, i;
	struct sub_request *last_sub = NULL;
	long long last_time;
	int *use_chip = NULL;
#ifdef RECOVERY
	struct recovery_operation *rec = NULL;
	unsigned int flag;
	long long recovery_time;
	unsigned int pos;
#ifdef ACTIVE_RECOVERY
	unsigned int j;
#endif
#endif

	use_chip = (int *)malloc(ssd->channel_head[channel].chip * sizeof(int));
	alloc_assert(subs,"use_chip");
	memset(use_chip, 0, ssd->channel_head[channel].chip * sizeof(int));
	for(i = 0; i < sub_count; i++)
	{
		chip = subs[i]->location->chip;
		//printf("chip = %d\t", chip);
		use_chip[chip] = 1;
	}
	//printf("\n");
	for(chip = 0; chip < ssd->channel_head[channel].chip; chip++)
	{
		if(use_chip[chip] == 1)
		{
			ssd->channel_head[channel].chip_head[chip].current_state = CHIP_DATA_TRANSFER;
			ssd->channel_head[channel].chip_head[chip].current_time = ssd->current_time;
			ssd->channel_head[channel].chip_head[chip].next_state = CHIP_IDLE;
			ssd->channel_head[channel].chip_head[chip].next_state_predict_time = ssd->current_time + ssd->parameter->time_characteristics.tR;
		}
	}

	for(i = 0; i < sub_count; i++)
	{
		subs[i]->current_state = SR_R_DATA_TRANSFER;
		if(last_sub == NULL)
		{
			chip = subs[i]->location->chip;
			subs[i]->current_time = ssd->channel_head[channel].chip_head[chip].next_state_predict_time;
		}
		else
		{
			subs[i]->current_time = last_sub->complete_time + ssd->parameter->time_characteristics.tDBSY;
		}
		subs[i]->current_state = SR_COMPLETE;
		subs[i]->next_state_predict_time = subs[i]->current_time + 7 * ssd->parameter->time_characteristics.tWC + ssd->parameter->page_capacity * ssd->parameter->time_characteristics.tRC;
		subs[i]->complete_time = subs[i]->next_state_predict_time;

		last_sub = subs[i];
		delete_r_sub_from_channel(ssd, channel, subs[i]);
		//delete_r_sub_request(ssd, channel, subs[i]);

#ifdef RECOVERY
		if(subs[i]->type == RECOVER_SUB)
		{	
			rec = ssd->recovery_head;
			//因为阻塞导致某些请求提前完成，则需轮询所有的恢复操作，确定是哪个恢复操作产生的子请求
			while(rec != NULL)
			{
				//和当前需要恢复的闪存页处于同一条带时，将恢复需要的读请求对应的完成标志置1
				if((flag = is_same_superpage(rec->sub, subs[i])) == TRUE)
				{
					pos = get_pos_in_band(ssd, subs[i]->ppn);
					rec->sub_r_complete_flag |= 1 << pos;
					//printf("lpn = %d\tblock_for_recovery = %d\tcomplete_flag = %d\n", rec->sub->lpn, rec->block_for_recovery, rec->sub_r_complete_flag);
					//printf("broken_lpn = %d\trecovery_lpn = %d\tblock_for_recovery = %d\tcomplete_flag = %d\n", rec->sub->lpn, subs[i]->lpn, rec->block_for_recovery, rec->sub_r_complete_flag);
				}
				//如果当前恢复操作所需的读请求都完成了，则进行解码恢复，并将恢复的闪存页写入缓存
				if(rec->sub_r_complete_flag == rec->block_for_recovery)
				{
#ifdef CALCULATION
					recovery_time = subs[i]->complete_time + 82000;
#else
					recovery_time = subs[i]->complete_time;
#endif
					/*printf("the page is recovered already, Write it into flash!\n");
					printf("lpn = %d\n\n", rec->sub->lpn);*/
#ifdef ACTIVE_RECOVERY
					active_write_recovery_page(ssd, rec, recovery_time);
#else
					write_recovery_page(ssd, rec, recovery_time);
#endif
					//write_recovery_page(ssd, rec->sub->lpn, rec->sub->state, recovery_time);
				}
				//如果已经确定请求对应的恢复操作，则跳出循环（读子请求和恢复操作为多对一）
				if(flag == TRUE)
				{
					break;
				}
				rec = rec->next_node;
			}
		}
#endif
	}

	ssd->channel_head[channel].current_state = CHANNEL_TRANSFER;
	ssd->channel_head[channel].current_time = ssd->current_time;
	ssd->channel_head[channel].next_state = CHANNEL_IDLE;

	last_time = ssd->channel_head[channel].next_state_predict_time;
	ssd->channel_head[channel].next_state_predict_time = (last_sub->complete_time > last_time) ? last_sub->complete_time : last_time;

	for (chip = 0; chip < ssd->channel_head[channel].chip; chip++)
	{
		if (use_chip[chip] == 1)
		{
			ssd->channel_head[channel].chip_head[chip].next_state_predict_time = last_sub->complete_time;
		}
	}

	if(use_chip != NULL)
	{
		free(use_chip);
		use_chip = NULL;
	}
	return ssd;
}

/*
struct ssd_info *delete_r_sub_request(struct ssd_info *ssd,unsigned int channel,struct sub_request *sub_req)
{
	struct sub_request *sub,*p;


	sub = ssd->channel_head[channel].subs_r_head;
	p = sub;

	while (sub!=NULL)
	{
		if (sub == sub_req)
		{
			//若在队列头
			if (sub == ssd->channel_head[channel].subs_r_head)                              
			{
				//队列中请求>=2
				if (ssd->channel_head[channel].subs_r_head != ssd->channel_head[channel].subs_w_tail)
				{
					ssd->channel_head[channel].subs_r_head = sub->next_node;
					sub = ssd->channel_head[channel].subs_r_head;
					continue;
				} 
				else
				{
					ssd->channel_head[channel].subs_r_head = NULL;
					ssd->channel_head[channel].subs_r_tail = NULL;
					p = NULL;
					break;
				}
			}
			else
			{
				//请求不在队尾
				if (sub->next_node != NULL) 
				{
					p->next_node = sub->next_node;
					sub=p->next_node;
					continue;
				} 
				else
				{
					ssd->channel_head[channel].subs_r_tail = p;
					ssd->channel_head[channel].subs_r_tail->next_node = NULL;
					break;
				}
			}
		}
		p = sub;
		sub = sub->next_node;
	}

	return ssd;
}
*/

int delete_r_sub_request(struct ssd_info * ssd, unsigned int channel, struct sub_request *sub)
{
	struct sub_request *p;
	p = ssd->channel_head[channel].subs_r_head;
	if(p == NULL){
		printf("there is no the same sub_request\n");
		return FAILURE;
	}
		//如果在队列头，直接删除
	if (sub == ssd->channel_head[channel].subs_r_head)                                  
	{
		// 2个及以上节点
		if (ssd->channel_head[channel].subs_r_head != ssd->channel_head[channel].subs_r_tail)
		{
			ssd->channel_head[channel].subs_r_head = ssd->channel_head[channel].subs_r_head->next_node;
		} 
		else
		{
			ssd->channel_head[channel].subs_r_head = NULL;
			ssd->channel_head[channel].subs_r_tail = NULL;
		}
	}
	else
	{
		while (p->next_node != NULL)
		{
			if(p->next_node == sub)
				break;
			p = p->next_node;
		}
		if(p->next_node != NULL)
		{
			//当找到的请求在队尾，删除该节点并修改队尾指针，否则只需删除该节点
			if(p->next_node == ssd->channel_head[channel].subs_r_tail)
			{
				p->next_node = NULL;
				ssd->channel_head[channel].subs_r_tail = p;
			}
			else 
			{
				p->next_node = sub->next_node;
			}
		}
		else
		{
			printf("Don't find the same subrequest!\t");
			if(p = ssd->channel_head[channel].subs_r_tail)
				printf("the tail of queue\n");
			else
				printf("error\n");
		}
	}
	return SUCCESS;	
}


// 根据sub request的类型分别从通道的读/写子请求队列中删除该请求
int delete_sub_from_channel(struct ssd_info * ssd, unsigned int channel, struct sub_request * sub)
{
	struct sub_request * p=NULL, *head = NULL, *tail = NULL;
	if(sub->operation == READ)
	{
		head = ssd->channel_head[channel].subs_r_head;
		tail = ssd->channel_head[channel].subs_r_tail;
	}
	else
	{
		head = ssd->channel_head[channel].subs_w_head;
		tail = ssd->channel_head[channel].subs_w_tail;
	}
	if (sub == head)                                   /*将这个子请求从channel队列中删除*/
	{
		if (head != tail)
		{
			head = sub->next_node;
		} 
		else
		{
			head = NULL;
			tail = NULL;
		}
	}
	else
	{
		p = head;
		while(p->next_node != NULL)
		{
			if(p->next_node == sub)
				break;
			p = p->next_node;

		}
		if(p->next_node == NULL)
		{
			printf("error, there is no the same sub-request\n");
			return FAILURE;
		}
		
		if (sub->next_node!=NULL)
		{
			p->next_node=sub->next_node;
		} 
		else
		{
			p->next_node=NULL;
			tail = p;
		}
	}

	if(sub->operation == READ)
	{
		ssd->channel_head[channel].subs_r_head = head;
	    ssd->channel_head[channel].subs_r_tail = tail;
	}
	else
	{
		ssd->channel_head[channel].subs_w_head = head;
		ssd->channel_head[channel].subs_w_tail = tail;
	}
	return SUCCESS;	
}
/*从plane或者die中获取第一个与给定channel，chip，die，plane位置相同的请求*/
struct sub_request *get_first_read_sub(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int *plane)
{
	struct sub_request *sub = NULL;
	sub = ssd->channel_head[channel].subs_r_head;
	if(plane == NULL)
	{
		while(sub != NULL)
		{
			if(sub->current_state != SR_WAIT)
			{
				sub = sub->next_node;
				continue;
			}
			if(sub->location->chip == chip && sub->location->die == die)
			{
				return sub;
			}
			sub = sub->next_node;
		}
	}
	else
	{
		while(sub != NULL)
		{
			if(sub->current_state != SR_WAIT)
			{
				sub = sub->next_node;
				continue;
			}
			if(sub->location->chip == chip && sub->location->die == die && sub->location->plane == *plane)
			{
				return sub;
			}
			sub = sub->next_node;
		}
	}
	return NULL;
}


/********************
*写子请求的处理函数
*********************/
int services_2_write(struct ssd_info * ssd,unsigned int channel,unsigned int * channel_busy_flag, unsigned int * change_current_time_flag)
{
	unsigned int chip;
	if (ssd->channel_head[channel].subs_w_head != NULL)
	{
		if (ssd->parameter->allocation_scheme == SUPERBLOCK_ALLOCTION)
		{
			for (chip = 0; chip < ssd->channel_head[channel].chip; chip++)
			{
				if (ssd->channel_head[channel].subs_w_head == NULL)
				{
					break;
				}
				if ((ssd->channel_head[channel].chip_head[chip].current_state == CHIP_IDLE) ||
					((ssd->channel_head[channel].chip_head[chip].next_state == CHIP_IDLE) && (ssd->channel_head[channel].chip_head[chip].next_state_predict_time <= ssd->current_time)))
				{
					if (dynamic_advanced_process(ssd, channel, chip) == NULL)
						*channel_busy_flag = 0;
					else
					{
						*channel_busy_flag = 1;
					}
				}
			}
		}
	}
	*channel_busy_flag = 0;
	return SUCCESS;
}
/*从plane或者die中获取第一个与给定channel，chip，die，plane位置相同的请求*/
struct sub_request *get_first_write_sub(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int *plane)
{
	struct sub_request *sub;
	sub = ssd->channel_head[channel].subs_w_head;
	if(plane == NULL)
	{
		while(sub != NULL)
		{
			if(sub->current_state == SR_WAIT && sub->begin_time <= ssd->current_time)
			{
				if ((sub->update == NULL) || ((sub->update != NULL) && ((sub->update->current_state == SR_COMPLETE) ||((sub->update->next_state == SR_COMPLETE) && (sub->update->next_state_predict_time <= ssd->current_time)))))
				{
					if(sub->location->chip == chip && sub->location->die == die)
					{
						return sub;
					}
				}
			}
			sub = sub->next_node;
		}
	}
	else
	{
		while(sub != NULL)
		{
			if(sub->current_state == SR_WAIT && sub->begin_time <= ssd->current_time)
			{
				if ((sub->update == NULL) || ((sub->update != NULL) && ((sub->update->current_state == SR_COMPLETE) ||((sub->update->next_state == SR_COMPLETE) && (sub->update->next_state_predict_time <= ssd->current_time)))))
				{
					if(sub->location->chip == chip && sub->location->die == die && sub->location->plane == *plane)
					{
						return sub;
					}
				}
			}
			sub = sub->next_node;
			/*
			if(sub->current_state != SR_WAIT)
			{
				sub = sub->next_node;
				continue;
			}
			if(sub->location->chip == chip && sub->location->die == die && sub->location->plane == *plane)
			{
				return sub;
			}
			sub = sub->next_node;
			*/
		}
	}
	return NULL;
}
/*检查两个请求是否满足multi_plane命令*/
int check_multi_plane(struct ssd_info *ssd, struct sub_request *sub0, struct sub_request *sub1)
{
	if(sub0 == NULL || sub1 == NULL)
		return FALSE;
	if(sub0->location->channel == sub1->location->channel && sub0->location->chip == sub1->location->chip && 
	   sub0->location->die == sub1->location->die && sub0->location->plane != sub1->location->plane && 
	   sub0->location->block == sub1->location->block && sub0->location->page == sub1->location->page)
		return TRUE;
	return FALSE;
}

int multi_plane_write(struct ssd_info *ssd, struct sub_request *sub0, struct sub_request *sub1)
{
	NAND_write(ssd, sub0);
	NAND_write(ssd, sub1);

	if (sub0->lpn != -1 && sub0->lpn != -2) {
		ssd->dram->map->map_entry[sub0->lpn].pn = sub0->ppn;
		ssd->dram->map->map_entry[sub0->lpn].state = sub0->state;
	}
	if (sub1->lpn != -1 && sub1->lpn != -2) {
		ssd->dram->map->map_entry[sub1->lpn].pn = sub1->ppn;
		ssd->dram->map->map_entry[sub1->lpn].state = sub1->state;
	}
	return SUCCESS;
}
/*
Status write_page(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,unsigned int active_block,unsigned int *ppn)
{
	int last_write_page=0;
	last_write_page=++(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page);	
	if(last_write_page>=(int)(ssd->parameter->page_block))
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page=0;
		printf("error! the last write page larger than 64!!\n");
		return ERROR;
	}

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--; 
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[last_write_page].written_count++;
	ssd->write_flash_count++;    
	*ppn=find_ppn(ssd,channel,chip,die,plane,active_block,last_write_page);

	return SUCCESS;
}
*/
void NAND_write(struct ssd_info *ssd, struct sub_request *sub)
{
	unsigned int channel, chip, die, plane, block, page;
	unsigned int full_page;
	full_page=~(0xffffffff<<(ssd->parameter->subpage_page));

	channel = sub->location->channel;
	chip = sub->location->chip;
	die = sub->location->die;
	plane = sub->location->plane;
	block = sub->location->block;
	page = sub->location->page;

	ssd->program_count++;
	ssd->channel_head[channel].program_count++;
	ssd->channel_head[channel].chip_head[chip].program_count++;
	ssd->write_flash_count++;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	//在alloction的时候已经修改这两个参数
	//ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num--;
	//ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn = sub->lpn;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].valid_state= sub->state;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].free_state=((~sub->state)&full_page);
	//每次写闪存后更新磨损表
}

struct ssd_info *dynamic_advanced_process(struct ssd_info *ssd,unsigned int channel,unsigned int chip)
{
	unsigned int die, plane, plane0, plane1;
	struct sub_request* sub0, * sub1, * d_sub;
	int multi_plane_flag, mp_flag;
	//struct sub_request **subs;
	unsigned int max_sub_num, i, count = 0;
	unsigned int sub_count_two_plane = 0, sub_count_interleave = 0;

	struct sub_request* subs[4];

	max_sub_num = (ssd->parameter->die_chip) * (ssd->parameter->plane_die);
	/*
	subs = (struct sub_request **)malloc(max_sub_num*sizeof(struct sub_request *));
	alloc_assert(subs,"sub_request");
	*/
	for (i = 0; i < max_sub_num; i++)
	{
		subs[i] = NULL;
	}

	for (die = 0; die < ssd->parameter->die_chip; die++)
	{
		mp_flag = 0;
		for (plane = 0; plane < ssd->parameter->plane_die / 2; plane++)
		{
			plane0 = 2 * plane;
			plane1 = 2 * plane + 1;
			sub0 = get_first_write_sub(ssd, channel, chip, die, &plane0);
			sub1 = get_first_write_sub(ssd, channel, chip, die, &plane1);

			multi_plane_flag = check_multi_plane(ssd, sub0, sub1);
			if (multi_plane_flag == TRUE)
			{
				sub_count_two_plane += 2;
				mp_flag = 1;
				multi_plane_write(ssd, sub0, sub1);
				subs[count++] = sub0;
				subs[count++] = sub1;
			}
		}
		// 若不存在可以执行multi_plane_write的请求，则进行普通的写
		if (mp_flag == FALSE)
		{
			d_sub = get_first_write_sub(ssd, channel, chip, die, NULL);
			if (d_sub == NULL)
			{
				if (sub0 != NULL)
					d_sub = sub0;
				if (sub1 = NULL)
					d_sub = sub1;
			}
			if (d_sub != NULL)
			{
				sub_count_interleave++;
				subs[count++] = d_sub;
				NAND_write(ssd, d_sub);
				if (d_sub->lpn != -2) {
					ssd->dram->map->map_entry[d_sub->lpn].pn = d_sub->ppn;
					ssd->dram->map->map_entry[d_sub->lpn].state = d_sub->state;
				}
			}
		}
	}
	if (count == 0)
		return NULL;

	if (count > 2)
	{
		ssd->interleave_count++;
		ssd->inter_mplane_prog_count++;
		compute_serve_time(ssd, channel, chip, 0, subs, count, INTERLEAVE_TWO_PLANE);
	}
	else if (count == 2)
	{
		if (sub_count_two_plane == 2)
		{
			ssd->m_plane_prog_count++;
			compute_serve_time(ssd, channel, chip, 0, subs, count, TWO_PLANE);
		}
		if (sub_count_interleave == 2)
		{
			ssd->interleave_count++;
			compute_serve_time(ssd, channel, chip, 0, subs, count, INTERLEAVE);
		}
	}
	else
	{
		compute_serve_time(ssd, channel, chip, 0, subs, count, NORMAL);
	}

	/*free(subs);
	subs = NULL;*/
	return ssd;
}
/*
int delete_w_sub_request(struct ssd_info * ssd, unsigned int channel, struct sub_request *sub)
{
	struct sub_request *p;
	p = ssd->channel_head[channel].subs_w_head;
	if(p == NULL){
		printf("there is no the same sub_request\n");
		return FAILURE;
	}
	//如果在队列头，直接删除
	if (sub == ssd->channel_head[channel].subs_w_head)                                  
	{
		// 2个及以上节点
		if (ssd->channel_head[channel].subs_w_head != ssd->channel_head[channel].subs_w_tail)
		{
			ssd->channel_head[channel].subs_w_head = ssd->channel_head[channel].subs_w_head->next_node;
		} 
		else
		{
			ssd->channel_head[channel].subs_w_head = NULL;
			ssd->channel_head[channel].subs_w_tail = NULL;
		}
	}
	else
	{
		while (p->next_node != NULL)
		{
			if(p->next_node == sub)
				break;
			p = p->next_node;
		}
		if(p->next_node != NULL)
		{
			//当找到的请求在队尾，删除该节点并修改队尾指针，否则只需删除该节点
			if(p->next_node == ssd->channel_head[channel].subs_w_tail)
			{
				p->next_node == NULL;
				ssd->channel_head[channel].subs_w_tail = p;
			}
			else 
			{
				p->next_node = p->next_node->next_node;
			}
		}
		else
		{
			printf("Don't find the same subrequest!\t");
			if(p = ssd->channel_head[channel].subs_w_tail)
				printf("the tail of queue\n");
			else
				printf("error\n");
		}
	}
	return SUCCESS;	
}

*/
#ifdef NO_SUPERBLOCK
Status services_2_write(struct ssd_info * ssd,unsigned int channel,unsigned int * channel_busy_flag, unsigned int * change_current_time_flag)
{
	int j=0,chip=0;
	unsigned int k=0;
	unsigned int  old_ppn=0,new_ppn=0;
	unsigned int chip_token=0,die_token=0,plane_token=0,address_ppn=0;
	unsigned int  die=0,plane=0;
	unsigned int band_last_lpn=0,parity_lpn=0;
	static unsigned int read_old_num = 0,parity_count = 0;
	unsigned int band_num = 0 ,p_ch = 0, first_parity = 0;//该block中的第几个条带，该条带校验位的位置。
	


	long long time=0;
	struct sub_request * sub=NULL, * p=NULL,*sub_r = NULL,*tmp = NULL,*sub_ch=NULL;
	struct sub_request * sub_twoplane_one=NULL, * sub_twoplane_two=NULL;
	struct sub_request * sub_interleave_one=NULL, * sub_interleave_two=NULL;

	first_parity = ssd->parameter->channel_number - ssd->band_head[ssd->current_band[0]].ec_modle; //该条带中第一个校验块的通道号
	//寻轮到校验通道，但未写满数据页，则跳过该通道
	if((size(ssd->strip_bits[0]) < first_parity) && (channel >= first_parity)){
		//printf("The strip0 is not full\n");
		return FAILURE;
	}
	/************************************************************************************************************************
	*写子请求挂在两个地方一个是channel_head[channel].subs_w_head，另外一个是ssd->subs_w_head，所以要保证至少有一个队列不为空
	*同时子请求的处理还分为动态分配和静态分配。
	*************************************************************************************************************************/
	/*if(channel != ssd->token)
	{
		printf("There are still read requests for this channel(%d)!\n", channel);
		return FALSE;
	}*/
	if((ssd->channel_head[channel].subs_w_head!=NULL)||(ssd->subs_w_head!=NULL))      
	{
		/*
		printf("num = %d\tchannel = %d", num++, channel);
		if(ssd->channel_head[channel].subs_w_head != NULL){
			printf("channel.sub_w_head !=NULL\n");
		}else{
			printf("ssd->subs_w_head!=NULL\n");
		}
		*/
		if (ssd->parameter->allocation_scheme==0)                                       /*动态分配*/
		{
			/*
			band_num = ssd->current_band;
			first_parity = ssd->parameter->channel_number - ssd->band_head[ssd->current_band].ec_modle; //该条带中第一个校验块的通道号
			//从ssd->sub_w_head中取出一个sub，写入该通道中
			if(channel < first_parity)
			{*/
			chip_token=ssd->channel_head[channel].token;                            /*chip令牌*/
			if (*channel_busy_flag==0)
			{
				//若chip的当前状态空闲，且下一个状态也空闲，并且下一个状态的预计时间小于等于当前时间
				if((ssd->channel_head[channel].chip_head[chip_token].current_state==CHIP_IDLE)||((ssd->channel_head[channel].chip_head[chip_token].next_state==CHIP_IDLE)&&(ssd->channel_head[channel].chip_head[chip_token].next_state_predict_time<=ssd->current_time)))				
				{
					die_token=ssd->channel_head[channel].chip_head[chip_token].token;	//获取该芯片中的die令牌
					//不使用高级命令
					if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))       
					{
						sub=find_write_sub_request(ssd,channel); //在ssd的写子请求队列中查找当前状态为SR_WAIT，且没有更新操作的写子请求，找到之后将其添加到对应通道中的写子请求队列
						if(sub==NULL)
						{
							//printf("Request not found!\n");
							return FALSE;
						}
						/*
						if(sub->lpn == -2){
							printf("This request is parity_sub_req!\n");
						}*/
						if(sub->current_state==SR_WAIT)
						{
							plane_token=ssd->channel_head[channel].chip_head[chip_token].die_head[die_token].token; //获取当前芯片当前die中的plane令牌
							//get_ppn 代表了一个新的物理页的数据写入。那么该条带块的写入数量+1；
							//如果该条带已经写满了数据页，那么写入校验数据。
							if(get_ppn(ssd,channel,chip_token,die_token,plane_token,sub) == NULL)  //在当前palne中的活跃块中获取可以写入的page，建立lpn-ppn映射关系，若是块中第一个页，建立条带块映射关系；当前条带写满数据页，则产生写校验请求，并插入对应通道中
								return FAILURE;
							
							//ssd->current_band = get_band_id_from_ppn(ssd, sub->ppn);
							*change_current_time_flag=0;

							if(ssd->parameter->ad_priority2==0)
							{
								ssd->real_time_subreq--;
							}
							go_one_step(ssd,sub,NULL,SR_W_TRANSFER,NORMAL);       /*执行普通的状态的转变。*/
							delete_w_sub_request(ssd,channel,sub);                /*删掉处理完后的写子请求*/
							/*if(ssd->channel_head[channel].subs_w_head == NULL){
								printf("one write sub\n");
							}*/

							#ifdef USE_WHITE_PARITY
								ssd->write_sub_request--;
								if(sub->lpn == 4502800)
									printf("wirte finished 4502800\n");
								if(sub->lpn == -2)
								{
									fprintf(ssd->parity_file," %8d:		 %2d :		 %2d :	 %2d :	 %2d :	 %5d :	 %4d \n",parity_count,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub->location->block,sub->location->page);
									parity_count++;
									//ssd->strip_bit = 0;
									free(sub->location);
									sub->location = NULL;
									free(sub);
									sub = NULL;
								}
								else{
									if(sub->type == GC_SUB)
									{
										if(sub->lpn == 4502800)
											printf("gc write finished  4502800\n");
										free(sub->location);
										sub->location = NULL;
										free(sub);
										sub = NULL;
										ssd->write_flash_gc_count++;//move number 在读请求时已经处理。
									}
								}
							#endif
							
							*channel_busy_flag=1;
							/**************************************************************************
							*跳出for循环前，修改令牌
							*这里的token的变化完全取决于在这个channel chip die plane下写是否成功 
							*成功了就break 没成功token就要变化直到找到能写成功的channel chip die plane
							***************************************************************************/
							/*
							ssd->channel_head[channel].chip_head[chip_token].die_head[die_token].token=(ssd->channel_head[channel].chip_head[chip_token].die_head[die_token].token+1)%ssd->parameter->plane_die;
							ssd->channel_head[channel].chip_head[chip_token].token=(ssd->channel_head[channel].chip_head[chip_token].token+1)%ssd->parameter->die_chip;
							ssd->channel_head[channel].token=(ssd->channel_head[channel].token+1)%ssd->parameter->chip_channel[0];*/
							//ssd->token = (ssd->token + 1) % ssd->parameter->channel_number;
						}
					}
					else
					{
						if (dynamic_advanced_process(ssd,channel,chip_token)==NULL)
						{
							*channel_busy_flag=0;
						}
						else
						{
							*channel_busy_flag=1;                                 /*执行了一个请求，传输了数据，占用了总线，需要跳出到下一个channel*/
							ssd->channel_head[channel].chip_head[chip_token].token=(ssd->channel_head[channel].chip_head[chip_token].token+1)%ssd->parameter->die_chip;
							ssd->channel_head[channel].token=(ssd->channel_head[channel].token+1)%ssd->parameter->chip_channel[channel];
						}
					}
				}
				/*
				else
				{
					printf("Chip is busy!\tcurrent_state = %d\n", ssd->channel_head[channel].chip_head[chip_token].current_state);
				}*/
			}
		} 
		else if(ssd->parameter->allocation_scheme==1)                                     /*静态分配*/
		{
			for(chip=0;chip<ssd->channel_head[channel].chip;chip++)					
			{	
				if((ssd->channel_head[channel].chip_head[chip].current_state==CHIP_IDLE)||((ssd->channel_head[channel].chip_head[chip].next_state==CHIP_IDLE)&&(ssd->channel_head[channel].chip_head[chip].next_state_predict_time<=ssd->current_time)))				
				{		
					if(ssd->channel_head[channel].subs_w_head==NULL)
					{
						break;
					}
					if (*channel_busy_flag==0)
					{
							                                                            
							if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))     /*不执行高级命令*/
							{
								for(die=0;die<ssd->channel_head[channel].chip_head[chip].die_num;die++)				
								{	
									if(ssd->channel_head[channel].subs_w_head==NULL)
									{
										break;
									}
									sub=ssd->channel_head[channel].subs_w_head;
									while (sub!=NULL)
									{
										if ((sub->current_state==SR_WAIT)&&(sub->location->channel==channel)&&(sub->location->chip==chip)&&(sub->location->die==die))      /*该子请求就是当前die的请求*/
										{
											break;
										}
										sub=sub->next_node;
									}
									if (sub==NULL)
									{
										continue;
									}

									if(sub->current_state==SR_WAIT)
									{
										sub->current_time=ssd->current_time;
										sub->current_state=SR_W_TRANSFER;
										sub->next_state=SR_COMPLETE;

										if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
										{
											copy_back(ssd, channel,chip, die,sub);      /*如果可以执行copyback高级命令，那么就用函数copy_back(ssd, channel,chip, die,sub)处理写子请求*/
											*change_current_time_flag=0;
										} 
										else
										{
											static_write(ssd, channel,chip, die,sub);   /*不能执行copyback高级命令，那么就用static_write(ssd, channel,chip, die,sub)函数来处理写子请求*/ 
											*change_current_time_flag=0;
										}
										
										delete_w_sub_request(ssd,channel,sub);
										*channel_busy_flag=1;
										break;
									}
								}
							} 
							else                                                        /*处理高级命令*/
							{
								if (dynamic_advanced_process(ssd,channel,chip)==NULL)
								{
									*channel_busy_flag=0;
								}
								else
								{
									*channel_busy_flag=1;                               /*执行了一个请求，传输了数据，占用了总线，需要跳出到下一个channel*/
									break;
								}
							}	
						
					}
				}		
			}
		}			
	}
	/*
	else
	{
		if(ssd->channel_head[channel].subs_w_head!=NULL)
			printf("channel = %d\tThere is no request!\n", channel);
		else
			printf("SSD\tThere is no request!\n");
	}*/
	return SUCCESS;	
}

/********************
*写子请求的处理函数
*********************/
Status services_2_write(struct ssd_info * ssd,unsigned int channel,unsigned int * channel_busy_flag, unsigned int * change_current_time_flag)
{
	int j=0,chip=0;
	unsigned int k=0;
	unsigned int  old_ppn=0,new_ppn=0;
	unsigned int chip_token=0,die_token=0,plane_token=0,address_ppn=0;
	unsigned int  die=0,plane=0;
	unsigned int band_last_lpn=0,parity_lpn=0;
	static unsigned int read_old_num = 0,parity_count = 0;
	unsigned int band_num = 0 ,p_ch = 0 ;//该block中的第几个条带，该条带校验位的位置。
	


	long long time=0;
	struct sub_request * sub=NULL, * p=NULL,*sub_r = NULL,*tmp = NULL,*sub_ch=NULL;
	struct sub_request * sub_twoplane_one=NULL, * sub_twoplane_two=NULL;
	struct sub_request * sub_interleave_one=NULL, * sub_interleave_two=NULL;

	/************************************************************************************************************************
	*写子请求挂在两个地方一个是channel_head[channel].subs_w_head，另外一个是ssd->subs_w_head，所以要保证至少有一个队列不为空
	*同时子请求的处理还分为动态分配和静态分配。
	*************************************************************************************************************************/
	if((ssd->channel_head[channel].subs_w_head!=NULL)||(ssd->subs_w_head!=NULL))      
	{
		//while()
		printf("num = %d\tchannel = %d", num++, channel);
		if(ssd->channel_head[channel].subs_w_head != NULL){
			printf("channel.sub_w_head !=NULL\n");
		}else{
			printf("ssd->subs_w_head!=NULL\n");
		}
		if (ssd->parameter->allocation_scheme==0)                                       /*动态分配*/
		{
			#ifdef USE_EC
				band_num = ssd->current_band;
				p_ch = ssd->parameter->channel_number - ssd->band_head[ssd->current_band].ec_modle; //该条带中第一个校验块的通道号
				//if(p_ch > channel) //条带中数据页还未写满
					//return SUCCESS; //写子请求处理函数返回，不在做后续的处理。
			#else

			#ifdef USE_WHITE_PARITY
				if(ssd->page_num !=0)
					band_num = (ssd->page_num-1)/PARITY_SIZE;
				else
					if(ssd->strip_bit != 0)
						band_num = ssd->parameter->page_block-1;
					else
						band_num = 0;
				if((ssd->strip_bit == 0)&&(ssd->page_num != 0))
					p_ch = PARITY_SIZE - (1+band_num)%(PARITY_SIZE+1);
				else
					p_ch = PARITY_SIZE - band_num%(PARITY_SIZE+1);
			//	if(ssd->page_num == 4)
				//	printf(" 8 ");

				if(p_ch == channel)//该通道是校验页所在的通道，
					if((ssd->strip_bit == 0)||(ssd->page_num % PARITY_SIZE != 0))//该条带中数据页还没有写满；
						return SUCCESS;//写子请求处理函数返回，不在做后续的处理。/**/

				if(ssd->strip_bit&(1<<channel))//如果该通道已经写过数据了，
				//	if(size(ssd->strip_bit) != PARITY_SIZE)//而且该条带写入的数据量还不够写入校验。
						return SUCCESS;
			#endif 
#endif
			for(j=0;j<ssd->channel_head[channel].chip;j++)					
			{		
				if((ssd->channel_head[channel].subs_w_head==NULL)&&(ssd->subs_w_head==NULL)) 
				{
					break;
				}
				//printf("channel = %d\tchip = %d\n", channel, j);
				chip_token=ssd->channel_head[channel].token;                            /*chip令牌*/
#ifdef USE_WHITE_PARITY
				/*if(ssd->strip_bit != 0){
					while(ssd->chip_num != chip_token) //将令牌动到条带所在的chip
					{
						//printf("ssd->chip_num = %d\tchip_token = %d\n",ssd->chip_num, chip_token);
						ssd->channel_head[channel].token=(ssd->channel_head[channel].token+1)%ssd->parameter->chip_channel[channel];
						chip_token=ssd->channel_head[channel].token;     
					}
				}*/
#endif 
				//printf("\n");
				if (*channel_busy_flag==0)
				{
					//若chip的当前状态空闲，且下一个状态也空闲，并且下一个状态的预计时间小于等于当前时间
					if((ssd->channel_head[channel].chip_head[chip_token].current_state==CHIP_IDLE)||((ssd->channel_head[channel].chip_head[chip_token].next_state==CHIP_IDLE)&&(ssd->channel_head[channel].chip_head[chip_token].next_state_predict_time<=ssd->current_time)))				
					{
						if((ssd->channel_head[channel].subs_w_head==NULL)&&(ssd->subs_w_head==NULL)) 
						{
							break;
						}
						die_token=ssd->channel_head[channel].chip_head[chip_token].token;	//获取该芯片中的die令牌
						//不使用高级命令
						if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))       
						{
							sub=find_write_sub_request(ssd,channel); //在ssd的写子请求队列中查找当前状态为SR_WAIT，且没有更新操作的写子请求，找到之后将其添加到对应通道中的写子请求队列
							if(sub==NULL)
							{
								break;
							}
							/*if(sub->lpn ==2282361)
								printf("");*/
							if(sub->current_state==SR_WAIT)
							{
								plane_token=ssd->channel_head[channel].chip_head[chip_token].die_head[die_token].token; //获取当前芯片当前die中的plane令牌
								//get_ppn 代表了一个新的物理页的数据写入。那么该条带块的写入数量+1；
								//如果该条带已经写满了数据页，那么写入校验数据。
								get_ppn(ssd,channel,chip_token,die_token,plane_token,sub);  //在当前palne中的活跃块中获取可以写入的page，建立lpn-ppn映射关系，若是块中第一个页，建立条带块映射关系；当前条带写满数据页，则产生写校验请求，并插入对应通道中
								//plane令牌+1
								ssd->channel_head[channel].chip_head[chip_token].die_head[die_token].token=(ssd->channel_head[channel].chip_head[chip_token].die_head[die_token].token+1)%ssd->parameter->plane_die;

								*change_current_time_flag=0;

								if(ssd->parameter->ad_priority2==0)
								{
									ssd->real_time_subreq--;
								}
								go_one_step(ssd,sub,NULL,SR_W_TRANSFER,NORMAL);       /*执行普通的状态的转变。*/
								delete_w_sub_request(ssd,channel,sub);                /*删掉处理完后的写子请求*/
								if(ssd->channel_head[channel].subs_w_head == NULL){
									printf("one write sub\n");
								}
									
								#ifdef USE_WHITE_PARITY
									ssd->write_sub_request--;
									if(sub->lpn == 4502800)
											printf("wirte finished 4502800\n");
									if(sub->lpn == -2)
									{
										fprintf(ssd->parity_file," %8d:		 %2d :		 %2d :	 %2d :	 %2d :	 %5d :	 %4d \n",parity_count,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub->location->block,sub->location->page);
										parity_count++;
										ssd->strip_bit = 0;
										free(sub->location);
										sub->location = NULL;
										free(sub);
										sub = NULL;/**/
									}
									else{
										if(sub->type == GC_SUB)
										{
											if(sub->lpn == 4502800)
												printf("gc write finished  4502800\n");
											free(sub->location);
											sub->location = NULL;
											free(sub);
											sub = NULL;/**/
											ssd->write_flash_gc_count++;//move number 在读请求时已经处理。
										}
									}
								#endif


								#ifdef USE_BLACK_PARITY
									band = sub->lpn/(PARITY_SIZE+1);
									band_last_lpn = (1+band)*(1+PARITY_SIZE)-1;
									parity_lpn = band_last_lpn - band%(PARITY_SIZE+1);
									if(sub->lpn == parity_lpn)
									{
										sub_r = sub;
										while (sub_r->read_old != NULL)
										{
											tmp = sub_r->read_old;
											sub_r->read_old = tmp->read_old;
											//we should delete the tmp (read_old) sub from the channel queue;
											//sub_ch = ssd->channel_head[tmp->location->channel].subs_r_head;
											delete_r_sub_from_channel(ssd,tmp->location->channel,tmp);
											free(tmp->location);
											tmp->location = NULL;
											free(tmp);
											tmp = NULL;
											read_old_num++;

										}
										free(sub->location);
										sub->location  = NULL;
										free(sub);
										sub = NULL;
										parity_count++;
										fprintf(ssd->parity_file,"%10d  %8d \n",read_old_num,parity_count);
									}
								#endif
						
								*channel_busy_flag=1;
								/**************************************************************************
								*跳出for循环前，修改令牌
								*这里的token的变化完全取决于在这个channel chip die plane下写是否成功 
								*成功了就break 没成功token就要变化直到找到能写成功的channel chip die plane
								***************************************************************************/
								ssd->channel_head[channel].chip_head[chip_token].token=(ssd->channel_head[channel].chip_head[chip_token].token+1)%ssd->parameter->die_chip;
								ssd->channel_head[channel].token=(ssd->channel_head[channel].token+1)%ssd->parameter->chip_channel[channel];
								//ssd->chip_num = ssd->channel_head[channel].token;
								break;
							}
						} 
						else                                                          /*use advanced commands*/
						{
							if (dynamic_advanced_process(ssd,channel,chip_token)==NULL)
							{
								*channel_busy_flag=0;
							}
							else
							{
								*channel_busy_flag=1;                                 /*执行了一个请求，传输了数据，占用了总线，需要跳出到下一个channel*/
                                    			ssd->channel_head[channel].chip_head[chip_token].token=(ssd->channel_head[channel].chip_head[chip_token].token+1)%ssd->parameter->die_chip;
                                    			ssd->channel_head[channel].token=(ssd->channel_head[channel].token+1)%ssd->parameter->chip_channel[channel];
								break;
							}
						}	
								
						ssd->channel_head[channel].chip_head[chip_token].token=(ssd->channel_head[channel].chip_head[chip_token].token+1)%ssd->parameter->die_chip;
					}
				}
								
				ssd->channel_head[channel].token=(ssd->channel_head[channel].token+1)%ssd->parameter->chip_channel[channel];
			}
		} 
		else if(ssd->parameter->allocation_scheme==1)                                     /*静态分配*/
		{
			for(chip=0;chip<ssd->channel_head[channel].chip;chip++)					
			{	
				if((ssd->channel_head[channel].chip_head[chip].current_state==CHIP_IDLE)||((ssd->channel_head[channel].chip_head[chip].next_state==CHIP_IDLE)&&(ssd->channel_head[channel].chip_head[chip].next_state_predict_time<=ssd->current_time)))				
				{		
					if(ssd->channel_head[channel].subs_w_head==NULL)
					{
						break;
					}
					if (*channel_busy_flag==0)
					{
							                                                            
							if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))     /*不执行高级命令*/
							{
								for(die=0;die<ssd->channel_head[channel].chip_head[chip].die_num;die++)				
								{	
									if(ssd->channel_head[channel].subs_w_head==NULL)
									{
										break;
									}
									sub=ssd->channel_head[channel].subs_w_head;
									while (sub!=NULL)
									{
										if ((sub->current_state==SR_WAIT)&&(sub->location->channel==channel)&&(sub->location->chip==chip)&&(sub->location->die==die))      /*该子请求就是当前die的请求*/
										{
											break;
										}
										sub=sub->next_node;
									}
									if (sub==NULL)
									{
										continue;
									}

									if(sub->current_state==SR_WAIT)
									{
										sub->current_time=ssd->current_time;
										sub->current_state=SR_W_TRANSFER;
										sub->next_state=SR_COMPLETE;

										if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
										{
											copy_back(ssd, channel,chip, die,sub);      /*如果可以执行copyback高级命令，那么就用函数copy_back(ssd, channel,chip, die,sub)处理写子请求*/
											*change_current_time_flag=0;
										} 
										else
										{
											static_write(ssd, channel,chip, die,sub);   /*不能执行copyback高级命令，那么就用static_write(ssd, channel,chip, die,sub)函数来处理写子请求*/ 
											*change_current_time_flag=0;
										}
										
										delete_w_sub_request(ssd,channel,sub);
										*channel_busy_flag=1;
										break;
									}
								}
							} 
							else                                                        /*处理高级命令*/
							{
								if (dynamic_advanced_process(ssd,channel,chip)==NULL)
								{
									*channel_busy_flag=0;
								}
								else
								{
									*channel_busy_flag=1;                               /*执行了一个请求，传输了数据，占用了总线，需要跳出到下一个channel*/
									break;
								}
							}	
						
					}
				}		
			}
		}			
	}
	return SUCCESS;	
}
#endif


/********************************************************
*这个函数的主要功能是主控读子请求和写子请求的状态变化处理
*********************************************************/
struct ssd_info* process(struct ssd_info* ssd)
{

	/*********************************************************************************************************
	*flag_die表示是否因为die的busy，阻塞了时间前进，-1表示没有，非-1表示有阻塞，
	*flag_die的值表示die号,old ppn记录在copyback之前的物理页号，用于判断copyback是否遵守了奇偶地址的限制；
	*two_plane_bit[8],two_plane_place[8]数组成员表示同一个channel上每个die的请求分配情况；
	*chg_cur_time_flag作为是否需要调整当前时间的标志位，当因为channel处于busy导致请求阻塞时，需要调整当前时间；
	*初始认为需要调整，置为1，当任何一个channel处理了传送命令或者数据时，这个值置为0，表示不需要调整；
	**********************************************************************************************************/
	int old_ppn = -1, flag_die = -1;
	unsigned int i, chan, random_num = 0;
	unsigned int flag = 0, new_write = 0, chg_cur_time_flag = 1, flag2 = 0, flag_gc = 0;
	__int64 time, channel_time = 0x7fffffffffffffff;
	struct sub_request* sub;

#ifdef DEBUG
	printf("enter process,  current time:%I64u\n", ssd->current_time);
#endif

	/*********************************************************
	*判断是否有读写子请求，如果有那么flag令为0，没有flag就为1
	*当flag为1时，若ssd中有gc操作这时就可以执行gc操作
	**********************************************************/
	for (i = 0; i < ssd->parameter->channel_number; i++)
	{
		if ((ssd->channel_head[i].subs_r_head == NULL) && (ssd->channel_head[i].subs_w_head == NULL) && (ssd->subs_w_head == NULL))
		{
			flag = 1;
		}
		else
		{
			flag = 0;
			break;
		}
	}
	if (flag == 1)
	{
		ssd->flag = 1;
		if (ssd->gc_request > 0)                                                            /*SSD中有gc操作的请求*/
		{
			gc(ssd, 0, 1);                                                                  /*这个gc要求所有channel都必须遍历到*/
		}
		return ssd;
	}
	else
	{
		ssd->flag = 0;
	}

	time = ssd->current_time;
	services_2_r_cmd_trans_and_complete(ssd);                                            /*处理当前状态是SR_R_C_A_TRANSFER或者当前状态是SR_COMPLETE，或者下一状态是SR_COMPLETE并且下一状态预计时间小于当前状态时间*/
	random_num = ssd->program_count % ssd->parameter->channel_number;                        /*产生一个随机数，保证每次从不同的channel开始查询*/

	/*****************************************
	*循环处理所有channel上的读写子请求
	*发读请求命令，传读写数据，都需要占用总线，
	******************************************/
	for (chan = 0; chan < ssd->parameter->channel_number; chan++)
	{
		i = (random_num + chan) % ssd->parameter->channel_number;
		flag = 0;
		flag_gc = 0;                                                                       /*每次进入channel时，将gc的标志位置为0，默认认为没有进行gc操作*/
		if ((ssd->channel_head[i].current_state == CHANNEL_IDLE) || (ssd->channel_head[i].next_state == CHANNEL_IDLE && ssd->channel_head[i].next_state_predict_time <= ssd->current_time))
		{
			if (ssd->gc_request > 0)                                                       /*有gc操作，需要进行一定的判断*/
			{
				if (ssd->channel_head[i].gc_command != NULL)
				{
					flag_gc = gc(ssd, i, 0);                                                 /*gc函数返回一个值，表示是否执行了gc操作，如果执行了gc操作，这个channel在这个时刻不能服务其他的请求*/
				}
				if (flag_gc == 1)                                                          /*执行过gc操作，需要跳出此次循环*/
				{
					continue;
				}
			}

			sub = ssd->channel_head[i].subs_r_head;                                        /*先处理读请求*/
			services_2_r_wait(ssd, i, &flag, &chg_cur_time_flag);                           /*处理处于等待状态的读子请求*/

			if ((flag == 0) && (ssd->channel_head[i].subs_r_head != NULL))                      /*if there are no new read request and data is ready in some dies, send these data to controller and response this request*/
			{
				services_2_r_data_trans(ssd, i, &flag, &chg_cur_time_flag);

			}
			if (flag == 0)                                                                  /*if there are no read request to take channel, we can serve write requests*/
			{
				services_2_write(ssd, i, &flag, &chg_cur_time_flag);

			}
		}
	}
	return ssd;
}

#ifdef NO_SUPERPAGE
struct ssd_info *process(struct ssd_info *ssd)   
{

	/*********************************************************************************************************
	*flag_die表示是否因为die的busy，阻塞了时间前进，-1表示没有，非-1表示有阻塞，
	*flag_die的值表示die号,old ppn记录在copyback之前的物理页号，用于判断copyback是否遵守了奇偶地址的限制；
	*two_plane_bit[8],two_plane_place[8]数组成员表示同一个channel上每个die的请求分配情况；
	*chg_cur_time_flag作为是否需要调整当前时间的标志位，当因为channel处于busy导致请求阻塞时，需要调整当前时间；
	*初始认为需要调整，置为1，当任何一个channel处理了传送命令或者数据时，这个值置为0，表示不需要调整；
	**********************************************************************************************************/
	int old_ppn=-1,flag_die=-1; 
	unsigned int i,chan,random_num;     
	unsigned int flag=0,new_write=0,chg_cur_time_flag=1,flag2=0,flag_gc=0;       
	__int64 time, channel_time=0x7fffffffffffffff;
	struct sub_request *sub;    
	unsigned int data_page_num;
	unsigned int chip_token;

#ifdef DEBUG
	printf("enter process,  current time:%I64u\n",ssd->current_time);
#endif

	/*********************************************************
	*判断是否有读写子请求，如果有那么flag令为0，没有flag就为1
	*当flag为1时，若ssd中有gc操作这时就可以执行gc操作
	**********************************************************/
	for(i=0;i<ssd->parameter->channel_number;i++)
	{          
		if((ssd->channel_head[i].subs_r_head==NULL)&&(ssd->channel_head[i].subs_w_head==NULL)&&(ssd->subs_w_head==NULL))
		{
			flag=1;
		}
		else
		{
			flag=0;
			break;
		}
	}
	if((flag==1)||(ssd->gc_request > 100))
	{
		ssd->flag=1;                                                                
		if (ssd->gc_request>0)                                                            /*SSD中有gc操作的请求*/
		{
#ifdef USE_WHITE_PARITY
			white_parity_gc(ssd);
#else
			gc(ssd,0,1);                                                                  /*这个gc要求所有channel都必须遍历到*/
#endif 
		}
#ifndef USE_WHITE_PARITY
		return ssd;
#endif
	}
	else
	{
		ssd->flag=0;
	}
		
	time = ssd->current_time;
	services_2_r_cmd_trans_and_complete(ssd);		/*处理当前状态是SR_R_C_A_TRANSFER或者当前状态是SR_COMPLETE，或者下一状态是SR_COMPLETE并且下一状态预计时间小于当前状态时间*/
#ifdef USE_WHITE_PARITY
	random_num=ssd->band_channel; 
	ssd->change_current_t_flag = 1;
#else
	random_num=ssd->program_count%ssd->parameter->channel_number;	/*产生一个随机数，保证每次从不同的channel开始查询*/
#endif
	/*****************************************
	*循环处理所有channel上的读写子请求
	*发读请求命令，传读写数据，都需要占用总线，
	******************************************/
	////从ssd->token的位置开始服务，由于按条带顺序写入，存在两种情况：（1）子请求队列中的写子请求在当前条带写完，则只需处理下其他通道的读请求
	//（2）子请求队列中的写子请求需要写入后续条带，则在处理完所有通道的读请求后，再将其他写子请求写入
	for(chan=0;chan<ssd->parameter->channel_number;chan++)	     
	{
		i=(random_num+chan)%ssd->parameter->channel_number;  
		data_page_num = ssd->parameter->channel_number - ssd->band_head[ssd->current_band[0]].ec_modle;
		//i = chan;
		flag=0;	
		#ifdef USE_WHITE_PARITY 
		ssd->channel_head[i].busy_flag = flag;
		#endif	
		flag_gc=0; 		/*每次进入channel时，将gc的标志位置为0，默认认为没有进行gc操作*/
		// 当通道当前状态为IDLE，或下一个状态为IDLE且下一状态预计时间小于当前时间，才执行以下操作
		if((ssd->channel_head[i].current_state==CHANNEL_IDLE)||(ssd->channel_head[i].next_state==CHANNEL_IDLE&&ssd->channel_head[i].next_state_predict_time<=ssd->current_time))		
		{
			/*
			//当该通道未写满的时候，考虑GC，否则当写满的时候，先进行校验数据处理
			if(find_first_zero(ssd, ssd->strip_bits[0]) < data_page_num){
				if (ssd->gc_request>0) 	//有gc操作，需要进行一定的判断
				{
					#ifdef USE_WHITE_PARITY
					if (ssd->channel_head[0].gc_command!=NULL)
					{
						if(((ssd->subs_w_head == NULL)&&(ssd->channel_head[i].subs_r_head == NULL))||(ssd->gc_request > 1000))
							flag_gc = white_parity_gc(ssd);
					}
					#else
					if (ssd->channel_head[i].gc_command!=NULL)
						flag_gc = gc(ssd,i,0);	//gc函数返回一个值，表示是否执行了gc操作，如果执行了gc操作，这个channel在这个时刻不能服务其他的请求
					#endif 
					
				}
				#ifndef USE_WHITE_PARITY
					if (flag_gc==1)	//执行过gc操作，需要跳出此次循环
					{
						continue;
					}
				#endif 
			}
			*/
			//printf("channel = %d\n", i);
			//如果strip_bit未写满数据，则检查未写数据块对应的channel和chip是否空闲，若空闲，则先服务对应通道的一个写子请求，更新strip_bit，
			//若strip_bit的size为该条带数据页个数，则产生校验请求挂在相应通道上
			
			//如果当前条带数据页未写满，且当前通道未写过
			/*if ((i < size(ssd->strip_bits[0])) || ((ssd->strip_bits[0] & (1 << i) == 0) && (ssd->strip_bits[0] >> i != 0)))// || size(ssd->strip_bits[0]) == data_page_num - 1
			{
			//if((i < size(ssd->strip_bits[0]) && ssd->strip_bits[1] != 0) || (size(ssd->strip_bits[0]) == i)){ 
			//if((ssd->strip_bits[0] & (1 << (data_page_num - 1))) && (size(ssd->strip_bits[0]) < data_page_num) && !(ssd->strip_bits[0] & (1 << i))){			
				//chip_token = ssd->channel_head[i].token;
				//if((ssd->channel_head[i].chip_head[chip_token].current_state==CHIP_IDLE)||((ssd->channel_head[i].chip_head[chip_token].next_state==CHIP_IDLE)&&(ssd->channel_head[i].chip_head[chip_token].next_state_predict_time<=ssd->current_time))){
					if(((ssd->strip_bits[3] >> i) & 0x01) == 0){
						//printf("channel = %d\tHandling write requests first!\n", i);
						services_2_write(ssd,i,&flag,&chg_cur_time_flag);
						ssd->change_current_t_flag = chg_cur_time_flag;
						ssd->channel_head[i].busy_flag = flag;
					}
					//printf("channel = %d\tHandling write requests first!\n", i);
				//}
				
			}
			*/
			sub=ssd->channel_head[i].subs_r_head;	/*先处理读请求*/
			if(flag == 0){
				//printf("services_2_r_wait\n");
				services_2_r_wait(ssd,i,&flag,&chg_cur_time_flag);	/*处理处于等待状态的读子请求，如果处理了请求则将flag赋值为1，chg_cur_time_flag赋值为0*/
				#ifdef USE_WHITE_PARITY
					ssd->change_current_t_flag = chg_cur_time_flag;
					ssd->channel_head[i].busy_flag = flag;
				#endif
			}
			if((flag==0)&&(ssd->channel_head[i].subs_r_head!=NULL))	/*if there are no new read request and data is ready in some dies, send these data to controller and response this request*/		
			{		     
				//printf("services_2_r_data_trans\n");
				services_2_r_data_trans(ssd,i,&flag,&chg_cur_time_flag);
				#ifdef USE_WHITE_PARITY
					ssd->change_current_t_flag = chg_cur_time_flag;
					ssd->channel_head[i].busy_flag = flag;
				#endif		
			}
			if(flag==0)	/*if there are no read request to take channel, we can serve write requests*/ 		
			{	
				if(((ssd->strip_bits[3] >> i) & 0x01) == 0){
					//printf("services_2_write\n");
					services_2_write(ssd,i,&flag,&chg_cur_time_flag);

					#ifdef USE_WHITE_PARITY
						ssd->change_current_t_flag = chg_cur_time_flag;
						ssd->channel_head[i].busy_flag = flag;
					#endif	
				}
			}	
		}
		/*
		else
		{
			printf("channel = %d is busy!\tcurrent_state = %d\n", i, ssd->channel_head[i].current_state);
		}*/
		if(ssd->channel_head[2].chip_head[0].die_head[0].plane_head[1].blk_head[7].page_head[38].lpn==-1)
			ssd->channel_head[2].chip_head[0].die_head[0].plane_head[1].blk_head[7].page_head[38].lpn =-1;
		if(ssd->dram->map->map_entry[186505].state == 15)
		    ssd->dram->map->map_entry[186505].state = 15;
		//printf("\n");
	}
	//printf("\n");
	return ssd;
}

/****************************************************************************************************************************
*当ssd支持高级命令时，这个函数的作用就是处理高级命令的写子请求
*根据请求的个数，决定选择哪种高级命令（这个函数只处理写请求，读请求已经分配到每个channel，所以在执行时之间进行选取相应的命令）
*****************************************************************************************************************************/
struct ssd_info *dynamic_advanced_process(struct ssd_info *ssd,unsigned int channel,unsigned int chip)         
{
	unsigned int die=0,plane=0;
	unsigned int subs_count=0;
	int flag;
	unsigned int gate;                                                                    /*record the max subrequest that can be executed in the same channel. it will be used when channel-level priority order is highest and allocation scheme is full dynamic allocation*/
	unsigned int plane_place;                                                             /*record which plane has sub request in static allocation*/
	struct sub_request *sub=NULL,*p=NULL,*sub0=NULL,*sub1=NULL,*sub2=NULL,*sub3=NULL,*sub0_rw=NULL,*sub1_rw=NULL,*sub2_rw=NULL,*sub3_rw=NULL;
	struct sub_request ** subs=NULL;
	unsigned int max_sub_num=0;
	unsigned int die_token=0,plane_token=0;
	unsigned int * plane_bits=NULL;
	unsigned int interleaver_count=0;
	
	unsigned int mask=0x00000001;
	unsigned int i=0,j=0;
	
	max_sub_num=(ssd->parameter->die_chip)*(ssd->parameter->plane_die);
	gate=max_sub_num;
	subs=(struct sub_request **)malloc(max_sub_num*sizeof(struct sub_request *));
	alloc_assert(subs,"sub_request");
	
	for(i=0;i<max_sub_num;i++)
	{
		subs[i]=NULL;
	}
	
	/*超级块分配方式，只需从这个特定的channel上选取等待服务的子请求*/
	if ((ssd->parameter->allocation_scheme==2))                                                                                                                         
	{
		sub=ssd->channel_head[channel].subs_w_head;
		plane_bits=(unsigned int * )malloc((ssd->parameter->die_chip)*sizeof(unsigned int));
		alloc_assert(plane_bits,"plane_bits");
		memset(plane_bits,0, (ssd->parameter->die_chip)*sizeof(unsigned int));

		for(i=0;i<ssd->parameter->die_chip;i++)
		{
			plane_bits[i]=0x00000000;
		}
		subs_count=0;
			
		while ((sub!=NULL)&&(subs_count<max_sub_num))
		{
			if(sub->current_state==SR_WAIT)								
			{
				if ((sub->update==NULL)||((sub->update!=NULL)&&((sub->update->current_state==SR_COMPLETE)||((sub->update->next_state==SR_COMPLETE)&&(sub->update->next_state_predict_time<=ssd->current_time)))))
				{
					if (sub->location->chip==chip)
					{
						plane_place=0x00000001<<(sub->location->plane);
	
						if ((plane_bits[sub->location->die]&plane_place)!=plane_place)      //we have not add sub request to this plane
						{
							subs[sub->location->die*ssd->parameter->plane_die+sub->location->plane]=sub;
							subs_count++;
							plane_bits[sub->location->die]=(plane_bits[sub->location->die]|plane_place);
						}
					}
				}						
			}
			sub=sub->next_node;	
		}//while ((sub!=NULL)&&(subs_count<max_sub_num))

		if (subs_count==0)                                                            /*没有请求可以服务，返回NULL*/
		{
			for(i=0;i<max_sub_num;i++)
			{
				subs[i]=NULL;
			}
			free(subs);
			subs=NULL;
			free(plane_bits);
			return NULL;
		}
			
		flag=0;
		if (ssd->parameter->advanced_commands!=0)
		{
			if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)        /*全部高级命令都可以使用*/
			{
				if (subs_count>1)                                                    /*有1个以上可以直接服务的写请求*/
				{
					get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,COPY_BACK);
				} 
				else
				{
					for(i=0;i<max_sub_num;i++)
					{
						if(subs[i]!=NULL)
						{
							break;
						}
					}
					get_ppn_for_normal_command(ssd,channel,chip,subs[i]);
				}
				
			}// if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
			else                                                                     /*不能执行copyback*/
			{
				if (subs_count>1)                                                    /*有1个以上可以直接服务的写请求*/
				{
					if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE))
					{
						get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,INTERLEAVE_TWO_PLANE);
					} 
					else if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))
					{
						for(die=0;die<ssd->parameter->die_chip;die++)
						{
							if(plane_bits[die]!=0x00000000)
							{
								for(i=0;i<ssd->parameter->plane_die;i++)
								{
									plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
									ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane_token+1)%ssd->parameter->plane_die;
									mask=0x00000001<<plane_token;
									if((plane_bits[die]&mask)==mask)
									{
										plane_bits[die]=mask;
										break;
									}
								}
								for(i=i+1;i<ssd->parameter->plane_die;i++)
								{
									plane=(plane_token+1)%ssd->parameter->plane_die;
									subs[die*ssd->parameter->plane_die+plane]=NULL;
									subs_count--;
								}
								interleaver_count++;
							}//if(plane_bits[die]!=0x00000000)
						}//for(die=0;die<ssd->parameter->die_chip;die++)
						if(interleaver_count>=2)
						{
							get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,INTERLEAVE);
						}
						else
						{
							for(i=0;i<max_sub_num;i++)
							{
								if(subs[i]!=NULL)
								{
									break;
								}
							}
							get_ppn_for_normal_command(ssd,channel,chip,subs[i]);	
						}
					}//else if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))
					else if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE))
					{
						for(i=0;i<ssd->parameter->die_chip;i++)
						{
							die_token=ssd->channel_head[channel].chip_head[chip].token;
							ssd->channel_head[channel].chip_head[chip].token=(die_token+1)%ssd->parameter->die_chip;
							if(size(plane_bits[die_token])>1)
							{
								break;
							}
								
						}
							
						if(i<ssd->parameter->die_chip)
						{
							for(die=0;die<ssd->parameter->die_chip;die++)
							{
								if(die!=die_token)
								{
									for(plane=0;plane<ssd->parameter->plane_die;plane++)
									{
										if(subs[die*ssd->parameter->plane_die+plane]!=NULL)
										{
											subs[die*ssd->parameter->plane_die+plane]=NULL;
											subs_count--;
										}
									}
								}
							}
							get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,TWO_PLANE);
						}//if(i<ssd->parameter->die_chip)
						else
						{
							for(i=0;i<ssd->parameter->die_chip;i++)
							{
								die_token=ssd->channel_head[channel].chip_head[chip].token;
								ssd->channel_head[channel].chip_head[chip].token=(die_token+1)%ssd->parameter->die_chip;
								if(plane_bits[die_token]!=0x00000000)
								{
									for(j=0;j<ssd->parameter->plane_die;j++)
									{
										plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die_token].token;
										ssd->channel_head[channel].chip_head[chip].die_head[die_token].token=(plane_token+1)%ssd->parameter->plane_die;
										if(((plane_bits[die_token])&(0x00000001<<plane_token))!=0x00000000)
										{
											sub=subs[die_token*ssd->parameter->plane_die+plane_token];
											break;
										}
									}
								}
							}//for(i=0;i<ssd->parameter->die_chip;i++)
							get_ppn_for_normal_command(ssd,channel,chip,sub);
						}//else
					}//else if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE))
				}//if (subs_count>1)  
				else
				{
					for(i=0;i<ssd->parameter->die_chip;i++)
					{
						die_token=ssd->channel_head[channel].chip_head[chip].token;
						ssd->channel_head[channel].chip_head[chip].token=(die_token+1)%ssd->parameter->die_chip;
						if(plane_bits[die_token]!=0x00000000)
						{
							for(j=0;j<ssd->parameter->plane_die;j++)
							{
								plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die_token].token;
								ssd->channel_head[channel].chip_head[chip].die_head[die_token].token=(plane_token+1)%ssd->parameter->plane_die;
								if(((plane_bits[die_token])&(0x00000001<<plane_token))!=0x00000000)
								{
									sub=subs[die_token*ssd->parameter->plane_die+plane_token];
									break;
								}
							}
							if(sub!=NULL)
							{
								break;
							}
						}
					}//for(i=0;i<ssd->parameter->die_chip;i++)
					get_ppn_for_normal_command(ssd,channel,chip,sub);
				}//else
			}
		}//if (ssd->parameter->advanced_commands!=0)
		else
		{
			for(i=0;i<ssd->parameter->die_chip;i++)
			{
				die_token=ssd->channel_head[channel].chip_head[chip].token;
				ssd->channel_head[channel].chip_head[chip].token=(die_token+1)%ssd->parameter->die_chip;
				if(plane_bits[die_token]!=0x00000000)
				{
					for(j=0;j<ssd->parameter->plane_die;j++)
					{
						plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die_token].token;
						ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane_token+1)%ssd->parameter->plane_die;
						if(((plane_bits[die_token])&(0x00000001<<plane_token))!=0x00000000)
						{
							sub=subs[die_token*ssd->parameter->plane_die+plane_token];
							break;
						}
					}
					if(sub!=NULL)
					{
						break;
					}
				}
			}//for(i=0;i<ssd->parameter->die_chip;i++)
			get_ppn_for_normal_command(ssd,channel,chip,sub);
		}//else
		
	}//else

	for(i=0;i<max_sub_num;i++)
	{
		subs[i]=NULL;
	}
	free(subs);
	subs=NULL;
	free(plane_bits);
	return ssd;
}


struct ssd_info *dynamic_advanced_process(struct ssd_info *ssd,unsigned int channel,unsigned int chip)         
{
	unsigned int die=0,plane=0;
	unsigned int subs_count=0;
	int flag;
	unsigned int gate;                                                                    /*record the max subrequest that can be executed in the same channel. it will be used when channel-level priority order is highest and allocation scheme is full dynamic allocation*/
	unsigned int plane_place;                                                             /*record which plane has sub request in static allocation*/
	struct sub_request *sub=NULL,*p=NULL,*sub0=NULL,*sub1=NULL,*sub2=NULL,*sub3=NULL,*sub0_rw=NULL,*sub1_rw=NULL,*sub2_rw=NULL,*sub3_rw=NULL;
	struct sub_request ** subs=NULL;
	unsigned int max_sub_num=0;
	unsigned int die_token=0,plane_token=0;
	unsigned int * plane_bits=NULL;
	unsigned int interleaver_count=0;
	
	unsigned int mask=0x00000001;
	unsigned int i=0,j=0;
	
	max_sub_num=(ssd->parameter->die_chip)*(ssd->parameter->plane_die);
	gate=max_sub_num;
	subs=(struct sub_request **)malloc(max_sub_num*sizeof(struct sub_request *));
	alloc_assert(subs,"sub_request");
	
	for(i=0;i<max_sub_num;i++)
	{
		subs[i]=NULL;
	}
	
	if((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==0)&&(ssd->parameter->ad_priority2==0))
	{
		gate=ssd->real_time_subreq/ssd->parameter->channel_number;

		if(gate==0)
		{
			gate=1;
		}
		else
		{
			if(ssd->real_time_subreq%ssd->parameter->channel_number!=0)
			{
				gate++;
			}
		}
	}

	if ((ssd->parameter->allocation_scheme==0))                                           /*全动态分配，需要从ssd->subs_w_head上选取等待服务的子请求*/
	{
		if(ssd->parameter->dynamic_allocation==0)
		{
			sub=ssd->subs_w_head;
		}
		else
		{
			sub=ssd->channel_head[channel].subs_w_head;
		}
		
		subs_count=0;
		
		while ((sub!=NULL)&&(subs_count<max_sub_num)&&(subs_count<gate))
		{
			if(sub->current_state==SR_WAIT)								
			{
				if ((sub->update==NULL)||((sub->update!=NULL)&&((sub->update->current_state==SR_COMPLETE)||((sub->update->next_state==SR_COMPLETE)&&(sub->update->next_state_predict_time<=ssd->current_time)))))    //没有需要提前读出的页
				{
					subs[subs_count]=sub;
					subs_count++;
				}						
			}
			
			p=sub;
			sub=sub->next_node;	
		}

		if (subs_count==0)                                                               /*没有请求可以服务，返回NULL*/
		{
			for(i=0;i<max_sub_num;i++)
			{
				subs[i]=NULL;
			}
			free(subs);

			subs=NULL;
			free(plane_bits);
			return NULL;
		}
		if(subs_count>=2)
		{
		    /*********************************************
			*two plane,interleave都可以使用
			*在这个channel上，选用interleave_two_plane执行
			**********************************************/
			if (((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE)&&((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE))     
			{                                                                        
				get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,INTERLEAVE_TWO_PLANE); 
			}
			else if (((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE)&&((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE))
			{
				if(subs_count>ssd->parameter->plane_die)
				{	
					for(i=ssd->parameter->plane_die;i<subs_count;i++)
					{
						subs[i]=NULL;
					}
					subs_count=ssd->parameter->plane_die;
				}
				get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,TWO_PLANE);
			}
			else if (((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE)&&((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE))
			{
				
				if(subs_count>ssd->parameter->die_chip)
				{	
					for(i=ssd->parameter->die_chip;i<subs_count;i++)
					{
						subs[i]=NULL;
					}
					subs_count=ssd->parameter->die_chip;
				}
				get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,INTERLEAVE);
			}
			else
			{
				for(i=1;i<subs_count;i++)
				{
					subs[i]=NULL;
				}
				subs_count=1;
				get_ppn_for_normal_command(ssd,channel,chip,subs[0]);
			}
			
		}//if(subs_count>=2)
		else if(subs_count==1)     //only one request
		{
			get_ppn_for_normal_command(ssd,channel,chip,subs[0]);
		}
		
	}//if ((ssd->parameter->allocation_scheme==0)) 
	else                                                                                  /*静态分配方式，只需从这个特定的channel上选取等待服务的子请求*/
	{
		                                                                                  /*在静态分配方式中，根据channel上的请求落在同一个die上的那些plane来确定使用什么命令*/
		
			sub=ssd->channel_head[channel].subs_w_head;
			plane_bits=(unsigned int * )malloc((ssd->parameter->die_chip)*sizeof(unsigned int));
			alloc_assert(plane_bits,"plane_bits");
			memset(plane_bits,0, (ssd->parameter->die_chip)*sizeof(unsigned int));

			for(i=0;i<ssd->parameter->die_chip;i++)
			{
				plane_bits[i]=0x00000000;
			}
			subs_count=0;
			
			while ((sub!=NULL)&&(subs_count<max_sub_num))
			{
				if(sub->current_state==SR_WAIT)								
				{
					if ((sub->update==NULL)||((sub->update!=NULL)&&((sub->update->current_state==SR_COMPLETE)||((sub->update->next_state==SR_COMPLETE)&&(sub->update->next_state_predict_time<=ssd->current_time)))))
					{
						if (sub->location->chip==chip)
						{
							plane_place=0x00000001<<(sub->location->plane);
	
							if ((plane_bits[sub->location->die]&plane_place)!=plane_place)      //we have not add sub request to this plane
							{
								subs[sub->location->die*ssd->parameter->plane_die+sub->location->plane]=sub;
								subs_count++;
								plane_bits[sub->location->die]=(plane_bits[sub->location->die]|plane_place);
							}
						}
					}						
				}
				sub=sub->next_node;	
			}//while ((sub!=NULL)&&(subs_count<max_sub_num))

			if (subs_count==0)                                                            /*没有请求可以服务，返回NULL*/
			{
				for(i=0;i<max_sub_num;i++)
				{
					subs[i]=NULL;
				}
				free(subs);
				subs=NULL;
				free(plane_bits);
				return NULL;
			}
			
			flag=0;
			if (ssd->parameter->advanced_commands!=0)
			{
				if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)        /*全部高级命令都可以使用*/
				{
					if (subs_count>1)                                                    /*有1个以上可以直接服务的写请求*/
					{
						get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,COPY_BACK);
					} 
					else
					{
						for(i=0;i<max_sub_num;i++)
						{
							if(subs[i]!=NULL)
							{
								break;
							}
						}
						get_ppn_for_normal_command(ssd,channel,chip,subs[i]);
					}
				
				}// if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
				else                                                                     /*不能执行copyback*/
				{
					if (subs_count>1)                                                    /*有1个以上可以直接服务的写请求*/
					{
						if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE))
						{
							get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,INTERLEAVE_TWO_PLANE);
						} 
						else if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))
						{
							for(die=0;die<ssd->parameter->die_chip;die++)
							{
								if(plane_bits[die]!=0x00000000)
								{
									for(i=0;i<ssd->parameter->plane_die;i++)
									{
										plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
										ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane_token+1)%ssd->parameter->plane_die;
										mask=0x00000001<<plane_token;
										if((plane_bits[die]&mask)==mask)
										{
											plane_bits[die]=mask;
											break;
										}
									}
									for(i=i+1;i<ssd->parameter->plane_die;i++)
									{
										plane=(plane_token+1)%ssd->parameter->plane_die;
										subs[die*ssd->parameter->plane_die+plane]=NULL;
										subs_count--;
									}
									interleaver_count++;
								}//if(plane_bits[die]!=0x00000000)
							}//for(die=0;die<ssd->parameter->die_chip;die++)
							if(interleaver_count>=2)
							{
								get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,INTERLEAVE);
							}
							else
							{
								for(i=0;i<max_sub_num;i++)
								{
									if(subs[i]!=NULL)
									{
										break;
									}
								}
								get_ppn_for_normal_command(ssd,channel,chip,subs[i]);	
							}
						}//else if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))
						else if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE))
						{
							for(i=0;i<ssd->parameter->die_chip;i++)
							{
								die_token=ssd->channel_head[channel].chip_head[chip].token;
								ssd->channel_head[channel].chip_head[chip].token=(die_token+1)%ssd->parameter->die_chip;
								if(size(plane_bits[die_token])>1)
								{
									break;
								}
								
							}
							
							if(i<ssd->parameter->die_chip)
							{
								for(die=0;die<ssd->parameter->die_chip;die++)
								{
									if(die!=die_token)
									{
										for(plane=0;plane<ssd->parameter->plane_die;plane++)
										{
											if(subs[die*ssd->parameter->plane_die+plane]!=NULL)
											{
												subs[die*ssd->parameter->plane_die+plane]=NULL;
												subs_count--;
											}
										}
									}
								}
								get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,TWO_PLANE);
							}//if(i<ssd->parameter->die_chip)
							else
							{
								for(i=0;i<ssd->parameter->die_chip;i++)
								{
									die_token=ssd->channel_head[channel].chip_head[chip].token;
									ssd->channel_head[channel].chip_head[chip].token=(die_token+1)%ssd->parameter->die_chip;
									if(plane_bits[die_token]!=0x00000000)
									{
										for(j=0;j<ssd->parameter->plane_die;j++)
										{
											plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die_token].token;
											ssd->channel_head[channel].chip_head[chip].die_head[die_token].token=(plane_token+1)%ssd->parameter->plane_die;
											if(((plane_bits[die_token])&(0x00000001<<plane_token))!=0x00000000)
											{
												sub=subs[die_token*ssd->parameter->plane_die+plane_token];
												break;
											}
										}
									}
								}//for(i=0;i<ssd->parameter->die_chip;i++)
								get_ppn_for_normal_command(ssd,channel,chip,sub);
							}//else
						}//else if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE))
					}//if (subs_count>1)  
					else
					{
						for(i=0;i<ssd->parameter->die_chip;i++)
						{
							die_token=ssd->channel_head[channel].chip_head[chip].token;
							ssd->channel_head[channel].chip_head[chip].token=(die_token+1)%ssd->parameter->die_chip;
							if(plane_bits[die_token]!=0x00000000)
							{
								for(j=0;j<ssd->parameter->plane_die;j++)
								{
									plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die_token].token;
									ssd->channel_head[channel].chip_head[chip].die_head[die_token].token=(plane_token+1)%ssd->parameter->plane_die;
									if(((plane_bits[die_token])&(0x00000001<<plane_token))!=0x00000000)
									{
										sub=subs[die_token*ssd->parameter->plane_die+plane_token];
										break;
									}
								}
								if(sub!=NULL)
								{
									break;
								}
							}
						}//for(i=0;i<ssd->parameter->die_chip;i++)
						get_ppn_for_normal_command(ssd,channel,chip,sub);
					}//else
				}
			}//if (ssd->parameter->advanced_commands!=0)
			else
			{
				for(i=0;i<ssd->parameter->die_chip;i++)
				{
					die_token=ssd->channel_head[channel].chip_head[chip].token;
					ssd->channel_head[channel].chip_head[chip].token=(die_token+1)%ssd->parameter->die_chip;
					if(plane_bits[die_token]!=0x00000000)
					{
						for(j=0;j<ssd->parameter->plane_die;j++)
						{
							plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die_token].token;
							ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane_token+1)%ssd->parameter->plane_die;
							if(((plane_bits[die_token])&(0x00000001<<plane_token))!=0x00000000)
							{
								sub=subs[die_token*ssd->parameter->plane_die+plane_token];
								break;
							}
						}
						if(sub!=NULL)
						{
							break;
						}
					}
				}//for(i=0;i<ssd->parameter->die_chip;i++)
				get_ppn_for_normal_command(ssd,channel,chip,sub);
			}//else
		
	}//else

	for(i=0;i<max_sub_num;i++)
	{
		subs[i]=NULL;
	}
	free(subs);
	subs=NULL;
	free(plane_bits);
	return ssd;
}
#endif
/****************************************
*执行写子请求时，为普通的写子请求获取ppn
*****************************************/
Status get_ppn_for_normal_command(struct ssd_info * ssd, unsigned int channel,unsigned int chip, struct sub_request * sub)
{
	unsigned int die=0;
	unsigned int plane=0;
	if(sub==NULL)
	{
		return ERROR;
	}
	
	if (ssd->parameter->allocation_scheme==DYNAMIC_ALLOCATION)
	{
		die=ssd->channel_head[channel].chip_head[chip].token;
		plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
		get_ppn(ssd,channel,chip,die,plane,sub);
		ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane+1)%ssd->parameter->plane_die;
		ssd->channel_head[channel].chip_head[chip].token=(die+1)%ssd->parameter->die_chip;
		
		compute_serve_time(ssd,channel,chip,die,&sub,1,NORMAL);
		return SUCCESS;
	}
	else
	{
		die=sub->location->die;
		plane=sub->location->plane;
		get_ppn(ssd,channel,chip,die,plane,sub);   
		compute_serve_time(ssd,channel,chip,die,&sub,1,NORMAL);
		return SUCCESS;
	}

}



/************************************************************************************************
*为高级命令获取ppn
*根据不同的命令，遵从在同一个block中顺序写的要求，选取可以进行写操作的ppn，跳过的ppn全部置为失效。
*在使用two plane操作时，为了寻找相同水平位置的页，可能需要直接找到两个完全空白的块，这个时候原来
*的块没有用完，只能放在这，等待下次使用，同时修改查找空白page的方法，将以前首先寻找free块改为，只
*要invalid block!=64即可。
*except find aim page, we should modify token and decide gc operation
*************************************************************************************************/
Status get_ppn_for_advanced_commands(struct ssd_info *ssd,unsigned int channel,unsigned int chip,struct sub_request * * subs ,unsigned int subs_count,unsigned int command)      
{
	unsigned int die=0,plane=0;
	unsigned int die_token=0,plane_token=0;
	struct sub_request * sub=NULL;
	unsigned int i=0,j=0,k=0;
	unsigned int unvalid_subs_count=0;
	unsigned int valid_subs_count=0;
	unsigned int interleave_flag=FALSE;
	unsigned int multi_plane_falg=FALSE;
	unsigned int max_subs_num=0;
	struct sub_request * first_sub_in_chip=NULL;
	struct sub_request * first_sub_in_die=NULL;
	struct sub_request * second_sub_in_die=NULL;
	unsigned int state=SUCCESS;
	unsigned int multi_plane_flag=FALSE;

	max_subs_num=ssd->parameter->die_chip*ssd->parameter->plane_die;
	
	if (ssd->parameter->allocation_scheme==DYNAMIC_ALLOCATION)                         /*动态分配操作*/ 
	{
		if((command==INTERLEAVE_TWO_PLANE)||(command==COPY_BACK))                      /*INTERLEAVE_TWO_PLANE以及COPY_BACK的情况*/
		{
			for(i=0;i<subs_count;i++)
			{
				die=ssd->channel_head[channel].chip_head[chip].token;
				if(i<ssd->parameter->die_chip)                                         /*为每个subs[i]获取ppn，i小于die_chip*/
				{
					plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
					get_ppn(ssd,channel,chip,die,plane,subs[i]);
					ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane+1)%ssd->parameter->plane_die;
				}
				else                                                                  
				{   
					/*********************************************************************************************************************************
					*超过die_chip的i所指向的subs[i]与subs[i%ssd->parameter->die_chip]获取相同位置的ppn
					*如果成功的获取了则令multi_plane_flag=TRUE并执行compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE_TWO_PLANE);
					*否则执行compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE);
					***********************************************************************************************************************************/
					state=make_level_page(ssd,subs[i%ssd->parameter->die_chip],subs[i]);
					if(state!=SUCCESS)                                                 
					{
						subs[i]=NULL;
						unvalid_subs_count++;
					}
					else
					{
						multi_plane_flag=TRUE;
					}
				}
				ssd->channel_head[channel].chip_head[chip].token=(die+1)%ssd->parameter->die_chip;
			}
			valid_subs_count=subs_count-unvalid_subs_count;
			ssd->interleave_count++;
			if(multi_plane_flag==TRUE)
			{
				ssd->inter_mplane_count++;
				compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE_TWO_PLANE);/*计算写子请求的处理时间，以写子请求的状态转变*/		
			}
			else
			{
				compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE);
			}
			return SUCCESS;
		}//if((command==INTERLEAVE_TWO_PLANE)||(command==COPY_BACK))
		else if(command==INTERLEAVE)
		{
			/***********************************************************************************************
			*INTERLEAVE高级命令的处理，这个处理比TWO_PLANE高级命令的处理简单
			*因为two_plane的要求是同一个die里面不同plane的同一位置的page，而interleave要求则是不同die里面的。
			************************************************************************************************/
			for(i=0;(i<subs_count)&&(i<ssd->parameter->die_chip);i++)
			{
				die=ssd->channel_head[channel].chip_head[chip].token;
				plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
				get_ppn(ssd,channel,chip,die,plane,subs[i]);
				ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane+1)%ssd->parameter->plane_die;
				ssd->channel_head[channel].chip_head[chip].token=(die+1)%ssd->parameter->die_chip;
				valid_subs_count++;
			}
			ssd->interleave_count++;
			compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE);
			return SUCCESS;
		}//else if(command==INTERLEAVE)
		else if(command==TWO_PLANE)
		{
			if(subs_count<2)
			{
				return ERROR;
			}
			die=ssd->channel_head[channel].chip_head[chip].token;
			for(j=0;j<subs_count;j++)
			{
				if(j==1)
				{
					state=find_level_page(ssd,channel,chip,die,subs[0],subs[1]);        /*寻找与subs[0]的ppn位置相同的subs[1]，执行TWO_PLANE高级命令*/
					if(state!=SUCCESS)
					{
						get_ppn_for_normal_command(ssd,channel,chip,subs[0]);           /*没找到，那么就当普通命令来处理*/
						return FAILURE;
					}
					else
					{
						valid_subs_count=2;
					}
				}
				else if(j>1)
				{
					state=make_level_page(ssd,subs[0],subs[j]);                         /*寻找与subs[0]的ppn位置相同的subs[j]，执行TWO_PLANE高级命令*/
					if(state!=SUCCESS)
					{
						for(k=j;k<subs_count;k++)
						{
							subs[k]=NULL;
						}
						subs_count=j;
						break;
					}
					else
					{
						valid_subs_count++;
					}
				}
			}//for(j=0;j<subs_count;j++)
			ssd->channel_head[channel].chip_head[chip].token=(die+1)%ssd->parameter->die_chip;
			ssd->m_plane_prog_count++;
			compute_serve_time(ssd,channel,chip,die,subs,valid_subs_count,TWO_PLANE);
			return SUCCESS;
		}//else if(command==TWO_PLANE)
		else 
		{
			return ERROR;
		}
	}//if (ssd->parameter->allocation_scheme==DYNAMIC_ALLOCATION)
	else                                                                              /*静态分配的情况*/
	{
		if((command==INTERLEAVE_TWO_PLANE)||(command==COPY_BACK))
		{
			for(die=0;die<ssd->parameter->die_chip;die++)
			{
				first_sub_in_die=NULL;
				for(plane=0;plane<ssd->parameter->plane_die;plane++)
				{
					sub=subs[die*ssd->parameter->plane_die+plane];
					if(sub!=NULL)
					{
						if(first_sub_in_die==NULL)
						{
							first_sub_in_die=sub;
							get_ppn(ssd,channel,chip,die,plane,sub);
						}
						else
						{
							state=make_level_page(ssd,first_sub_in_die,sub);
							if(state!=SUCCESS)
							{
								subs[die*ssd->parameter->plane_die+plane]=NULL;
								subs_count--;
								sub=NULL;
							}
							else
							{
								multi_plane_flag=TRUE;
							}
						}
					}
				}
			}
			if(multi_plane_flag==TRUE)
			{
				ssd->inter_mplane_count++;
				compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE_TWO_PLANE);
				return SUCCESS;
			}
			else
			{
				compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE);
				return SUCCESS;
			}
		}//if((command==INTERLEAVE_TWO_PLANE)||(command==COPY_BACK))
		else if(command==INTERLEAVE)
		{
			for(die=0;die<ssd->parameter->die_chip;die++)
			{	
				first_sub_in_die=NULL;
				for(plane=0;plane<ssd->parameter->plane_die;plane++)
				{
					sub=subs[die*ssd->parameter->plane_die+plane];
					if(sub!=NULL)
					{
						if(first_sub_in_die==NULL)
						{
							first_sub_in_die=sub;
							get_ppn(ssd,channel,chip,die,plane,sub);
							valid_subs_count++;
						}
						else
						{
							subs[die*ssd->parameter->plane_die+plane]=NULL;
							subs_count--;
							sub=NULL;
						}
					}
				}
			}
			if(valid_subs_count>1)
			{
				ssd->interleave_count++;
			}
			compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE);	
		}//else if(command==INTERLEAVE)
		else if(command==TWO_PLANE)
		{
			for(die=0;die<ssd->parameter->die_chip;die++)
			{	
				first_sub_in_die=NULL;
				second_sub_in_die=NULL;
				for(plane=0;plane<ssd->parameter->plane_die;plane++)
				{
					sub=subs[die*ssd->parameter->plane_die+plane];
					if(sub!=NULL)
					{	
						if(first_sub_in_die==NULL)
						{
							first_sub_in_die=sub;
						}
						else if(second_sub_in_die==NULL)
						{
							second_sub_in_die=sub;
							state=find_level_page(ssd,channel,chip,die,first_sub_in_die,second_sub_in_die);
							if(state!=SUCCESS)
							{
								subs[die*ssd->parameter->plane_die+plane]=NULL;
								subs_count--;
								second_sub_in_die=NULL;
								sub=NULL;
							}
							else
							{
								valid_subs_count=2;
							}
						}
						else
						{
							state=make_level_page(ssd,first_sub_in_die,sub);
							if(state!=SUCCESS)
							{
								subs[die*ssd->parameter->plane_die+plane]=NULL;
								subs_count--;
								sub=NULL;
							}
							else
							{
								valid_subs_count++;
							}
						}
					}//if(sub!=NULL)
				}//for(plane=0;plane<ssd->parameter->plane_die;plane++)
				if(second_sub_in_die!=NULL)
				{
					multi_plane_flag=TRUE;
					break;
				}
			}//for(die=0;die<ssd->parameter->die_chip;die++)
			if(multi_plane_flag==TRUE)
			{
				ssd->m_plane_prog_count++;
				compute_serve_time(ssd,channel,chip,die,subs,valid_subs_count,TWO_PLANE);
				return SUCCESS;
			}//if(multi_plane_flag==TRUE)
			else
			{
				i=0;
				sub=NULL;
				while((sub==NULL)&&(i<max_subs_num))
				{
					sub=subs[i];
					i++;
				}
				if(sub!=NULL)
				{
					get_ppn_for_normal_command(ssd,channel,chip,sub);
					return FAILURE;
				}
				else 
				{
					return ERROR;
				}
			}//else
		}//else if(command==TWO_PLANE)
		else
		{
			return ERROR;
		}
	}//elseb 静态分配的情况
	return SUCCESS;
}


/***********************************************
*函数的作用是让sub0，sub1的ppn所在的page位置相同
************************************************/
Status make_level_page(struct ssd_info * ssd, struct sub_request * sub0,struct sub_request * sub1)
{
	unsigned int i=0,j=0,k=0;
	unsigned int channel=0,chip=0,die=0,plane0=0,plane1=0,block0=0,block1=0,page0=0,page1=0;
	unsigned int active_block0=0,active_block1=0;
	unsigned int old_plane_token=0;
	
	if((sub0==NULL)||(sub1==NULL)||(sub0->location==NULL))
	{
		return ERROR;
	}
	channel=sub0->location->channel;
	chip=sub0->location->chip;
	die=sub0->location->die;
	plane0=sub0->location->plane;
	block0=sub0->location->block;
	page0=sub0->location->page;
	old_plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die].token;

	/***********************************************************************************************
	*动态分配的情况下
	*sub1的plane是根据sub0的ssd->channel_head[channel].chip_head[chip].die_head[die].token令牌获取的
	*sub1的channel，chip，die，block，page都和sub0的相同
	************************************************************************************************/
	if(ssd->parameter->allocation_scheme==DYNAMIC_ALLOCATION)                             
	{
		old_plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
		for(i=0;i<ssd->parameter->plane_die;i++)
		{
			plane1=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].add_reg_ppn==-1)
			{
				find_active_block(ssd,channel,chip,die,plane1);                               /*在plane1中找到活跃块*/
				block1=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].active_block;

				/*********************************************************************************************
				*只有找到的block1与block0相同，才能继续往下寻找相同的page
				*在寻找page时比较简单，直接用last_write_page（上一次写的page）+1就可以了。
				*如果找到的page不相同，那么如果ssd允许贪婪的使用高级命令，这样就可以让小的page 往大的page靠拢
				*********************************************************************************************/
				if(block1==block0)
				{
					page1=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].blk_head[block1].last_write_page+1;
					if(page1==page0)
					{
						break;
					}
					else if(page1<page0)
					{
						if (ssd->parameter->greed_MPW_ad==1)                                  /*允许贪婪的使用高级命令*/
						{                                                                   
							//make_same_level(ssd,channel,chip,die,plane1,active_block1,page0); /*小的page地址往大的page地址靠*/
							make_same_level(ssd,channel,chip,die,plane1,block1,page0);
							break;
						}    
					}
				}//if(block1==block0)
			}
			ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane1+1)%ssd->parameter->plane_die;
		}//for(i=0;i<ssd->parameter->plane_die;i++)
		if(i<ssd->parameter->plane_die)
		{
			flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page0);          /*这个函数的作用就是更新page1所对应的物理页以及location还有map表*/
			//flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page1);
			ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane1+1)%ssd->parameter->plane_die;
			return SUCCESS;
		}
		else
		{
			ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane_token;
			return FAILURE;
		}
	}
	else                                                                                      /*静态分配的情况*/
	{
		if((sub1->location==NULL)||(sub1->location->channel!=channel)||(sub1->location->chip!=chip)||(sub1->location->die!=die))
		{
			return ERROR;
		}
		plane1=sub1->location->plane;
		if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].add_reg_ppn==-1)
		{
			find_active_block(ssd,channel,chip,die,plane1);
			block1=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].active_block;
			if(block1==block0)
			{
				page1=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].blk_head[block1].last_write_page+1;
				if(page1>page0)
				{
					return FAILURE;
				}
				else if(page1<page0)
				{
					if (ssd->parameter->greed_MPW_ad==1)
					{ 
						//make_same_level(ssd,channel,chip,die,plane1,active_block1,page0);    /*小的page地址往大的page地址靠*/
                        make_same_level(ssd,channel,chip,die,plane1,block1,page0);
						flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page0);
						//flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page1);
						return SUCCESS;
					}
					else
					{
						return FAILURE;
					}					
				}
				else
				{
					flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page0);
					//flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page1);
					return SUCCESS;
				}
				
			}
			else
			{
				return FAILURE;
			}
			
		}
		else
		{
			return ERROR;
		}
	}
	
}

/******************************************************************************************************
*函数的功能是为two plane命令寻找出两个相同水平位置的页，并且修改统计值，修改页的状态
*注意这个函数与上一个函数make_level_page函数的区别，make_level_page这个函数是让sub1与sub0的page位置相同
*而find_level_page函数的作用是在给定的channel，chip，die中找两个位置相同的subA和subB。
*******************************************************************************************************/
Status find_level_page(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,struct sub_request *subA,struct sub_request *subB)       
{
	unsigned int i,planeA,planeB,active_blockA,active_blockB,pageA,pageB,aim_page,old_plane;
	struct gc_operation *gc_node;

	old_plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
    
	/************************************************************
	*在动态分配的情况下
	*planeA赋初值为die的令牌，如果planeA是偶数那么planeB=planeA+1
	*planeA是奇数，那么planeA+1变为偶数，再令planeB=planeA+1
	*************************************************************/
	if (ssd->parameter->allocation_scheme==0)                                                
	{
		planeA=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
		if (planeA%2==0)
		{
			planeB=planeA+1;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token=(ssd->channel_head[channel].chip_head[chip].die_head[die].token+2)%ssd->parameter->plane_die;
		} 
		else
		{
			planeA=(planeA+1)%ssd->parameter->plane_die;
			planeB=planeA+1;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token=(ssd->channel_head[channel].chip_head[chip].die_head[die].token+3)%ssd->parameter->plane_die;
		}
	} 
	else                                                                                     /*静态分配的情况，就直接赋值给planeA和planeB*/
	{
		planeA=subA->location->plane;
		planeB=subB->location->plane;
	}
	find_active_block(ssd,channel,chip,die,planeA);                                          /*寻找active_block*/
	find_active_block(ssd,channel,chip,die,planeB);
	active_blockA=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].active_block;
	active_blockB=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].active_block;

	
    
	/*****************************************************
	*如果active_block相同，那么就在这两个块中找相同的page
	*或者使用贪婪的方法找到两个相同的page
	******************************************************/
	if (active_blockA==active_blockB)
	{
		pageA=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockA].last_write_page+1;      
		pageB=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockB].last_write_page+1;
		if (pageA==pageB)                                                                    /*两个可用的页正好在同一个水平位置上*/
		{
			flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockA,pageA);
			flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockB,pageB);
		} 
		else
		{
			if (ssd->parameter->greed_MPW_ad==1)                                             /*贪婪地使用高级命令*/
			{
				if (pageA<pageB)                                                            
				{
					aim_page=pageB;
					make_same_level(ssd,channel,chip,die,planeA,active_blockA,aim_page);     /*小的page地址往大的page地址靠*/
				}
				else
				{
					aim_page=pageA;
					make_same_level(ssd,channel,chip,die,planeB,active_blockB,aim_page);    
				}
				flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockA,aim_page);
				flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockB,aim_page);
			} 
			else                                                                             /*不能贪婪的使用高级命令*/
			{
				subA=NULL;
				subB=NULL;
				ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane;
				return FAILURE;
			}
		}
	}
	/*********************************
	*如果找到的两个active_block不相同
	**********************************/
	else
	{   
		pageA=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockA].last_write_page+1;      
		pageB=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockB].last_write_page+1;
		if (pageA<pageB)
		{
			if (ssd->parameter->greed_MPW_ad==1)                                             /*贪婪地使用高级命令*/
			{
				/*******************************************************************************
				*在planeA中，与active_blockB相同位置的的block中，与pageB相同位置的page是可用的。
				*也就是palneA中的相应水平位置是可用的，将其最为与planeB中对应的页。
				*那么可也让planeA，active_blockB中的page往pageB靠拢
				********************************************************************************/
				if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockB].page_head[pageB].free_state==PG_SUB)    
				{
					make_same_level(ssd,channel,chip,die,planeA,active_blockB,pageB);
					flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockB,pageB);
					flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockB,pageB);
				}
                /********************************************************************************
				*在planeA中，与active_blockB相同位置的的block中，与pageB相同位置的page是可用的。
				*那么就要重新寻找block，需要重新找水平位置相同的一对页
				*********************************************************************************/
				else    
				{
					for (i=0;i<ssd->parameter->block_plane;i++)
					{
						pageA=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[i].last_write_page+1;
						pageB=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[i].last_write_page+1;
						if ((pageA<ssd->parameter->page_block)&&(pageB<ssd->parameter->page_block))
						{
							if (pageA<pageB)
							{
								if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[i].page_head[pageB].free_state==PG_SUB)
								{
									aim_page=pageB;
									make_same_level(ssd,channel,chip,die,planeA,i,aim_page);
									break;
								}
							} 
							else
							{
								if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[i].page_head[pageA].free_state==PG_SUB)
								{
									aim_page=pageA;
									make_same_level(ssd,channel,chip,die,planeB,i,aim_page);
									break;
								}
							}
						}
					}//for (i=0;i<ssd->parameter->block_plane;i++)
					if (i<ssd->parameter->block_plane)
					{
						flash_page_state_modify(ssd,subA,channel,chip,die,planeA,i,aim_page);
						flash_page_state_modify(ssd,subB,channel,chip,die,planeB,i,aim_page);
					} 
					else
					{
						subA=NULL;
						subB=NULL;
						ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane;
						return FAILURE;
					}
				}
			}//if (ssd->parameter->greed_MPW_ad==1)  
			else
			{
				subA=NULL;
				subB=NULL;
				ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane;
				return FAILURE;
			}
		}//if (pageA<pageB)
		else
		{
			if (ssd->parameter->greed_MPW_ad==1)     
			{
				if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockA].page_head[pageA].free_state==PG_SUB)
				{
					make_same_level(ssd,channel,chip,die,planeB,active_blockA,pageA);
					flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockA,pageA);
					flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockA,pageA);
				}
				else    
				{
					for (i=0;i<ssd->parameter->block_plane;i++)
					{
						pageA=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[i].last_write_page+1;
						pageB=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[i].last_write_page+1;
						if ((pageA<ssd->parameter->page_block)&&(pageB<ssd->parameter->page_block))
						{
							if (pageA<pageB)
							{
								if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[i].page_head[pageB].free_state==PG_SUB)
								{
									aim_page=pageB;
									make_same_level(ssd,channel,chip,die,planeA,i,aim_page);
									break;
								}
							} 
							else
							{
								if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[i].page_head[pageA].free_state==PG_SUB)
								{
									aim_page=pageA;
									make_same_level(ssd,channel,chip,die,planeB,i,aim_page);
									break;
								}
							}
						}
					}//for (i=0;i<ssd->parameter->block_plane;i++)
					if (i<ssd->parameter->block_plane)
					{
						flash_page_state_modify(ssd,subA,channel,chip,die,planeA,i,aim_page);
						flash_page_state_modify(ssd,subB,channel,chip,die,planeB,i,aim_page);
					} 
					else
					{
						subA=NULL;
						subB=NULL;
						ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane;
						return FAILURE;
					}
				}
			} //if (ssd->parameter->greed_MPW_ad==1) 
			else
			{
				if ((pageA==pageB)&&(pageA==0))
				{
					/*******************************************************************************************
					*下面是两种情况
					*1，planeA，planeB中的active_blockA，pageA位置都可用，那么不同plane 的相同位置，以blockA为准
					*2，planeA，planeB中的active_blockB，pageA位置都可用，那么不同plane 的相同位置，以blockB为准
					********************************************************************************************/
					if ((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockA].page_head[pageA].free_state==PG_SUB)
					  &&(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockA].page_head[pageA].free_state==PG_SUB))
					{
						flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockA,pageA);
						flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockA,pageA);
					}
					else if ((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockB].page_head[pageA].free_state==PG_SUB)
						   &&(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockB].page_head[pageA].free_state==PG_SUB))
					{
						flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockB,pageA);
						flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockB,pageA);
					}
					else
					{
						subA=NULL;
						subB=NULL;
						ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane;
						return FAILURE;
					}
				}
				else
				{
					subA=NULL;
					subB=NULL;
					ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane;
					return ERROR;
				}
			}
		}
	}

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].free_page<(ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->gc_hard_threshold))
	{
		gc_node=(struct gc_operation *)malloc(sizeof(struct gc_operation));
		alloc_assert(gc_node,"gc_node");
		memset(gc_node,0, sizeof(struct gc_operation));

		gc_node->next_node=NULL;
		gc_node->chip=chip;
		gc_node->die=die;
		gc_node->plane=planeA;
		gc_node->block=0xffffffff;
		gc_node->page=0;
		gc_node->state=GC_WAIT;
		gc_node->priority=GC_UNINTERRUPT;
		gc_node->next_node=ssd->channel_head[channel].gc_command;
		ssd->channel_head[channel].gc_command=gc_node;
		ssd->gc_request++;
	}
	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].free_page<(ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->gc_hard_threshold))
	{
		gc_node=(struct gc_operation *)malloc(sizeof(struct gc_operation));
		alloc_assert(gc_node,"gc_node");
		memset(gc_node,0, sizeof(struct gc_operation));

		gc_node->next_node=NULL;
		gc_node->chip=chip;
		gc_node->die=die;
		gc_node->plane=planeB;
		gc_node->block=0xffffffff;
		gc_node->page=0;
		gc_node->state=GC_WAIT;
		gc_node->priority=GC_UNINTERRUPT;
		gc_node->next_node=ssd->channel_head[channel].gc_command;
		ssd->channel_head[channel].gc_command=gc_node;
		ssd->gc_request++;
	}

	return SUCCESS;     
}

/*
*函数的功能是修改找到的page页的状态以及相应的dram中映射表的值
*/
struct ssd_info *flash_page_state_modify(struct ssd_info *ssd,struct sub_request *sub,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,unsigned int block,unsigned int page)
{
	unsigned int ppn,full_page;
	struct local *location;
	struct direct_erase *new_direct_erase,*direct_erase_node;
	
	full_page=~(0xffffffff<<ssd->parameter->subpage_page);
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page=page;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num--;

	if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page>63)
	{
		printf("error! the last write page larger than 64!!\n");
		while(1){}
	}

	if(ssd->dram->map->map_entry[sub->lpn].state==0)                                          /*this is the first logical page*/
	{
		ssd->dram->map->map_entry[sub->lpn].pn=find_ppn(ssd,channel,chip,die,plane,block,page);
		ssd->dram->map->map_entry[sub->lpn].state=sub->state;
	}
	else                                                                                      /*这个逻辑页进行了更新，需要将原来的页置为失效*/
	{
		ppn=ssd->dram->map->map_entry[sub->lpn].pn;
		location=find_location(ssd,ppn);
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=0;        //表示某一页失效，同时标记valid和free状态都为0
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=0;         //表示某一页失效，同时标记valid和free状态都为0
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=0;
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;
		if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num==ssd->parameter->page_block)    //该block中全是invalid的页，可以直接删除
		{
			new_direct_erase=(struct direct_erase *)malloc(sizeof(struct direct_erase));
			alloc_assert(new_direct_erase,"new_direct_erase");
			memset(new_direct_erase,0, sizeof(struct direct_erase));

			new_direct_erase->block=location->block;
			new_direct_erase->next_node=NULL;
			direct_erase_node=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
			if (direct_erase_node==NULL)
			{
				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node=new_direct_erase;
			} 
			else
			{
				new_direct_erase->next_node=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node=new_direct_erase;
			}
		}
		free(location);
		location=NULL;
		ssd->dram->map->map_entry[sub->lpn].pn=find_ppn(ssd,channel,chip,die,plane,block,page);
		ssd->dram->map->map_entry[sub->lpn].state=(ssd->dram->map->map_entry[sub->lpn].state|sub->state);
	}

	sub->ppn=ssd->dram->map->map_entry[sub->lpn].pn;
	sub->location->channel=channel;
	sub->location->chip=chip;
	sub->location->die=die;
	sub->location->plane=plane;
	sub->location->block=block;
	sub->location->page=page;
	
	ssd->program_count++;
	ssd->channel_head[channel].program_count++;
	ssd->channel_head[channel].chip_head[chip].program_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn=sub->lpn;	
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].valid_state=sub->state;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].free_state=((~(sub->state))&full_page);
	ssd->write_flash_count++;

	return ssd;
}


/********************************************
*函数的功能就是让两个位置不同的page位置相同
*********************************************/
struct ssd_info *make_same_level(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,unsigned int block,unsigned int aim_page)
{
	int i=0,step,page;
	struct direct_erase *new_direct_erase,*direct_erase_node;

	page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page+1;                  /*需要调整的当前块的可写页号*/
	step=aim_page-page;
	while (i<step)
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page+i].valid_state=0;     /*表示某一页失效，同时标记valid和free状态都为0*/
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page+i].free_state=0;      /*表示某一页失效，同时标记valid和free状态都为0*/
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page+i].lpn=0;

		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num++;

		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num--;

		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;

		i++;
	}

	ssd->waste_page_count+=step;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page=aim_page-1;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num==ssd->parameter->page_block)    /*该block中全是invalid的页，可以直接删除*/
	{
		new_direct_erase=(struct direct_erase *)malloc(sizeof(struct direct_erase));
		alloc_assert(new_direct_erase,"new_direct_erase");
		memset(new_direct_erase,0, sizeof(struct direct_erase));

		direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
		if (direct_erase_node==NULL)
		{
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node=new_direct_erase;
		} 
		else
		{
			new_direct_erase->next_node=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node=new_direct_erase;
		}
	}

	if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page>63)
		{
		printf("error! the last write page larger than 64!!\n");
		while(1){}
		}

	return ssd;
}


/****************************************************************************
*在处理高级命令的写子请求时，这个函数的功能就是计算处理时间以及处理的状态转变
*功能还不是很完善，需要完善，修改时注意要分为静态分配和动态分配两种情况
*****************************************************************************/
struct ssd_info *compute_serve_time(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,struct sub_request **subs, unsigned int subs_count,unsigned int command)
{
	unsigned int i=0;
	unsigned int max_subs_num=0;
	struct sub_request *sub=NULL,*p=NULL;
	struct sub_request * last_sub=NULL;
	max_subs_num=ssd->parameter->die_chip*ssd->parameter->plane_die;

	if((command==INTERLEAVE_TWO_PLANE)||(command==COPY_BACK))
	{
		int count = 0;
		int index = 0;
		for (i = 0; i < max_subs_num; i++)
		{
			if (subs[i] != NULL)
			{
				if (last_sub != NULL) {
					if (subs[i]->location->die == last_sub->location->die)
						index = i;
				}
				count++;
				last_sub = subs[i];
			}
		}
		
		last_sub = NULL;
		
		for (i = 0; i < max_subs_num; i++)
		{
			if (subs[i] != NULL)
			{

				subs[i]->current_state = SR_W_TRANSFER;
				if (last_sub == NULL)
				{
					subs[i]->current_time = ssd->current_time;
				}
				else
				{
					subs[i]->current_time = last_sub->next_state_predict_time;
				}

				subs[i]->next_state = SR_COMPLETE;

				subs[i]->next_state_predict_time = subs[i]->current_time + 7 * ssd->parameter->time_characteristics.tWC + (subs[i]->size * ssd->parameter->subpage_capacity) * ssd->parameter->time_characteristics.tWC;

				last_sub = subs[i];
				delete_w_sub_request(ssd, channel, subs[i]);
				//delete_from_channel(ssd,channel,subs[i]);
			}
		}

		if (count == 4) {
			for (i = 0; i < 2; i++)
			{
				if (subs[i] != NULL)
				{
					subs[i]->complete_time = subs[1]->next_state_predict_time + ssd->parameter->time_characteristics.tPROG;
				}
			}
			for (i = 2; i < 4; i++)
			{
				if (subs[i] != NULL)
				{
					subs[i]->complete_time = subs[3]->next_state_predict_time + ssd->parameter->time_characteristics.tPROG;
				}
			}

		}

		if (count == 3) {
			for (i = 0; i < 3-index; i++)
			{
				if (subs[i] != NULL)
				{
					subs[i]->complete_time = subs[2 - index]->next_state_predict_time + ssd->parameter->time_characteristics.tPROG;
				}
			}
			for (i = 3 - index; i < 3; i++)
			{
				if (subs[i] != NULL)
				{
					subs[i]->complete_time = subs[2]->next_state_predict_time + ssd->parameter->time_characteristics.tPROG;
				}
			}

		}

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time= last_sub->complete_time;

		ssd->channel_head[channel].current_state = CHANNEL_TRANSFER;
		ssd->channel_head[channel].current_time = ssd->current_time;
		ssd->channel_head[channel].next_state = CHANNEL_IDLE;
		ssd->channel_head[channel].next_state_predict_time = ssd->channel_head[channel].chip_head[chip].next_state_predict_time - ssd->parameter->time_characteristics.tPROG;
	}

	else if(command==TWO_PLANE)
	{
		for(i=0;i<max_subs_num;i++)
		{
			if(subs[i]!=NULL)
			{
				
				subs[i]->current_state=SR_W_TRANSFER;
				if(last_sub==NULL)
				{
					subs[i]->current_time=ssd->current_time;
				}
				else
				{
					subs[i]->current_time=last_sub->next_state_predict_time;
				}
				
				subs[i]->next_state=SR_COMPLETE;

				subs[i]->next_state_predict_time=subs[i]->current_time+7*ssd->parameter->time_characteristics.tWC+(subs[i]->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;

				last_sub=subs[i];
				delete_w_sub_request(ssd,channel,subs[i]);
				//delete_from_channel(ssd,channel,subs[i]);
			}
		}
		for (i = 0; i < max_subs_num; i++)
		{
			if (subs[i] != NULL)
			{
				subs[i]->complete_time = last_sub->next_state_predict_time + ssd->parameter->time_characteristics.tPROG;
			}
		}
		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=last_sub->next_state_predict_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
	}
	else if(command==INTERLEAVE)
	{
		for(i=0;i<max_subs_num;i++)
		{
			if(subs[i]!=NULL)
			{
				
				subs[i]->current_state=SR_W_TRANSFER;
				if(last_sub==NULL)
				{
					subs[i]->current_time=ssd->current_time;
				}
				else
				{
					subs[i]->current_time=last_sub->next_state_predict_time;
				}
				subs[i]->next_state=SR_COMPLETE;
				subs[i]->next_state_predict_time=subs[i]->current_time+7*ssd->parameter->time_characteristics.tWC+(subs[i]->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				subs[i]->complete_time=subs[i]->next_state_predict_time + ssd->parameter->time_characteristics.tPROG;
				last_sub=subs[i];
				delete_w_sub_request(ssd,channel,subs[i]);
				//delete_from_channel(ssd,channel,subs[i]);
			}
		}
		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=last_sub->next_state_predict_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
	}
	else if(command==NORMAL)
	{
		subs[0]->current_state=SR_W_TRANSFER;
		subs[0]->current_time=ssd->current_time;
		subs[0]->next_state=SR_COMPLETE;
		subs[0]->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(subs[0]->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;

		subs[0]->complete_time=subs[0]->next_state_predict_time + ssd->parameter->time_characteristics.tPROG;
		//delete_from_channel(ssd,channel,subs[0]);
		delete_w_sub_request(ssd,channel,subs[0]);
		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=subs[0]->next_state_predict_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
	}
	else
	{
		return NULL;
	}
	
	return ssd;

}


/*****************************************************************************************
*函数的功能就是把子请求从ssd->subs_w_head或者ssd->channel_head[channel].subs_w_head上删除
******************************************************************************************/
struct ssd_info *delete_from_channel(struct ssd_info *ssd,unsigned int channel,struct sub_request * sub_req)
{
	struct sub_request *sub,*p;
    
	/******************************************************************
	*完全动态分配子请求就在ssd->subs_w_head上
	*不是完全动态分配子请求就在ssd->channel_head[channel].subs_w_head上
	*******************************************************************/
	if ((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==0))    
	{
		sub=ssd->subs_w_head;
	} 
	else
	{
		sub=ssd->channel_head[channel].subs_w_head;
	}
	p=sub;

	while (sub!=NULL)
	{
		if (sub==sub_req)
		{
			if ((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==0))
			{
				if(ssd->parameter->ad_priority2==0)
				{
					ssd->real_time_subreq--;
				}
				
				if (sub==ssd->subs_w_head)                                                     /*将这个子请求从sub request队列中删除*/
				{
					if (ssd->subs_w_head!=ssd->subs_w_tail)
					{
						ssd->subs_w_head=sub->next_node;
						sub=ssd->subs_w_head;
						continue;
					} 
					else
					{
						ssd->subs_w_head=NULL;
						ssd->subs_w_tail=NULL;
						p=NULL;
						break;
					}
				}//if (sub==ssd->subs_w_head) 
				else
				{
					if (sub->next_node!=NULL)
					{
						p->next_node=sub->next_node;
						sub=p->next_node;
						continue;
					} 
					else
					{
						ssd->subs_w_tail=p;
						ssd->subs_w_tail->next_node=NULL;
						break;
					}
				}
			}//if ((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==0)) 
			else
			{
				if (sub==ssd->channel_head[channel].subs_w_head)                               /*将这个子请求从channel队列中删除*/
				{
					if (ssd->channel_head[channel].subs_w_head!=ssd->channel_head[channel].subs_w_tail)
					{
						ssd->channel_head[channel].subs_w_head=sub->next_node;
						sub=ssd->channel_head[channel].subs_w_head;
						continue;;
					} 
					else
					{
						ssd->channel_head[channel].subs_w_head=NULL;
						ssd->channel_head[channel].subs_w_tail=NULL;
						p=NULL;
						break;
					}
				}//if (sub==ssd->channel_head[channel].subs_w_head)
				else
				{
					if (sub->next_node!=NULL)
					{
						p->next_node=sub->next_node;
						sub=p->next_node;
						continue;
					} 
					else
					{
						ssd->channel_head[channel].subs_w_tail=p;
						ssd->channel_head[channel].subs_w_tail->next_node=NULL;
						break;
					}
				}//else
			}//else
		}//if (sub==sub_req)
		p=sub;
		sub=sub->next_node;
	}//while (sub!=NULL)

	return ssd;
}


struct ssd_info *un_greed_interleave_copyback(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,struct sub_request *sub1,struct sub_request *sub2)
{
	unsigned int old_ppn1,ppn1,old_ppn2,ppn2,greed_flag=0;

	old_ppn1=ssd->dram->map->map_entry[sub1->lpn].pn;
	get_ppn(ssd,channel,chip,die,sub1->location->plane,sub1);                                  /*找出来的ppn一定是发生在与子请求相同的plane中,才能使用copyback操作*/
	ppn1=sub1->ppn;

	old_ppn2=ssd->dram->map->map_entry[sub2->lpn].pn;
	get_ppn(ssd,channel,chip,die,sub2->location->plane,sub2);                                  /*找出来的ppn一定是发生在与子请求相同的plane中,才能使用copyback操作*/
	ppn2=sub2->ppn;

	if ((old_ppn1%2==ppn1%2)&&(old_ppn2%2==ppn2%2))
	{
		ssd->copy_back_count++;
		ssd->copy_back_count++;

		sub1->current_state=SR_W_TRANSFER;
		sub1->current_time=ssd->current_time;
		sub1->next_state=SR_COMPLETE;
		sub1->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub1->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub1->complete_time=sub1->next_state_predict_time;

		sub2->current_state=SR_W_TRANSFER;
		sub2->current_time=sub1->complete_time;
		sub2->next_state=SR_COMPLETE;
		sub2->next_state_predict_time=sub2->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub2->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub2->complete_time=sub2->next_state_predict_time;

		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=sub2->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;

		delete_from_channel(ssd,channel,sub1);
		delete_from_channel(ssd,channel,sub2);
	} //if ((old_ppn1%2==ppn1%2)&&(old_ppn2%2==ppn2%2))
	else if ((old_ppn1%2==ppn1%2)&&(old_ppn2%2!=ppn2%2))
	{
		ssd->interleave_count--;
		ssd->copy_back_count++;

		sub1->current_state=SR_W_TRANSFER;
		sub1->current_time=ssd->current_time;
		sub1->next_state=SR_COMPLETE;
		sub1->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub1->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub1->complete_time=sub1->next_state_predict_time;

		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=sub1->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;

		delete_from_channel(ssd,channel,sub1);
	}//else if ((old_ppn1%2==ppn1%2)&&(old_ppn2%2!=ppn2%2))
	else if ((old_ppn1%2!=ppn1%2)&&(old_ppn2%2==ppn2%2))
	{
		ssd->interleave_count--;
		ssd->copy_back_count++;

		sub2->current_state=SR_W_TRANSFER;
		sub2->current_time=ssd->current_time;
		sub2->next_state=SR_COMPLETE;
		sub2->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub2->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub2->complete_time=sub2->next_state_predict_time;

		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=sub2->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;

		delete_from_channel(ssd,channel,sub2);
	}//else if ((old_ppn1%2!=ppn1%2)&&(old_ppn2%2==ppn2%2))
	else
	{
		ssd->interleave_count--;

		sub1->current_state=SR_W_TRANSFER;
		sub1->current_time=ssd->current_time;
		sub1->next_state=SR_COMPLETE;
		sub1->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+2*(ssd->parameter->subpage_page*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub1->complete_time=sub1->next_state_predict_time;

		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=sub1->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;

		delete_from_channel(ssd,channel,sub1);
	}//else

	return ssd;
}


struct ssd_info *un_greed_copyback(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,struct sub_request *sub1)
{
	unsigned int old_ppn,ppn;

	old_ppn=ssd->dram->map->map_entry[sub1->lpn].pn;
	get_ppn(ssd,channel,chip,die,0,sub1);                                                     /*找出来的ppn一定是发生在与子请求相同的plane中,才能使用copyback操作*/
	ppn=sub1->ppn;
	
	if (old_ppn%2==ppn%2)
	{
		ssd->copy_back_count++;
		sub1->current_state=SR_W_TRANSFER;
		sub1->current_time=ssd->current_time;
		sub1->next_state=SR_COMPLETE;
		sub1->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub1->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub1->complete_time=sub1->next_state_predict_time;

		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=sub1->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
	}//if (old_ppn%2==ppn%2)
	else
	{
		sub1->current_state=SR_W_TRANSFER;
		sub1->current_time=ssd->current_time;
		sub1->next_state=SR_COMPLETE;
		sub1->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+2*(ssd->parameter->subpage_page*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub1->complete_time=sub1->next_state_predict_time;

		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=sub1->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
	}//else

	delete_from_channel(ssd,channel,sub1);

	return ssd;
}


/****************************************************************************************
*函数的功能是在处理读子请求的高级命令时，需要找与one_page相匹配的另外一个page即two_page
*没有找到可以和one_page执行two plane或者interleave操作的页,需要将one_page向后移一个节点
*当command == TWO_PLANE时，
*寻找与one_page在相同chip、die、block、page，不同plane，且当前状态也为SR_WAIT的two_page
*当command == INTERLEAVE时，
*寻找与one_page在相同chip，不同die，且当前状态也为SR_WAIT的two_page
*****************************************************************************************/
struct sub_request *find_interleave_twoplane_page(struct ssd_info *ssd, struct sub_request *one_page,unsigned int command)
{
	struct sub_request *two_page;

	if (one_page->current_state!=SR_WAIT)
	{
		return NULL;                                                            
	}
	if (((ssd->channel_head[one_page->location->channel].chip_head[one_page->location->chip].current_state==CHIP_IDLE)||((ssd->channel_head[one_page->location->channel].chip_head[one_page->location->chip].next_state==CHIP_IDLE)&&
		(ssd->channel_head[one_page->location->channel].chip_head[one_page->location->chip].next_state_predict_time<=ssd->current_time))))
	{
		two_page=one_page->next_node;
		if(command==TWO_PLANE)
		{
			while (two_page!=NULL)
		    {
				if (two_page->current_state!=SR_WAIT)
				{
					two_page=two_page->next_node;
				}
				else if ((one_page->location->chip==two_page->location->chip)&&(one_page->location->die==two_page->location->die)&&(one_page->location->block==two_page->location->block)&&(one_page->location->page==two_page->location->page))
				{//同一芯片，同一die中具有相同的block和page的两个请求处于不同的plane执行two_plane命令
					if (one_page->location->plane!=two_page->location->plane)
					{
						return two_page;                                                       /*找到了与one_page可以执行two plane操作的页*/
					}
					else
					{
						two_page=two_page->next_node;
					}
				}
				else
				{
					two_page=two_page->next_node;
				}
			}//while (two_page!=NULL)
			if (two_page==NULL)                                                               /*没有找到可以和one_page执行two_plane操作的页,需要将one_page向后移一个节点*/
			{
				return NULL;
			}
		}//if(command==TWO_PLANE)
		else if(command==INTERLEAVE)
		{
			while (two_page!=NULL)
		    {
				if (two_page->current_state!=SR_WAIT)
				{
					two_page=two_page->next_node;
				}
				else if ((one_page->location->chip==two_page->location->chip)&&(one_page->location->die!=two_page->location->die))  //interleave命令：同一chip中不同die的两个读请求同时执行
				{
					return two_page;                                                           /*找到了与one_page可以执行interleave操作的页*/
				}
				else
				{
					two_page=two_page->next_node;
				}
		     }
		    if (two_page==NULL)                                                                /*没有找到可以和one_page执行interleave操作的页,需要将one_page向后移一个节点*/
		    {
				return NULL;
		    }//while (two_page!=NULL)
		}//else if(command==INTERLEAVE)
		
	} 
	{
		return NULL;
	}
}


/*************************************************************************
*在处理读子请求高级命令时，利用这个还是查找可以执行高级命令的sub_request
**************************************************************************/
int find_interleave_twoplane_sub_request(struct ssd_info * ssd, unsigned int channel,struct sub_request **sub_request_one,struct sub_request **sub_request_two,unsigned int command)
{
	if (*sub_request_one == NULL) {
		*sub_request_one = ssd->channel_head[channel].subs_r_head;
		while (*sub_request_one != NULL)
		{
			*sub_request_two = find_interleave_twoplane_page(ssd, *sub_request_one, command);		/*找出两个可以做two_plane或者interleave的read子请求，包括位置条件和时间条件*/
			if (*sub_request_two == NULL)
			{
				*sub_request_one = (*sub_request_one)->next_node;
			}
			else if (*sub_request_two != NULL)                                                            /*找到了两个可以执行two plane操作的页*/
			{
				//printf("two_plane_read\n");
				break;
			}
		}
	}
	else {
		*sub_request_two = find_interleave_twoplane_page(ssd, *sub_request_one, command);		/*找出两个可以做two_plane或者interleave的read子请求，包括位置条件和时间条件*/
	}

	if (*sub_request_two!=NULL)
	{
		if (ssd->request_queue!=ssd->request_tail)      
		{                                                                                         /*确保interleave read的子请求是第一个请求的子请求*/
			if ((ssd->request_queue->lsn-ssd->parameter->subpage_page)<((*sub_request_one)->lpn*ssd->parameter->subpage_page))  
			{
				if ((ssd->request_queue->lsn+ssd->request_queue->size+ssd->parameter->subpage_page)>((*sub_request_one)->lpn*ssd->parameter->subpage_page))
				{
				}
				else
				{
					*sub_request_two=NULL;
				}
			}
			else
			{
				*sub_request_two=NULL;
			}
		}//if (ssd->request_queue!=ssd->request_tail) 
	}//if (sub_request_two!=NULL)

	if(*sub_request_two!=NULL)
	{
		//printf("two_plane_read\n");
		return SUCCESS;
	}
	else
	{
		return FAILURE;
	}

}


/**************************************************************************
*这个函数非常重要，读子请求的状态转变，以及时间的计算都通过这个函数来处理
*还有写子请求的执行普通命令时的状态，以及时间的计算也是通过这个函数来处理的
****************************************************************************/
Status go_one_step(struct ssd_info * ssd, struct sub_request * sub1,struct sub_request *sub2, unsigned int aim_state,unsigned int command)
{
	unsigned int i=0,j=0,k=0,m=0;
	long long time=0;
	struct sub_request * sub=NULL ; 
	struct sub_request * sub_twoplane_one=NULL, * sub_twoplane_two=NULL;
	struct sub_request * sub_interleave_one=NULL, * sub_interleave_two=NULL;
	struct local * location=NULL;
	if(sub1==NULL)
	{
		return ERROR;
	}
	
	/***************************************************************************************************
	*处理普通命令时，读子请求的目标状态分为以下几种情况SR_R_READ，SR_R_C_A_TRANSFER，SR_R_DATA_TRANSFER
	*写子请求的目标状态只有SR_W_TRANSFER
	****************************************************************************************************/
	if(command==NORMAL)
	{
		sub=sub1;
		location=sub1->location;
		switch(aim_state)						
		{	
			case SR_R_READ:
			{   
				/*****************************************************************************************************
			    *这个目标状态是指flash处于读数据的状态，sub的下一状态就应该是传送数据SR_R_DATA_TRANSFER
			    *这时与channel无关，只与chip有关所以要修改chip的状态为CHIP_READ_BUSY，下一个状态就是CHIP_DATA_TRANSFER
			    ******************************************************************************************************/
				sub->current_time= sub->next_state_predict_time;
				sub->current_state=SR_R_READ;
				sub->next_state=SR_R_DATA_TRANSFER;
				sub->next_state_predict_time=sub->current_time+ssd->parameter->time_characteristics.tR;

				if (ssd->channel_head[location->channel].next_state_predict_time <= sub->current_time) {
					ssd->channel_head[location->channel].current_state = CHANNEL_IDLE;
					ssd->channel_head[location->channel].current_time = ssd->current_time;
					ssd->channel_head[location->channel].next_state = CHANNEL_DATA_TRANSFER;
					ssd->channel_head[location->channel].next_state_predict_time = sub->next_state_predict_time;
				}

				if (ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time <= sub->current_time) {
					ssd->channel_head[location->channel].chip_head[location->chip].current_state = CHIP_READ_BUSY;
					ssd->channel_head[location->channel].chip_head[location->chip].current_time = ssd->current_time;
					ssd->channel_head[location->channel].chip_head[location->chip].next_state = CHIP_DATA_TRANSFER;
					ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time = sub->next_state_predict_time;
				}

				break;
			}
			case SR_R_C_A_TRANSFER:
			{   
				/*******************************************************************************************************
				*目标状态是命令地址传输时，sub的下一个状态就是SR_R_READ
				*这个状态与channel，chip有关，所以要修改channel，chip的状态分别为CHANNEL_C_A_TRANSFER，CHIP_C_A_TRANSFER
				*下一状态分别为CHANNEL_IDLE，CHIP_READ_BUSY
				*******************************************************************************************************/
				sub->current_time=ssd->current_time;									
				sub->current_state=SR_R_C_A_TRANSFER;									
				sub->next_state=SR_R_READ;									
				sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC;									
				sub->begin_time=ssd->current_time;

				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].add_reg_ppn=sub->ppn;
				ssd->read_count++;

				ssd->channel_head[location->channel].current_state=CHANNEL_C_A_TRANSFER;									
				ssd->channel_head[location->channel].current_time=ssd->current_time;										
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;								
				ssd->channel_head[location->channel].next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_C_A_TRANSFER;								
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;						
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_READ_BUSY;							
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC;
				
				break;
			
			}
			case SR_R_DATA_TRANSFER:
			{   
				/**************************************************************************************************************
				*目标状态是数据传输时，sub的下一个状态就是完成状态SR_COMPLETE
				*这个状态的处理也与channel，chip有关，所以channel，chip的当前状态变为CHANNEL_DATA_TRANSFER，CHIP_DATA_TRANSFER
				*下一个状态分别为CHANNEL_IDLE，CHIP_IDLE。
				***************************************************************************************************************/
				sub->current_time=ssd->current_time;					
				sub->current_state=SR_R_DATA_TRANSFER;		
				sub->next_state=SR_COMPLETE;				
				sub->next_state_predict_time=ssd->current_time+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;			
				sub->complete_time=sub->next_state_predict_time;

				ssd->channel_head[location->channel].current_state=CHANNEL_DATA_TRANSFER;		
				ssd->channel_head[location->channel].current_time=ssd->current_time;		
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;	
				ssd->channel_head[location->channel].next_state_predict_time=sub->next_state_predict_time;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_DATA_TRANSFER;				
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_IDLE;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=sub->next_state_predict_time;

				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].add_reg_ppn=-1;

				break;
			}
			case SR_W_TRANSFER:
			{
				/******************************************************************************************************
				*这是处理写子请求时，状态的转变以及时间的计算
				*虽然写子请求的处理状态也像读子请求那么多，但是写请求都是从上往plane中传输数据
				*这样就可以把几个状态当一个状态来处理，就当成SR_W_TRANSFER这个状态来处理，sub的下一个状态就是完成状态了
				*此时channel，chip的当前状态变为CHANNEL_TRANSFER，CHIP_WRITE_BUSY
				*下一个状态变为CHANNEL_IDLE，CHIP_IDLE
				*******************************************************************************************************/
				sub->current_time=ssd->current_time;
				sub->current_state=SR_W_TRANSFER;
				sub->next_state=SR_COMPLETE;
				sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				sub->complete_time=sub->next_state_predict_time;		
				time=sub->complete_time;
				
				ssd->channel_head[location->channel].current_state=CHANNEL_TRANSFER;										
				ssd->channel_head[location->channel].current_time=ssd->current_time;										
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;										
				ssd->channel_head[location->channel].next_state_predict_time=time;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_WRITE_BUSY;										
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;									
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_IDLE;										
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=time+ssd->parameter->time_characteristics.tPROG;
				
				break;
			}
			default :  return ERROR;
			
		}//switch(aim_state)	
	}//if(command==NORMAL)
	else if(command==TWO_PLANE)
	{   
		/**********************************************************************************************
		*高级命令TWO_PLANE的处理，这里的TWO_PLANE高级命令是读子请求的高级命令
		*状态转变与普通命令一样，不同的是在SR_R_C_A_TRANSFER时计算时间是串行的，因为共用一个通道channel
		*还有SR_R_DATA_TRANSFER也是共用一个通道
		**********************************************************************************************/
		if((sub1==NULL)||(sub2==NULL))
		{
			return ERROR;
		}
		sub_twoplane_one=sub1;
		sub_twoplane_two=sub2;
		location=sub1->location;
		
		switch(aim_state)						
		{	
			case SR_R_C_A_TRANSFER:
			{
				sub_twoplane_one->current_time=ssd->current_time;									
				sub_twoplane_one->current_state=SR_R_C_A_TRANSFER;									
				sub_twoplane_one->next_state=SR_R_READ;									
				sub_twoplane_one->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;									
				sub_twoplane_one->begin_time=ssd->current_time;

				ssd->channel_head[sub_twoplane_one->location->channel].chip_head[sub_twoplane_one->location->chip].die_head[sub_twoplane_one->location->die].plane_head[sub_twoplane_one->location->plane].add_reg_ppn=sub_twoplane_one->ppn;
				ssd->read_count++;

				sub_twoplane_two->current_time=ssd->current_time;									
				sub_twoplane_two->current_state=SR_R_C_A_TRANSFER;									
				sub_twoplane_two->next_state=SR_R_READ;									
				sub_twoplane_two->next_state_predict_time=sub_twoplane_one->next_state_predict_time;									
				sub_twoplane_two->begin_time=ssd->current_time;

				ssd->channel_head[sub_twoplane_two->location->channel].chip_head[sub_twoplane_two->location->chip].die_head[sub_twoplane_two->location->die].plane_head[sub_twoplane_two->location->plane].add_reg_ppn=sub_twoplane_two->ppn;
				ssd->read_count++;
				ssd->m_plane_read_count++;

				ssd->channel_head[location->channel].current_state=CHANNEL_C_A_TRANSFER;									
				ssd->channel_head[location->channel].current_time=ssd->current_time;										
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;								
				ssd->channel_head[location->channel].next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_C_A_TRANSFER;								
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;						
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_READ_BUSY;							
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;

				
				break;
			}
			case SR_R_DATA_TRANSFER:
			{
				sub_twoplane_one->current_time=ssd->current_time;					
				sub_twoplane_one->current_state=SR_R_DATA_TRANSFER;		
				sub_twoplane_one->next_state=SR_COMPLETE;				
				sub_twoplane_one->next_state_predict_time=ssd->current_time+(sub_twoplane_one->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;			
				sub_twoplane_one->complete_time=sub_twoplane_one->next_state_predict_time;
				
				sub_twoplane_two->current_time=sub_twoplane_one->next_state_predict_time;					
				sub_twoplane_two->current_state=SR_R_DATA_TRANSFER;		
				sub_twoplane_two->next_state=SR_COMPLETE;				
				sub_twoplane_two->next_state_predict_time=sub_twoplane_two->current_time+(sub_twoplane_two->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;			
				sub_twoplane_two->complete_time=sub_twoplane_two->next_state_predict_time;
				
				ssd->channel_head[location->channel].current_state=CHANNEL_DATA_TRANSFER;		
				ssd->channel_head[location->channel].current_time=ssd->current_time;		
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;	
				ssd->channel_head[location->channel].next_state_predict_time=sub_twoplane_two->next_state_predict_time;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_DATA_TRANSFER;				
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_IDLE;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time= sub_twoplane_two->next_state_predict_time;

				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].add_reg_ppn=-1;
			
				break;
			}
			default :  return ERROR;
		}//switch(aim_state)	
	}//else if(command==TWO_PLANE)
	else if(command==INTERLEAVE)
	{
		if((sub1==NULL)||(sub2==NULL))
		{
			return ERROR;
		}
		sub_interleave_one=sub1;
		sub_interleave_two=sub2;
		location=sub1->location;
		
		switch(aim_state)						
		{	
			case SR_R_C_A_TRANSFER:
			{
				sub_interleave_one->current_time=ssd->current_time;									
				sub_interleave_one->current_state=SR_R_C_A_TRANSFER;									
				sub_interleave_one->next_state=SR_R_READ;									
				sub_interleave_one->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC;									
				sub_interleave_one->begin_time=ssd->current_time;

				ssd->channel_head[sub_interleave_one->location->channel].chip_head[sub_interleave_one->location->chip].die_head[sub_interleave_one->location->die].plane_head[sub_interleave_one->location->plane].add_reg_ppn=sub_interleave_one->ppn;
				ssd->read_count++;

				sub_interleave_two->current_time=ssd->current_time;									
				sub_interleave_two->current_state=SR_R_C_A_TRANSFER;									
				sub_interleave_two->next_state=SR_R_READ;									
				sub_interleave_two->next_state_predict_time=sub_interleave_one->next_state_predict_time + 7 * ssd->parameter->time_characteristics.tWC;
				sub_interleave_two->begin_time=ssd->current_time;

				ssd->channel_head[sub_interleave_two->location->channel].chip_head[sub_interleave_two->location->chip].die_head[sub_interleave_two->location->die].plane_head[sub_interleave_two->location->plane].add_reg_ppn=sub_interleave_two->ppn;
				ssd->read_count++;
				ssd->interleave_read_count++;

				ssd->channel_head[location->channel].current_state=CHANNEL_C_A_TRANSFER;									
				ssd->channel_head[location->channel].current_time=ssd->current_time;										
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;								
				ssd->channel_head[location->channel].next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_C_A_TRANSFER;								
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;						
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_READ_BUSY;							
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;
				
				break;
						
			}
			case SR_R_DATA_TRANSFER:
			{
				sub_interleave_one->current_time=ssd->current_time - 7 * ssd->parameter->time_characteristics.tWC; // current_time是sub_interleave_two读介质阶段结束的时间，sub_interleave_one开始数据传输的时间在这之前
				sub_interleave_one->current_state=SR_R_DATA_TRANSFER;		
				sub_interleave_one->next_state=SR_COMPLETE;				
				sub_interleave_one->next_state_predict_time= sub_interleave_one->current_time +(sub_interleave_one->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;
				sub_interleave_one->complete_time=sub_interleave_one->next_state_predict_time;
				
				sub_interleave_two->current_time=sub_interleave_one->next_state_predict_time > ssd->current_time? sub_interleave_one->next_state_predict_time: ssd->current_time;
				sub_interleave_two->current_state=SR_R_DATA_TRANSFER;		
				sub_interleave_two->next_state=SR_COMPLETE;				
				sub_interleave_two->next_state_predict_time=sub_interleave_two->current_time+(sub_interleave_two->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;			
				sub_interleave_two->complete_time=sub_interleave_two->next_state_predict_time;

				ssd->channel_head[location->channel].current_state=CHANNEL_DATA_TRANSFER;		
				ssd->channel_head[location->channel].current_time=ssd->current_time;		
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;	
				ssd->channel_head[location->channel].next_state_predict_time=sub_interleave_two->next_state_predict_time;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_DATA_TRANSFER;				
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_IDLE;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=sub_interleave_two->next_state_predict_time;

				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].add_reg_ppn=-1;
				
				break;
			}
			default :  return ERROR;	
		}//switch(aim_state)				
	}//else if(command==INTERLEAVE)
	else
	{
		printf("\nERROR: Unexpected command !\n" );
		return ERROR;
	}

	return SUCCESS;
}

/**************************************************************************
*组合高级命令的处理，原函数无法组合
****************************************************************************/
Status go_one_step_interleave_twoplane(struct ssd_info* ssd, struct sub_request* sub1, struct sub_request* sub2, struct sub_request* sub3, struct sub_request* sub4, unsigned int aim_state) {
	struct local* location = NULL;
	struct sub_request* tmp1 = NULL, * tmp2 = NULL, * tmp3 = NULL,* tmp4 = NULL;
	if (sub1 == NULL)
	{
		return ERROR;
	}
	location = sub1->location;
	switch (aim_state)
	{
		case SR_R_C_A_TRANSFER: {
			if (sub2 != NULL && sub4 != NULL) {
				sub1->current_time = ssd->current_time;
				sub1->current_state = SR_R_C_A_TRANSFER;
				sub1->next_state = SR_R_READ;
				sub1->next_state_predict_time = ssd->current_time + 14 * ssd->parameter->time_characteristics.tWC;
				sub1->begin_time = ssd->current_time;

				ssd->channel_head[sub1->location->channel].chip_head[sub1->location->chip].die_head[sub1->location->die].plane_head[sub1->location->plane].add_reg_ppn = sub1->ppn;
				ssd->read_count++;

				sub2->current_time = ssd->current_time;
				sub2->current_state = SR_R_C_A_TRANSFER;
				sub2->next_state = SR_R_READ;
				sub2->next_state_predict_time = sub1->next_state_predict_time;
				sub2->begin_time = ssd->current_time;

				ssd->channel_head[sub2->location->channel].chip_head[sub2->location->chip].die_head[sub2->location->die].plane_head[sub2->location->plane].add_reg_ppn = sub2->ppn;
				ssd->read_count++;

				sub3->current_time = ssd->current_time;
				sub3->current_state = SR_R_C_A_TRANSFER;
				sub3->next_state = SR_R_READ;
				sub3->next_state_predict_time = ssd->current_time + 28 * ssd->parameter->time_characteristics.tWC;
				sub3->begin_time = ssd->current_time;

				ssd->channel_head[sub3->location->channel].chip_head[sub3->location->chip].die_head[sub3->location->die].plane_head[sub3->location->plane].add_reg_ppn = sub3->ppn;
				ssd->read_count++;

				sub4->current_time = ssd->current_time;
				sub4->current_state = SR_R_C_A_TRANSFER;
				sub4->next_state = SR_R_READ;
				sub4->next_state_predict_time = sub3->next_state_predict_time;
				sub4->begin_time = ssd->current_time;

				ssd->channel_head[sub4->location->channel].chip_head[sub4->location->chip].die_head[sub4->location->die].plane_head[sub4->location->plane].add_reg_ppn = sub4->ppn;
				ssd->read_count++;

				ssd->inter_mplane_count++;

				ssd->channel_head[location->channel].current_state = CHANNEL_C_A_TRANSFER;
				ssd->channel_head[location->channel].current_time = ssd->current_time;
				ssd->channel_head[location->channel].next_state = CHANNEL_IDLE;
				ssd->channel_head[location->channel].next_state_predict_time = ssd->current_time + 28 * ssd->parameter->time_characteristics.tWC;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state = CHIP_C_A_TRANSFER;
				ssd->channel_head[location->channel].chip_head[location->chip].current_time = ssd->current_time;
				ssd->channel_head[location->channel].chip_head[location->chip].next_state = CHIP_READ_BUSY;
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time = ssd->current_time + 28 * ssd->parameter->time_characteristics.tWC;
			}
			else if (sub2 == NULL) {
				sub3->current_time = ssd->current_time;
				sub3->current_state = SR_R_C_A_TRANSFER;
				sub3->next_state = SR_R_READ;
				sub3->next_state_predict_time = ssd->current_time + 14 * ssd->parameter->time_characteristics.tWC;
				sub3->begin_time = ssd->current_time;

				ssd->channel_head[sub3->location->channel].chip_head[sub3->location->chip].die_head[sub3->location->die].plane_head[sub3->location->plane].add_reg_ppn = sub3->ppn;
				ssd->read_count++;

				sub4->current_time = ssd->current_time;
				sub4->current_state = SR_R_C_A_TRANSFER;
				sub4->next_state = SR_R_READ;
				sub4->next_state_predict_time = sub3->next_state_predict_time;
				sub4->begin_time = ssd->current_time;

				ssd->channel_head[sub4->location->channel].chip_head[sub4->location->chip].die_head[sub4->location->die].plane_head[sub4->location->plane].add_reg_ppn = sub4->ppn;
				ssd->read_count++;

				sub1->current_time = ssd->current_time;
				sub1->current_state = SR_R_C_A_TRANSFER;
				sub1->next_state = SR_R_READ;
				sub1->next_state_predict_time = ssd->current_time + 21 * ssd->parameter->time_characteristics.tWC;
				sub1->begin_time = ssd->current_time;

				ssd->channel_head[sub1->location->channel].chip_head[sub1->location->chip].die_head[sub1->location->die].plane_head[sub1->location->plane].add_reg_ppn = sub1->ppn;
				ssd->read_count++;

				ssd->inter_mplane_count++;

				ssd->channel_head[location->channel].current_state = CHANNEL_C_A_TRANSFER;
				ssd->channel_head[location->channel].current_time = ssd->current_time;
				ssd->channel_head[location->channel].next_state = CHANNEL_IDLE;
				ssd->channel_head[location->channel].next_state_predict_time = ssd->current_time + 21 * ssd->parameter->time_characteristics.tWC;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state = CHIP_C_A_TRANSFER;
				ssd->channel_head[location->channel].chip_head[location->chip].current_time = ssd->current_time;
				ssd->channel_head[location->channel].chip_head[location->chip].next_state = CHIP_READ_BUSY;
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time = ssd->current_time + 21 * ssd->parameter->time_characteristics.tWC;
			}
			else if (sub4 == NULL) {
				sub1->current_time = ssd->current_time;
				sub1->current_state = SR_R_C_A_TRANSFER;
				sub1->next_state = SR_R_READ;
				sub1->next_state_predict_time = ssd->current_time + 14 * ssd->parameter->time_characteristics.tWC;
				sub1->begin_time = ssd->current_time;

				ssd->channel_head[sub1->location->channel].chip_head[sub1->location->chip].die_head[sub1->location->die].plane_head[sub1->location->plane].add_reg_ppn = sub1->ppn;
				ssd->read_count++;

				sub2->current_time = ssd->current_time;
				sub2->current_state = SR_R_C_A_TRANSFER;
				sub2->next_state = SR_R_READ;
				sub2->next_state_predict_time = sub1->next_state_predict_time;
				sub2->begin_time = ssd->current_time;

				ssd->channel_head[sub2->location->channel].chip_head[sub2->location->chip].die_head[sub2->location->die].plane_head[sub2->location->plane].add_reg_ppn = sub2->ppn;
				ssd->read_count++;

				sub3->current_time = ssd->current_time;
				sub3->current_state = SR_R_C_A_TRANSFER;
				sub3->next_state = SR_R_READ;
				sub3->next_state_predict_time = ssd->current_time + 21 * ssd->parameter->time_characteristics.tWC;
				sub3->begin_time = ssd->current_time;

				ssd->channel_head[sub3->location->channel].chip_head[sub3->location->chip].die_head[sub3->location->die].plane_head[sub3->location->plane].add_reg_ppn = sub3->ppn;
				ssd->read_count++;

				ssd->inter_mplane_count++;

				ssd->channel_head[location->channel].current_state = CHANNEL_C_A_TRANSFER;
				ssd->channel_head[location->channel].current_time = ssd->current_time;
				ssd->channel_head[location->channel].next_state = CHANNEL_IDLE;
				ssd->channel_head[location->channel].next_state_predict_time = ssd->current_time + 21 * ssd->parameter->time_characteristics.tWC;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state = CHIP_C_A_TRANSFER;
				ssd->channel_head[location->channel].chip_head[location->chip].current_time = ssd->current_time;
				ssd->channel_head[location->channel].chip_head[location->chip].next_state = CHIP_READ_BUSY;
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time = ssd->current_time + 21 * ssd->parameter->time_characteristics.tWC;
			}
			else{
				return ERROR;
			}
			break;
		}
		case SR_R_DATA_TRANSFER: {
			int count = 4;
			if (sub1->next_state_predict_time == ssd->current_time) {
				tmp3 = sub1;
				tmp4 = sub2;
				tmp1 = sub3;
				tmp2 = sub4;
			}
			else {
				tmp1 = sub1;
				tmp2 = sub2;
				tmp3 = sub3;
				tmp4 = sub4;
			}
			if (tmp2 == NULL || tmp4 == NULL)
				count = 3;
			tmp1->current_time = ssd->current_time - (count - 2) * 7 * ssd->parameter->time_characteristics.tWC; // current_time是sub_interleave_two读介质阶段结束的时间，sub_interleave_one开始数据传输的时间在这之前
			tmp1->current_state = SR_R_DATA_TRANSFER;
			tmp1->next_state = SR_COMPLETE;
			tmp1->next_state_predict_time = tmp1->current_time + (tmp1->size * ssd->parameter->subpage_capacity) * ssd->parameter->time_characteristics.tRC;
			tmp1->complete_time = tmp1->next_state_predict_time;

			if (tmp2 != NULL) {
				tmp2->current_time = tmp1->next_state_predict_time;
				tmp2->current_state = SR_R_DATA_TRANSFER;
				tmp2->next_state = SR_COMPLETE;
				tmp2->next_state_predict_time = tmp2->current_time + (tmp2->size * ssd->parameter->subpage_capacity) * ssd->parameter->time_characteristics.tRC;
				tmp2->complete_time = tmp2->next_state_predict_time;
				tmp3->current_time = tmp2->next_state_predict_time > ssd->current_time ? tmp2->next_state_predict_time : ssd->current_time;
			}
			else {
				tmp3->current_time = tmp1->next_state_predict_time > ssd->current_time ? tmp1->next_state_predict_time : ssd->current_time;
			}

			tmp3->current_state = SR_R_DATA_TRANSFER;
			tmp3->next_state = SR_COMPLETE;
			tmp3->next_state_predict_time = tmp3->current_time + (tmp3->size * ssd->parameter->subpage_capacity) * ssd->parameter->time_characteristics.tRC;
			tmp3->complete_time = tmp3->next_state_predict_time;

			if (tmp4 != NULL) {
				tmp4->current_time = tmp3->next_state_predict_time;
				tmp4->current_state = SR_R_DATA_TRANSFER;
				tmp4->next_state = SR_COMPLETE;
				tmp4->next_state_predict_time = tmp4->current_time + (tmp4->size * ssd->parameter->subpage_capacity) * ssd->parameter->time_characteristics.tRC;
				tmp4->complete_time = tmp4->next_state_predict_time;
				ssd->channel_head[location->channel].next_state_predict_time = tmp4->next_state_predict_time;
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time = tmp4->next_state_predict_time;
			}
			else {
				ssd->channel_head[location->channel].next_state_predict_time = tmp3->next_state_predict_time;
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time = tmp3->next_state_predict_time;
			}

			ssd->channel_head[location->channel].current_state = CHANNEL_DATA_TRANSFER;
			ssd->channel_head[location->channel].current_time = ssd->current_time;
			ssd->channel_head[location->channel].next_state = CHANNEL_IDLE;

			ssd->channel_head[location->channel].chip_head[location->chip].current_state = CHIP_DATA_TRANSFER;
			ssd->channel_head[location->channel].chip_head[location->chip].current_time = ssd->current_time;
			ssd->channel_head[location->channel].chip_head[location->chip].next_state = CHIP_IDLE;

			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].add_reg_ppn = -1;
		}
		default:  return ERROR;
	}
	return SUCCESS;
}