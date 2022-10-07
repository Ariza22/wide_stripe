/*****************************************************************************************************************************
This project was supported by the National Basic Research 973 Program of China under Grant No.2011CB302301
Huazhong University of Science and Technology (HUST)   Wuhan National Laboratory for Optoelectronics

FileName�� flash.c
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
*�������ֻ������д����
*��д����Ҫд������ҳ��Ϊ�������޷�������д����ҳ��ʱ����д֮ǰ��Ҫ�ȶ�ȡ��ҳ�е�����
*Ϊд����ĸ��²������������󣬲�����������ص���Ӧchannel�Ķ�������е�β��
*���ݲ����ļ��е���Ϣ�����ö�̬��̬�ķ�ʽΪд�������channel��chip��die��plane
*��д������ص���Ӧchannel��д������е�β��
*****************************************************************************************/
Status allocate_location(struct ssd_info * ssd ,struct sub_request *sub_req)
{
	struct sub_request * update=NULL;		//update��ʾ��д�������Ҫ���¶������Ķ�����
	unsigned int channel_num=0,chip_num=0,die_num=0,plane_num=0;
	struct local *location=NULL;

	channel_num=ssd->parameter->channel_number;
	chip_num=ssd->parameter->chip_channel[0];
	die_num=ssd->parameter->die_chip;
	plane_num=ssd->parameter->plane_die;
    
	
	if (ssd->parameter->allocation_scheme==0)                                          /*��̬��������*/
	{
		/******************************************************************
		*�ڶ�̬�����У���Ϊҳ�ĸ��²���ʹ�ò���copyback������
		*��Ҫ����һ�������󣬲���ֻ�������������ɺ���ܽ������ҳ��д����
		*******************************************************************/
		if (ssd->dram->map->map_entry[sub_req->lpn].state!=0)    
		{//lpn��Ӧ������ҳ�д��������
			if ((sub_req->state&ssd->dram->map->map_entry[sub_req->lpn].state)!=ssd->dram->map->map_entry[sub_req->lpn].state)
			{//��Ҫд�����ҳ��������ǰ����ҳ�е�������ҳ
				ssd->read_count++;
				ssd->update_read_count++;
#ifdef USE_WHITE_PARITY
				ssd->read_sub_request++;
#endif
				/***************************************************
				*�����µĶ����󣬲��ҹҵ�channel��subs_r_tail����β
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
		*�����Ƕ�̬����ļ������
		*0��ȫ��̬����
		*1����ʾchannel��package��die��plane��̬
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
		*�Ǿ�̬���䷽ʽ�����Կ��Խ���������������channel��chip��die��planeȫ���ó�
		*�ܹ���0,1,2,3,4,5,�����־�̬���䷽ʽ��
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
			/*���д�ص���������߼�ҳ�����Ը���֮ǰ��д�ص����ݣ���Ҫ����������*/ 
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

	/*��д������ص���Ӧchannel��д������е�β�� �����ھ�̬����Ͱ붯̬��������������Ҫ��д����ҵ�ͨ��д�������β����*/
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
*insert2buffer���������ר��Ϊд�������������������buffer_management�б����á�
********************************************************************************/
#ifdef NO_SUPERPAGE
struct ssd_info * insert2buffer(struct ssd_info *ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req)      
{
	int write_back_count,flag=0;                                                             /*flag��ʾΪд���������ڿռ��Ƿ���ɣ�0��ʾ��Ҫ��һ���ڣ�1��ʾ�Ѿ��ڿ�*/
	unsigned int i,lsn,hit_flag,add_flag,sector_count,active_region_flag=0,free_sector=0;
	struct buffer_group *buffer_node=NULL,*pt,*new_node=NULL,key;
	struct sub_request *sub_req=NULL,*update=NULL;
	
	
	unsigned int sub_req_state=0, sub_req_size=0,sub_req_lpn=0;

	#ifdef DEBUG
	printf("enter insert2buffer,  current time:%I64u, lpn:%d, state:%d,\n",ssd->current_time,lpn,state);
	#endif

	sector_count=size(state);		/*��Ҫд��buffer��sector����*/
	key.group=lpn;
	buffer_node= (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);    /*��ƽ���������Ѱ��buffer node*/ 
    
	/************************************************************************************************
	*û�����С�
	*��һ���������lpn�ж�����ҳ��Ҫд��buffer��ȥ����д�ص�lsn��Ϊ��lpn�ڳ�λ�ã�
	*���ȼ�Ҫ�����free sector����ʾ���ж��ٿ���ֱ��д��buffer�ڵ㣩��
	*���free_sector>=sector_count�����ж���Ŀռ乻lpn������д������Ҫ����д������
	*����û�ж���Ŀռ乩lpn������д����ʱ��Ҫ�ͷ�һ���ֿռ䣬����д�����󡣾�Ҫcreat_sub_request()
	*************************************************************************************************/
	if(buffer_node==NULL)
	{
		free_sector=ssd->dram->buffer->max_buffer_sector-ssd->dram->buffer->buffer_sector_count;   
		if(free_sector>=sector_count)
		{//�ж���Ŀռ乻lpn������д������Ҫ����д������
			flag=1;    
		}
		if(flag==0)     
		{//û�ж���Ŀռ乩lpn������д����ʱ��Ҫ�ͷ�һ���ֿռ䣬����д������
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
				*req��Ϊ�գ���ʾ���insert2buffer��������buffer_management�е��ã�������request����
				*reqΪ�գ���ʾ�����������process�����д���һ�Զ�ӳ���ϵ�Ķ���ʱ����Ҫ���������
				*�����ݼӵ�buffer�У�����ܲ���ʵʱ��д�ز�������Ҫ�����ʵʱ��д�ز��������������
				*������������������
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
				*д������뵽��ƽ�����������ʱ��Ҫ�޸�dram��buffer_sector_count��
				*ά��ƽ�����������avlTreeDel()��AVL_TREENODE_FREE()������
				*Ϊ��ά��LRU�㷨����Ҫ��λ��LRU���ж�β�Ľڵ��LRU������ɾ��
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
				
				write_back_count=write_back_count-sub_req->size;                            /*��Ϊ������ʵʱд�ز�������Ҫ������д�ز�����������*/
			}
		}
		
		/******************************************************************************
		*����һ��buffer node���������ҳ������ֱ�ֵ��������Ա��
		*��ӵ�LRU���ж��׺���ƽ���������ʽ�����buffer��
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
	*��buffer�����е����
	*��Ȼ�����ˣ��������е�ֻ��lpn���п���������д����ֻ����Ҫдlpn��һpage��ĳ����sub_page
	*��ʱ��Ҫ��һ�����ж�
	*****************************************************************************************/
	else
	{
		for(i=0;i<ssd->parameter->subpage_page;i++)
		{
			/*************************************************************
			*�ж�state��iλ�ǲ���1
			*�����жϵ�i��sector�Ƿ����buffer�У�1��ʾ���ڣ�0��ʾ�����ڡ�
			**************************************************************/
			if((state>>i)%2!=0)                                                         
			{
				lsn=lpn*ssd->parameter->subpage_page+i;
				hit_flag=0;
				hit_flag=(buffer_node->stored)&(0x00000001<<i);
				
				if(hit_flag!=0)				/*�����ˣ���Ҫ���ýڵ��Ƶ�buffer�Ķ��ף����ҽ����е�lsn���б��*/
				{	
					active_region_flag=1;		/*������¼�����buffer node�е�lsn�Ƿ����У����ں������ֵ���ж�*/

					if(req!=NULL)
					{
						if(ssd->dram->buffer->buffer_head!=buffer_node)     
						{//��buffer_node�ڵ��ƶ���buffer���еĶ���				
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
						req->complete_lsn_count++;                                        /*�ؼ� ����buffer������ʱ ����req->complete_lsn_count++��ʾ��buffer��д�����ݡ�*/					
					}
					else
					{
					}				
				}			
				else                 			
				{
					/************************************************************************************************************
					*��lsnû�����У����ǽڵ���buffer�У���Ҫ�����lsn�ӵ�buffer�Ķ�Ӧ�ڵ���
					*��buffer��ĩ����һ���ڵ㣬��һ���Ѿ�д�ص�lsn�ӽڵ���ɾ��(����ҵ��Ļ�)����������ڵ��״̬��ͬʱ������µ�
					*lsn�ӵ���Ӧ��buffer�ڵ��У��ýڵ������bufferͷ�����ڵĻ��������Ƶ�ͷ�������û���ҵ��Ѿ�д�ص�lsn����buffer
					*�ڵ���һ��group����д�أ�����������������������ϡ�������ǰ����һ��channel�ϡ�
					*��һ��:��buffer��β���Ѿ�д�صĽڵ�ɾ��һ����Ϊ�µ�lsn�ڳ��ռ䣬������Ҫ�޸Ķ�βĳ�ڵ��stored״̬���ﻹ��Ҫ
					*       ���ӣ���û�п���֮��ɾ����lsnʱ����Ҫ�����µ�д������д��LRU���Ľڵ㡣
					*�ڶ���:���µ�lsn�ӵ�������buffer�ڵ��С�
					*************************************************************************************************************/	
					ssd->dram->buffer->write_miss_hit++;
					
					if(ssd->dram->buffer->buffer_sector_count>=ssd->dram->buffer->max_buffer_sector)
					{
						if (buffer_node==ssd->dram->buffer->buffer_tail)                  /*������еĽڵ���buffer�����һ���ڵ㣬������������ڵ�*/
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
						/* ��:  ������������buffer�Ľڵ㲻Ӧ����ɾ����                        */
						/*      ��ȵ�д����֮�����ɾ��									             */
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

					/*�ڶ���:���µ�lsn�ӵ�������buffer�ڵ���*/	
					add_flag=0x00000001<<(lsn%ssd->parameter->subpage_page);
					
					if(ssd->dram->buffer->buffer_head!=buffer_node)                      /*�����buffer�ڵ㲻��buffer�Ķ��ף���Ҫ������ڵ��ᵽ����*/
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
	int write_back_count,flag=0;                                                             /*flag��ʾΪд���������ڿռ��Ƿ���ɣ�0��ʾ��Ҫ��һ���ڣ�1��ʾ�Ѿ��ڿ�*/
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

	sector_count=size(state);		/*��Ҫд��buffer��sector����*/
	key.group=lpn;
	buffer_node= (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);    /*��ƽ���������Ѱ��buffer node*/ 
	/************************************************************************************************
	*û�����С�
	*��һ���������lpn�ж�����ҳ��Ҫд��buffer��ȥ����д�ص�lsn��Ϊ��lpn�ڳ�λ�ã�
	*���ȼ�Ҫ�����free sector����ʾ���ж��ٿ���ֱ��д��buffer�ڵ㣩��
	*���free_sector>=sector_count�����ж���Ŀռ乻lpn������д������Ҫ����д������
	*����û�ж���Ŀռ乩lpn������д����ʱ��Ҫ�ͷ�һ���ֿռ䣬����д�����󡣾�Ҫcreat_sub_request()
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
		{//�ж���Ŀռ乻lpn������д������Ҫ����д������
			flag=1;    
		}
		//û�ж���Ŀռ乩lpn������д����ʱ��Ҫ�ͷ�һ���ֿռ䣨һ�����ͷ�һ������ҳ�Ĵ�С��������д������
		if(flag==0)     
		{
			//��superpage_buffer�е�����д��flash֮�󣬲Ž����´δ�buffer����ˢ������
			if(ssd->strip_bit == 0)
			{
				//��ȡ��Ҫд��������ecģʽ
				if(find_active_block(ssd, 0, 0, 0, 0) == FAILURE)
				{
					printf("get_ppn()\tERROR :there is no free page in channel:0, chip:0, die:0, plane:0\n");	
					return NULL;
				}

				active_superblock = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].active_block; //��ȡ��ǰ�������
				ec_mode = ssd->band_head[active_superblock].ec_modle;
				//�˴�ԭ��Ӧ����дʵ�ʵ�����ҳʱ�Ž�last_write_page��free_page_num�����޸ĵģ�����Ϊ�˴��Ѿ�Ϊ���������������ҳ��Ϊ�˷�ֹ�´η��������ʱ����Ҵ����active_block��page���ڴ˴����޸ģ�����ʵ��д����ҳ��ʱ�����ٴ��޸ģ�����ᷢ������
				active_superpage = ++(ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page); //��ȡ��Ҫд�ĳ���ҳ
				ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].free_page_num--;
				if(active_superpage>=(int)(ssd->parameter->page_block))
				{
					ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page=0;
					printf("insert2buffer();   error! the last write page larger than 64!!\n");
					return NULL;
				}
				first_parity = BAND_WITDH - (((active_superpage + 1) * ec_mode) % BAND_WITDH); //�ҳ���һ��У���λ��
				for(i = first_parity; i < first_parity + ec_mode; i++)
				{
					page_mask = page_mask | (1 << (i % BAND_WITDH));
				}
				//�Ƚ�buffer�е����band_width - ec_mode��buffer nodeǨ�Ƶ�superpage_buffer�У�Ȼ�����У�飬��һ���Խ�superpage��Сд������
				for(i = 0; i < BAND_WITDH - ec_mode; i++)
				{
					pos = find_first_zero(ssd, page_mask);
					page_mask = page_mask | (1 << (pos % BAND_WITDH));

					pt = ssd->dram->buffer->buffer_tail;
					//�Ӷ�������ɾ���ýڵ�
					avlTreeDel(ssd->dram->buffer, (TREE_NODE *) pt); 
					// ��LRU��ɾ���ýڵ�
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
					//���ýڵ�Ĵ����superpage��
					ssd->dram->superpage_buffer[pos].state = pt->stored; 
					ssd->dram->superpage_buffer[pos].size = size(pt->stored);
					ssd->dram->superpage_buffer[pos].lpn = pt->group;
					//����У�����ݵ�����״̬
					parity_state |= ssd->dram->superpage_buffer[pos].state;
					//�ͷŸýڵ�
					AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE *) pt);
					pt = NULL;
					//�޸�buffer�еĻ����������sector��
					ssd->dram->buffer->buffer_sector_count -= ssd->dram->superpage_buffer[pos].size;
				}
				//����У������
				for(i = first_parity; i < first_parity + ec_mode; i++)
				{
					ssd->dram->superpage_buffer[i % BAND_WITDH].lpn = -2;
					ssd->dram->superpage_buffer[i % BAND_WITDH].size = size(parity_state);
					ssd->dram->superpage_buffer[i % BAND_WITDH].state = parity_state;
				}
				//Ϊ����superpage���������󣬹��ڵ���Ӧ��ͨ����
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
		//����һ��buffer node���������ҳ������ֱ�ֵ��������Ա����ӵ�LRU���ж��׺���ƽ���������ʽ�����buffer��
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
	*��buffer�����е����
	*��Ȼ�����ˣ��������е�ֻ��lpn���п���������д����ֻ����Ҫдlpn��һpage��ĳ����sub_page
	*��ʱ��Ҫ��һ�����ж�
	*****************************************************************************************/
	else
	{
		hit_state = state & buffer_node->stored;
		miss_hit_state = (state ^ buffer_node->stored) & state;
		//�������е�lsn
		if(hit_state != 0)
		{
			if(req!=NULL)
			{
				//��buffer_node�ڵ��ƶ���buffer���еĶ���
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
				req->complete_lsn_count += size(hit_state);                                        /*�ؼ� ����buffer������ʱ ����req->complete_lsn_count++��ʾ��buffer��д�����ݡ�*/					
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
			//���δ���е�lsn�ܱ�bufferȫ�����棬�������д�������ڿռ䣬�������д����
			if(free_sector >= size(miss_hit_state))
			{
				if(ssd->dram->buffer->buffer_head!=buffer_node)                      /*�����buffer�ڵ㲻��buffer�Ķ��ף���Ҫ������ڵ��ᵽ����*/
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
				release_buffer_flag = 1; //δ���е�lsn���ܱ�bufferȫ���������ͷ�֮�����
				//printf("........xiashau\n");
			}

		}
	}

	if(release_buffer_flag == 1)
	{
		release_2_buffer++;
		ssd = buffer_2_superpage_buffer(ssd, sub, req);
	}
	// �ͷ�buffer�����㹻�Ŀռ����д����
	// δ����lpn��������µ�buffer_node�ҵ�buffer����β������lpnʱ���޸����е�buffer_node
	if(buffer_node == NULL)
	{
		//����һ��buffer node���������ҳ������ֱ�ֵ��������Ա����ӵ�LRU���ж��׺���ƽ���������ʽ�����buffer��
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
		if (ssd->dram->buffer->pTreeHeader == 0)
			printf("yes");
		avlTreeAdd(ssd->dram->buffer, (TREE_NODE *) new_node);
		ssd->dram->buffer->buffer_sector_count += size(state);
	}
	else if(release_buffer_flag == 1) //������buffer����δ����lsn
	{
			buffer_node->stored |= miss_hit_state;		
			buffer_node->dirty_clean |= miss_hit_state;	
			ssd->dram->buffer->buffer_sector_count +=size(miss_hit_state);
	}

	return ssd;
}

int write_flash_sub_count = 0;
// data_buffer����дʱ������������Ǩ�Ƶ�superpage_buffer��
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

	int wear_num = 0, high_wear_num = 0, idle_num = 0, healthy_num = 0;
	unsigned int wear_flag = 0, high_wear_flag = 0, idle_flag = 0, healthy_flag = 0;

	//��ȡ��Ҫд��������ecģʽ
	while (1) {
		if (find_superblock_for_write(ssd, 0, 0, 0, 0, req) == FAILURE)
		{
			printf("get_ppn()\tERROR :there is no free page in channel:0, chip:0, die:0, plane:0\n");
			getchar();
			return NULL;
		}

		active_superblock = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].active_block; //��ȡ��ǰ�������
		if ((ssd->band_head[active_superblock].pe_cycle - 1) % 1 == 0) { // ���Ƶ��
			// ����ĥ��������������֯
			update_block_wear_state(ssd, active_superblock);
		}

		for (int i = 0; i < BAND_WITDH; i++)
		{
			/*if (i == 5 || i == 7)
				ssd->dram->wear_map->wear_map_entry[i * ssd->parameter->block_plane + active_superblock].wear_state = 1;
			else if(i == 11)
				ssd->dram->wear_map->wear_map_entry[i * ssd->parameter->block_plane + active_superblock].wear_state = 2;*/
			switch (ssd->dram->wear_map->wear_map_entry[i * ssd->parameter->block_plane + active_superblock].wear_state) {
			case 1:
			{
				wear_num++;
				wear_flag |= 1 << i;
				break;
			}
			case 2:
			{
				high_wear_num++;
				high_wear_flag |= 1 << i;
				// ��ĥ��鲻����䣬��Ϊ���ÿ�
				ssd->dram->wear_map->wear_map_entry[i * ssd->parameter->block_plane + active_superblock].wear_state = 3;
				break;
			}
			case 3:
			{
				idle_num++;
				idle_flag |= 1 << i;
				break;
			}
			default: {
			}
			}
		}
		//û��ĥ���ʱĬ��ѡ�����һ�齡������ΪУ���
		if (wear_num == 0)
		{
			wear_num = 1;
			for (int i = BAND_WITDH - 1; i >= 0; i--) {
				if ((healthy_flag | (1 << i)) == healthy_flag) {
					wear_flag |= 1 << i;
					break;
				}
			}
		}

		healthy_num = BAND_WITDH - wear_num - high_wear_num - idle_num;
		healthy_flag = (~(idle_flag | wear_flag | high_wear_flag)) & ((1 << BAND_WITDH) - 1);
		// ����������ĥ��飬�޷�ʹ��WARD��֯��
		if (healthy_num < wear_num) {
			// ���øó����飬ʹ���¸�������
			ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].active_block++;
			continue;
		}
		// ʹ�øó����飬����������֯
		break;
	}

	int now_length = healthy_num + wear_num; //��ĥ��鲻����������֯
	for (int i = 0; i < wear_num; i++) {
		int parity_state = 0;
		int band_width = (now_length + wear_num - i - 1) / (wear_num - i); // ����ȡ��
		//printf("band_width:%d\n", band_width);
		now_length -= band_width;
		for (int j = 0; j < band_width - 1; j++) {
			int pos = find_first_one(ssd, healthy_flag);
			healthy_flag &= ~(1 << pos);

			pt = ssd->dram->buffer->buffer_tail;
			//�Ӷ�������ɾ���ýڵ�
			avlTreeDel(ssd->dram->buffer, (TREE_NODE*)pt);
			// ��LRU��ɾ���ýڵ�
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
			//���ýڵ�Ĵ����superpage��
			ssd->dram->superpage_buffer[pos].state = pt->stored;
			ssd->dram->superpage_buffer[pos].size = size(pt->stored);
			ssd->dram->superpage_buffer[pos].lpn = pt->group;
			ssd->dram->buffer->buffer_sector_count -= size(pt->stored);
			//����У�����ݵ�����״̬
			parity_state |= ssd->dram->superpage_buffer[pos].state;
			//�ͷŸýڵ�
			AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE*)pt);
			pt = NULL;
			//�޸�buffer�еĻ����������sector��
		}
		int parity_pos = find_first_one(ssd, wear_flag);
		wear_flag &= ~(1 << parity_pos);
		ssd->dram->superpage_buffer[parity_pos].lpn = -2;
		ssd->dram->superpage_buffer[parity_pos].size = size(parity_state);
		ssd->dram->superpage_buffer[parity_pos].state = parity_state;
	}
	active_superpage = ++(ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page); //��ȡ��Ҫд�ĳ���ҳ
	ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].free_page_num--;
	/*for(i = 0; i < ssd->parameter->channel_number; i++)
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
	active_superpage = ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page;*/
	
	
	if(active_superpage>=(int)(ssd->parameter->page_block))
	{
		ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[active_superblock].last_write_page=0;
		printf("insert2buffer();   error! the last write page larger than 64!!\n");
		return NULL;
	}

	//Ϊ��������ҳ��������
	int write_flag = idle_flag | high_wear_flag;
	for(i = 0; i < wear_num + healthy_num; i++)
	{
		int write_pos = find_first_zero(ssd, write_flag);
		write_flag |= 1 << write_pos;
		sub_req=NULL;
		sub_req_state = ssd->dram->superpage_buffer[write_pos].state;
		sub_req_size = ssd->dram->superpage_buffer[write_pos].size;
		sub_req_lpn = ssd->dram->superpage_buffer[write_pos].lpn;
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

	memset(ssd->dram->superpage_buffer, 0, BAND_WITDH * sizeof(struct sub_request));
	return ssd;
}

// ����uper����ĥ����
struct ssd_info* update_block_wear_state(struct ssd_info* ssd, int active_superblock) {
	unsigned int channel, chip, die, plane, page;
	for (channel = 0; channel < ssd->parameter->channel_number; channel++) {
		for (chip = 0; chip < ssd->parameter->chip_channel[0]; chip++) {
			for (die = 0; die < ssd->parameter->die_chip; die++) {
				for (plane = 0; plane < ssd->parameter->plane_die; plane++) {
					int rber = ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_superblock].rber_per_cycle[ssd->band_head[active_superblock].pe_cycle - 1]; //�ϴ�д�����������β����޸�������֯
					int block_id = (((channel * ssd->parameter->chip_channel[0] + chip) * ssd->parameter->die_chip + die) * ssd->parameter->plane_die + plane) * ssd->parameter->block_plane + active_superblock;
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

	sub = (struct sub_request*)malloc(sizeof(struct sub_request));                        /*����һ��������Ľṹ*/
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
	
	//�����������Ҫ���䵽��λ��
	plane_die = ssd->parameter->plane_die;
	plane_chip = plane_die * ssd->parameter->die_chip;
	plane_channel = plane_chip * ssd->parameter->chip_channel[0];

	channel = pos / plane_channel;
	chip = (pos % plane_channel) / plane_chip;
	die = (pos % plane_chip) / plane_die;
	plane = pos % plane_die;

    //sub��location                       
	sub->location=(struct local *)malloc(sizeof(struct local));
	alloc_assert(sub->location,"sub->location");
	memset(sub->location,0, sizeof(struct local));
	sub->location->channel = channel;
	sub->location->chip = chip;
	sub->location->die = die;
	sub->location->plane = plane;
	sub->location->block = block;
	sub->location->page = page;

	//sub�������ֶ�
	sub->ppn = find_ppn(ssd, channel, chip, die, plane, block, page);
	sub->operation = WRITE;
	sub->current_state=SR_WAIT;
	sub->current_time=ssd->current_time;
	sub->lpn=lpn;
	sub->size=sub_size;
	sub->state=state;
	if(lpn == -2)
	{
#ifdef CALCULATION
		sub->begin_time = ssd->current_time + 82000;
#else
		sub->begin_time = ssd->current_time;
#endif
	}
		
	else
		sub->begin_time = ssd->current_time;
	//�Ѹ�������뵽req��sub����ͷ
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

	//�鿴���д�����Ƿ������������
	if(lpn != -2 )
	{
		if (ssd->dram->map->map_entry[sub->lpn].state!=0)    
		{
			
			//�ȶϿ���ҳ��ppn_2_lpn��ӳ���ϵ
			old_ppn = ssd->dram->map->map_entry[sub->lpn].pn;
			location = find_location(ssd, old_ppn);  //��ȡ��ҳ��λ��
			if(ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn != -1)
			{
				//ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=0;             
				//ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=0;             
				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=-1;
				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;
			}
			if(location != NULL)
			{
				free(location);
				location = NULL;
			}
			
			//��Ҫд�����ҳ��������ǰ����ҳ�е�������ҳ���������������
			if ((sub->state&ssd->dram->map->map_entry[sub->lpn].state)!=ssd->dram->map->map_entry[sub->lpn].state)
			{
				ssd->read_count++;
				ssd->update_read_count++;
				ssd->read_sub_request++;

				//�����µĶ����󣬲��ҹҵ�channel��subs_r_tail����β
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
		// ����������ҵ�sub��update������
		if (update!=NULL)
		{
			sub->update=update;

			sub->state=(sub->state|update->state);
			sub->size=size(sub->state);
		}
	}
	
	// ��д������ҵ���Ӧ��ͨ����
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

	sub = (struct sub_request*)malloc(sizeof(struct sub_request));                        /*����һ��������Ľṹ*/
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

	//�Ѹ�������뵽req��sub����ͷ
	
	/*************************************************************************************
	*�ڶ�����������£���һ��ǳ���Ҫ����ҪԤ���ж϶�������������Ƿ����������������ͬ�ģ�
	*�еĻ�����������Ͳ�����ִ���ˣ����µ�������ֱ�Ӹ�Ϊ���
	*û�еĻ�������������ص���Ӧchannel�Ķ�������е�β��
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
	sub->size = size;                                                               /*��Ҫ�������������������С*/
	sub->ppn = ssd->dram->map->map_entry[lpn].pn;
	sub->operation = READ;
	sub->state = (ssd->dram->map->map_entry[lpn].state & 0x7fffffff);
	sub->type = FAULT_SUB;
	if(req != NULL)
	{
		if (sub->lpn == 3722304989)
			printf("yes");
		sub->next_subs = req->subs;
		req->subs = sub;
	}
#ifdef RECOVERY
#ifdef ACTIVE_RECOVERY

#else
	//����������������ҳ������ʱ������Ҫ������ȡ,���������¼�ڻָ��������ݽṹ�У����轫���������ͨ���������������
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
	*���¼��а���flag�����жϸö�������������Ƿ����������������ͬ�ģ�
	*1. �еĻ������µ�������ֱ�Ӹ�Ϊ���
	*2. û�еĻ�������������ص���Ӧchannel�Ķ�������е�β��
	************************************************************************/
	flag = 0;
	while(sub_r!=NULL)
	{
		//�����ͨ��������������ҵ���������flag��־Ϊ1
		if (sub_r->ppn == sub->ppn)
		{
			flag = 1;  
			break;
		}
		sub_r = sub_r->next_node;
	}
	//��δ�ҵ����򽫸ö���������ӵ���Ӧͨ���Ķ����������β�����ҵ�����ֱ�ӷ���ö�������
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
		ssd->read_sub_request++;  //��¼���������������
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
*�����Ĺ�����Ѱ�һ�Ծ�죬ӦΪÿ��plane�ж�ֻ��һ����Ծ�飬ֻ�������Ծ���в��ܽ��в���
***************************************************************************************/
Status  find_superblock_for_write(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane, struct request *req)
{
	unsigned int active_block=0;
	unsigned int free_page_num=0;
	unsigned int count=0;
	unsigned int free_superblock_num = 0, i;
	active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	free_page_num=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
	//last_write_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
	//Ѱ��active_block(�ӵ�ǰactive_block��ʼ��˳����ң��ҵ���һ��free_page_num��Ϊ0��block)
	while((free_page_num == 0)&&(count<ssd->parameter->block_plane))
	{
		active_block=(active_block+1)%ssd->parameter->block_plane;	
		free_page_num=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
		count++;
	}
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block=active_block;
	
	if(count < ssd->parameter->block_plane)
	{
		//��Ѱ�ҵ��µ�superblockʱ�������е�superblock�Ƿ�����GCҪ��
		if(count != 0)
		{
			ssd->free_superblock_num--;
			//ͳ�ƿ���superblock�ĸ���
			/*
			for(i = 0; i < ssd->parameter->block_plane; i++)
			{
				if(ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[i].last_write_page == -1)
				{
					free_superblock_num++;
				}
			}*/
			// ���е�superblock��С�����õ�gc��ֵʱ������GC����
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

Status  find_superblock_for_pre_read(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane)
{
	unsigned int active_block=0;
	unsigned int free_page_num=0;
	unsigned int count=0;
	active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	free_page_num=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
	//last_write_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
	//Ѱ��active_block(�ӵ�ǰactive_block��ʼ��˳����ң��ҵ���һ��free_page_num��Ϊ0��block)
	while((free_page_num == 0)&&(count<ssd->parameter->block_plane))
	{
		active_block=(active_block+1)%ssd->parameter->block_plane;	
		free_page_num=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
		count++;
	}
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block=active_block;
	
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
	//Ѱ��active_block(�ӵ�ǰactive_block��ʼ��˳����ң��ҵ���һ��free_page_num��Ϊ0��block)
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
*��������Ĺ��ܾ���һ��ģ��һ��ʵʵ���ڵ�д����
*���Ǹ������page����ز������Լ�����ssd��ͳ�Ʋ���
**************************************************/
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
	//ssd->write_flash_count++;    
	*ppn=find_ppn(ssd,channel,chip,die,plane,active_block,last_write_page);

	return SUCCESS;
}

/**********************************************************
*��������Ĺ����Ǹ���lpn��size��state����������
*������������ص���Ӧchannel�Ķ�������л�д������еĶ�β
***********************************************************/
struct sub_request * creat_sub_request(struct ssd_info * ssd,unsigned int lpn,int size,unsigned int state,struct request * req,unsigned int operation)
{
	struct sub_request* sub=NULL,* sub_r=NULL;
	struct channel_info * p_ch=NULL;
	struct local * loc=NULL;
	unsigned int flag=0;

	sub = (struct sub_request*)malloc(sizeof(struct sub_request));                        /*����һ��������Ľṹ*/
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
	//�Ѹ�������뵽req��sub����ͷ
	if(req!=NULL)
	{
		sub->next_subs = req->subs;
		req->subs = sub;
	}
	
	/*************************************************************************************
	*�ڶ�����������£���һ��ǳ���Ҫ����ҪԤ���ж϶�������������Ƿ����������������ͬ�ģ�
	*�еĻ�����������Ͳ�����ִ���ˣ����µ�������ֱ�Ӹ�Ϊ���
	*û�еĻ�������������ص���Ӧchannel�Ķ�������е�β��
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
		sub->size=size;                                                               /*��Ҫ�������������������С*/

		p_ch = &ssd->channel_head[loc->channel];	
		sub->ppn = ssd->dram->map->map_entry[lpn].pn;
		sub->operation = READ;
		sub->state=(ssd->dram->map->map_entry[lpn].state&0x7fffffff);
		sub_r=p_ch->subs_r_head;                                                      
		/***********************************************************************
		*���¼��а���flag�����жϸö�������������Ƿ����������������ͬ�ģ�
		*�еĻ������µ�������ֱ�Ӹ�Ϊ���
		*û�еĻ�������������ص���Ӧchannel�Ķ�������е�β��
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
	*д���������£�����Ҫ���õ�����allocate_location(ssd ,sub)������̬����Ͷ�̬������
	*��д����ҵ���Ӧchannel��д������еĶ�β�Ĺ���Ҳ��allocate_location(ssd ,sub)�����
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
      
		if (allocate_location(ssd ,sub)==ERROR)//����λ�ó���  free��������������Ϣ��
		{
			free(sub->location);
			sub->location=NULL;
			free(sub);
			sub=NULL;
			return NULL;
		}
			
	}
	else//�Ȳ��Ƕ�����Ҳ����д����   ��free����������Ϣ�������κδ���
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
*�����Ĺ������ڸ�����channel��chip��die����Ѱ�Ҷ�������
*����������ppnҪ����Ӧ��plane�ļĴ��������ppn���
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
*�����Ĺ�����Ѱ��д������
*���������1��Ҫ������ȫ��̬�������ssd->subs_w_head��������
*2��Ҫ�ǲ�����ȫ��̬������ô����ssd->channel_head[channel].subs_w_head�����ϲ���
********************************************************************************/
struct sub_request * find_write_sub_request(struct ssd_info * ssd, unsigned int channel)
{
	struct sub_request * sub=NULL,* p=NULL;
	struct sub_request*sub_r = NULL,*sub_gc = NULL;
	unsigned int break_flag = 0;
	unsigned int update_flag =0;
	unsigned int read_old_flag =0;
	
	if ((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==0))    /*����ȫ�Ķ�̬����*/
	{
		sub=ssd->subs_w_head;
#ifdef USE_WHITE_PARITY
		//��ͨ����д��������ж�ͷ������У��д����ֱ�ӷ��ظ�����
		/*if(ssd->channel_head[channel].subs_w_head != NULL)
			if(ssd->channel_head[channel].subs_w_head->lpn = -2)
			{
				sub = ssd->channel_head[channel].subs_w_head;
				return sub ;
			}
		*/
		//����get_ppn�Ĳ���ȷд�룬����ͨ���ϵ�д��������ۣ���������У�����ݻ���ԭ���ݶ�����ͨ����д����������ϲ�ѯ����δ�����������ssd��д����������в���
		if(ssd->channel_head[channel].subs_w_head != NULL){
			sub = ssd->channel_head[channel].subs_w_head;
			return sub;
		}
#ifdef GC
		if((ssd->request_queue!=NULL))//(ssd->gc_request < ssd->parameter->channel_number*5)&&
			if(sub != NULL) //ssd��д�����������д����
			{
				//ѭ������д��������У��ҵ���һ����ΪGC�����д������
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
				//�ҵ��˲�ΪGC��д��������sub�ᵽ���ף�head�ӵ�tail��
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
		{// ����current_stateΪSR_WAIT����û�и��²�����д������
#ifdef USE_WHITE_PARITY
			if(sub->type == GC_SUB)
			{
				if(ssd->dram->map->map_entry[sub->lpn].pn != 0)//˵�������󱻱����������
				{
					//ɾ����д����
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
				if (sub->update!=NULL)                                                      /*�������Ҫ��ǰ������ҳ*/
				{
					if ((sub->update->current_state==SR_COMPLETE)||((sub->update->next_state==SR_COMPLETE)&&(sub->update->next_state_predict_time<=ssd->current_time)))   //�����µ�ҳ�Ѿ�������
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

		if (sub==NULL)                                                                      /*���û���ҵ����Է���������󣬷���NULL*/
		{
			return NULL;
		}

		// ��sub��ssd->subs_w_head�������Ƴ�
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

		// ��sub��ӵ�channel��д��������еĶ�β
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
	*����ȫ��̬���䷽ʽ��������ʽ�������Ѿ����䵽�ض���channel��
	*��ֻ��Ҫ��channel���ҳ�׼�������������
	***********************************************************/
	else            
	{
		sub=ssd->channel_head[channel].subs_w_head;

		while(sub!=NULL)        						
		{		
			if(sub->lpn == 1825889)
				printf("");
			if(sub->current_state==SR_WAIT)								
			{// ����current_stateΪSR_WAIT����û�и��²�����д������
				update_flag = 1;
				read_old_flag = 1;
				if (sub->update!=NULL)    
				{
					update_flag = 0;
					if ((sub->update->current_state==SR_COMPLETE)||((sub->update->next_state==SR_COMPLETE)&&(sub->update->next_state_predict_time<=ssd->current_time)))   //�����µ�ҳ�Ѿ�������
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
*ר��Ϊ�����������ĺ���
*1����������ĵ�ǰ״̬��SR_R_C_A_TRANSFER
*2����������ĵ�ǰ״̬��SR_COMPLETE������һ״̬��SR_COMPLETE������һ״̬�����ʱ��ȵ�ǰʱ��С
**********************************************************************************************/
Status services_2_r_cmd_trans_and_complete(struct ssd_info * ssd)
{
	unsigned int i=0;
	struct sub_request * sub=NULL, * p=NULL;
#ifdef RECOVERY
	struct recovery_operation* rec = NULL;
	unsigned int flag, delete_flag = 0;
	long long recovery_time;
	unsigned int pos;
#ifdef ACTIVE_RECOVERY
	unsigned int j;
#endif
#endif
	for(i=0;i<ssd->parameter->channel_number;i++)		/*���ѭ��������Ҫchannel��ʱ��(�������Ѿ�����chip��chip��ready��Ϊbusy)�������������ʱ�������channel�Ķ�����ȡ��*/
	{
		sub=ssd->channel_head[i].subs_r_head;

		while(sub!=NULL)
		{
			delete_flag = 0;
			if(sub->current_state==SR_R_C_A_TRANSFER)		/*���������ϣ�����Ӧ��die��Ϊbusy��ͬʱ�޸�sub��״̬; �������ר�Ŵ���������ɵ�ǰ״̬Ϊ�������Ϊdie��ʼbusy��die��ʼbusy����ҪchannelΪ�գ����Ե����г�*/
			{
				if(sub->next_state_predict_time<=ssd->current_time)
				{
					go_one_step(ssd, sub,NULL, SR_R_READ,NORMAL);		/*״̬���䴦����*/

				}
			}
			else if((sub->current_state==SR_COMPLETE)||((sub->next_state==SR_COMPLETE)&&(sub->next_state_predict_time<=ssd->current_time)))					
			{// ���������Ѿ���ɣ������channel�Ķ����������ɾ��
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
					//��Ϊ��������ĳЩ������ǰ��ɣ�������ѯ���еĻָ�������ȷ�����ĸ��ָ�����������������
					while (rec != NULL)
					{
						//�͵�ǰ��Ҫ�ָ�������ҳ����ͬһ����ʱ�����ָ���Ҫ�Ķ������Ӧ����ɱ�־��1
						if ((flag = is_same_superpage(rec->sub, sub)) == TRUE)
						{
							pos = get_pos_in_band(ssd, sub->ppn);
							rec->sub_r_complete_flag |= 1 << pos;
							//printf("lpn = %d\tblock_for_recovery = %d\tcomplete_flag = %d\n", rec->sub->lpn, rec->block_for_recovery, rec->sub_r_complete_flag);
							//printf("broken_lpn = %d\trecovery_lpn = %d\tblock_for_recovery = %d\tcomplete_flag = %d\n", rec->sub->lpn, subs[i]->lpn, rec->block_for_recovery, rec->sub_r_complete_flag);
						}
						//�����ǰ�ָ���������Ķ���������ˣ�����н���ָ��������ָ�������ҳд�뻺��
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
						//����Ѿ�ȷ�������Ӧ�Ļָ�������������ѭ������������ͻָ�����Ϊ���һ��
						if (flag == TRUE)
						{
							break;
						}
						rec = rec->next_node;
					}
				}
#endif
			}
			if(!delete_flag)
				p=sub;
			sub=sub->next_node;
		}
	}
	
	return SUCCESS;
}

/**************************************************************************
*�������Ҳ��ֻ����������󣬴���chip��ǰ״̬��CHIP_WAIT��
*������һ��״̬��CHIP_DATA_TRANSFER������һ״̬��Ԥ��ʱ��С�ڵ�ǰʱ���chip
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
					//��channel chip die �в�ѯ����������ҵ�������󣬻Ὣ������Ӹ�ͨ��������ɾ����
					sub=find_read_sub_request(ssd,channel,chip,die);                   /*��channel,chip,die���ҵ���������*/
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
				*���ssd֧�ָ߼������ô���ǿ���һ����֧��AD_TWOPLANE_READ��AD_INTERLEAVE�Ķ�������
				*1���п��ܲ�����two plane����������������£���ͬһ��die�ϵ�����plane���������δ���
				*2���п��ܲ�����interleave����������������£�����ͬdie�ϵ�����plane���������δ���
				***************************************************************************************/
				if(((ssd->parameter->advanced_commands&AD_TWOPLANE_READ)==AD_TWOPLANE_READ)||((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE))
				{
					if ((ssd->parameter->advanced_commands&AD_TWOPLANE_READ)==AD_TWOPLANE_READ)     /*�п��ܲ�����two plane����������������£���ͬһ��die�ϵ�����plane���������δ���*/
					{
						sub_twoplane_one=sub;
						sub_twoplane_two=NULL;                                                      
						                                                                            /*Ϊ�˱�֤�ҵ���sub_twoplane_two��sub_twoplane_one��ͬ����add_reg_ppn=-1*/
						ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[sub->location->plane].add_reg_ppn=-1;
						sub_twoplane_two=find_read_sub_request(ssd,channel,chip,die);               /*����ͬ��channel,chip,die��Ѱ������һ����������*/
						
						/******************************************************
						*����ҵ�����ô��ִ��TWO_PLANE��״̬ת������go_one_step
						*���û�ҵ���ô��ִ����ͨ�����״̬ת������go_one_step
						******************************************************/
						if (sub_twoplane_two==NULL)
						{
							if ((ssd->parameter->advanced_commands & AD_INTERLEAVE) == AD_INTERLEAVE)      /*�п��ܲ�����interleave����������������£�����ͬdie�ϵ�����plane���������δ���*/
							{
								sub_interleave_one = sub;
								sub_interleave_two = NULL;
								ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[sub->location->plane].add_reg_ppn = -1;

								for (die1 = 0; die1 < ssd->parameter->die_chip; die1++)
								{
									if (die1 != die)
									{
										sub_interleave_two = find_read_sub_request(ssd, channel, chip, die1);    /*����ͬ��channel��chip����ͬ��die����������һ����������*/
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
					else if ((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)      /*�п��ܲ�����interleave����������������£�����ͬdie�ϵ�����plane���������δ���*/
					{
						sub_interleave_one=sub;
						sub_interleave_two=NULL;
						ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[sub->location->plane].add_reg_ppn=-1;
						
						for(die1=0;die1<ssd->parameter->die_chip;die1++)
						{	
							if(die1!=die)
							{
								sub_interleave_two=find_read_sub_request(ssd,channel,chip,die1);    /*����ͬ��channel��chip����ͬ��die����������һ����������*/
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
				else                                                                                 /*���ssd��֧�ָ߼�������ô��ִ��һ��һ����ִ�ж�������*/
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
//							//�����µ�д���󣬴�ʱ���lpn2ppn��ӳ����ppn2lpnd��ӳ���
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
//								ssd->channel_head[channel].next_state_predict_time=ssd->current_time+5*ssd->parameter->time_characteristics.tWC//���ʶ���֮����ܽ��в���������
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
//							//�ͷ�gc����������ڴ�ռ䡣
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
			
		if(*channel_busy_flag==1)
		{
			break;
		}
	}		
	return SUCCESS;
}
/*���������*/
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
*�������Ҳ��ֻ����������󣬲��Ҵ��ڵȴ�״̬�Ķ�������
*******************************************************/
int services_2_r_wait(struct ssd_info * ssd,unsigned int channel,unsigned int * channel_busy_flag, unsigned int * change_current_time_flag)
{
	unsigned int plane=0,address_ppn=0;
	struct sub_request * sub=NULL, * p=NULL,*p_gc = NULL;
	struct sub_request * sub_twoplane_one=NULL, * sub_twoplane_two=NULL;
	struct sub_request * sub_interleave_one=NULL, * sub_interleave_two=NULL;
	struct gc_operation*gc_node=NULL;
	struct direct_erase *new_direct_erase = NULL,*direct_erase_node= NULL;
	
	
	sub=ssd->channel_head[channel].subs_r_head;
	
	if ((ssd->parameter->advanced_commands&AD_TWOPLANE_READ)==AD_TWOPLANE_READ)         /*to find whether there are two sub request can be served by two plane operation*/
	{
		sub_twoplane_one=NULL;
		sub_twoplane_two=NULL;

		/*Ѱ����ִ��two_plane��������������*/
		find_interleave_twoplane_sub_request(ssd,channel,&sub_twoplane_one,&sub_twoplane_two,TWO_PLANE);
	

		if (sub_twoplane_two!=NULL) 		/*����ִ��two plane read ����*/
		{
			go_one_step(ssd, sub_twoplane_one,sub_twoplane_two, SR_R_C_A_TRANSFER,TWO_PLANE);
			//printf("two_plane_read\n");		
			*change_current_time_flag=0;
			*channel_busy_flag=1;		/*�Ѿ�ռ����������ڵ����ߣ�����ִ��die�����ݵĻش�*/
		} 
		else if((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)       /*û����������������page��������û��interleave read����ʱ��ֻ��ִ�е���page�Ķ�*/
		{
			while(sub!=NULL)		/*if there are read requests in queue, send one of them to target die*/			
			{		
				if(sub->current_state==SR_WAIT)									
				{	
					/*ע���¸�����ж�������services_2_r_data_trans���ж������Ĳ�ͬ*/
					if((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].current_state==CHIP_IDLE)||((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state==CHIP_IDLE)&&
						(ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state_predict_time<=ssd->current_time)))												
					{	
						go_one_step(ssd, sub,NULL, SR_R_C_A_TRANSFER,NORMAL);
									
						*change_current_time_flag=0;
						*channel_busy_flag=1;                                           /*�Ѿ�ռ����������ڵ����ߣ�����ִ��die�����ݵĻش�*/
						break;										
					}	
					else
					{
						                                                                /*��Ϊdie��busy���µ�����*/
					}
				}						
				sub=sub->next_node;								
			}
		}
	}
	if ((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE && (*channel_busy_flag != 1))               /*to find whether there are two sub request can be served by INTERLEAVE operation*/
	{
		sub_interleave_one=NULL;
		sub_interleave_two=NULL;
		find_interleave_twoplane_sub_request(ssd,channel,&sub_interleave_one,&sub_interleave_two,INTERLEAVE);
		
		if (sub_interleave_two!=NULL)                                                  /*����ִ��interleave read ����*/
		{

			go_one_step(ssd, sub_interleave_one,sub_interleave_two, SR_R_C_A_TRANSFER,INTERLEAVE);
						
			*change_current_time_flag=0;
			*channel_busy_flag=1;                                                      /*�Ѿ�ռ����������ڵ����ߣ�����ִ��die�����ݵĻش�*/
		} 
		else                                                                           /*û����������������page��ֻ��ִ�е���page�Ķ�*/
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
						*channel_busy_flag=1;                                          /*�Ѿ�ռ����������ڵ����ߣ�����ִ��die�����ݵĻش�*/
						break;										
					}	
					else
					{
						                                                               /*��Ϊdie��busy���µ�����*/
					}
				}						
				sub=sub->next_node;								
			}
		}
	}

	/*******************************
	*ssd����ִ��ִ�и߼�����������
	*******************************/
	if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE_READ)!=AD_TWOPLANE_READ))
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
					*channel_busy_flag=1;                                              /*�Ѿ�ռ����������ڵ����ߣ�����ִ��die�����ݵĻش�*/
					break;										
				}	
				else
				{
					                                                                   /*��Ϊdie��busy���µ�����*/
				}
			}						
			sub=sub->next_node;								
		}
	}

	return SUCCESS;
}

/*********************************************************************
*��һ��д�����������Ҫ�����������ɾ���������������ִ��������ܡ�
**********************************************************************/
int delete_w_sub_request(struct ssd_info * ssd, unsigned int channel, struct sub_request * sub )
{
	struct sub_request * p=NULL;
	if (sub==ssd->channel_head[channel].subs_w_head)                                   /*������������channel������ɾ��*/
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
*�����Ĺ��ܾ���ִ��copyback����Ĺ��ܣ�
*/
Status copy_back(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die,struct sub_request * sub)
{
	int old_ppn=-1, new_ppn=-1;
	long long time=0;
	if (ssd->parameter->greed_CB_ad==1)                                               /*����̰��ʹ��copyback�߼�����*/
	{
		old_ppn=-1;
		if (ssd->dram->map->map_entry[sub->lpn].state!=0)                             /*˵������߼�ҳ֮ǰ��д������Ҫʹ��copyback+random input�������ֱ��д��ȥ����*/
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
				old_ppn=ssd->dram->map->map_entry[sub->lpn].pn;                       /*��¼ԭ��������ҳ��������copybackʱ���ж��Ƿ�����ͬΪ���ַ����ż��ַ*/
			}															
		} 
		else
		{
			sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		}
		sub->complete_time=sub->next_state_predict_time;		
		time=sub->complete_time;

		get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);

		if (old_ppn!=-1)                                                              /*������copyback��������Ҫ�ж��Ƿ���������ż��ַ������*/
		{
			new_ppn=ssd->dram->map->map_entry[sub->lpn].pn;
			while (old_ppn%2!=new_ppn%2)                                              /*û��������ż��ַ���ƣ���Ҫ��������һҳ*/
			{
				get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);
				ssd->program_count--;
				ssd->write_flash_count--;
				ssd->waste_page_count++;
				new_ppn=ssd->dram->map->map_entry[sub->lpn].pn;
			}
		}
	} 
	else                                                                              /*����̰����ʹ��copyback�߼�����*/
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
				old_ppn=ssd->dram->map->map_entry[sub->lpn].pn;                       /*��¼ԭ��������ҳ��������copybackʱ���ж��Ƿ�����ͬΪ���ַ����ż��ַ*/
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
	*ִ��copyback�߼�����ʱ����Ҫ�޸�channel��chip��״̬���Լ�ʱ���
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
*��̬д������ʵ��
******************/
Status static_write(struct ssd_info * ssd, unsigned int channel,unsigned int chip, unsigned int die,struct sub_request * sub)
{
	long long time=0;
	if (ssd->dram->map->map_entry[sub->lpn].state!=0)                                    /*˵������߼�ҳ֮ǰ��д������Ҫʹ���ȶ���������д��ȥ������ֱ��д��ȥ����*/
	{
		if ((sub->state&ssd->dram->map->map_entry[sub->lpn].state)==ssd->dram->map->map_entry[sub->lpn].state)   /*���Ը���*/
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
	*ִ��copyback�߼�����ʱ����Ҫ�޸�channel��chip��״̬���Լ�ʱ���
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
*д������Ĵ�����
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
				//����ʹ��multi_plane
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
// NAND multi_plane�߼����������
int multi_plane_read(struct ssd_info *ssd, struct sub_request *sub0, struct sub_request *sub1)
{
	if(NAND_read(ssd, sub0) == FAILURE || NAND_read(ssd, sub1) == FAILURE)
	{
		printf("error!\tmulti_plane_read\n");
		return FAILURE;
	}

	//�޸�״̬
	return SUCCESS;
}
// NAND������
int NAND_read(struct ssd_info *ssd, struct sub_request *sub)
{
	// ʵ�ʶ�����
	ssd->read_count++;
	return SUCCESS; 
}
// ���������ķ���ʱ��
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
			//��Ϊ��������ĳЩ������ǰ��ɣ�������ѯ���еĻָ�������ȷ�����ĸ��ָ�����������������
			while(rec != NULL)
			{
				//�͵�ǰ��Ҫ�ָ�������ҳ����ͬһ����ʱ�����ָ���Ҫ�Ķ������Ӧ����ɱ�־��1
				if((flag = is_same_superpage(rec->sub, subs[i])) == TRUE)
				{
					pos = get_pos_in_band(ssd, subs[i]->ppn);
					rec->sub_r_complete_flag |= 1 << pos;
					//printf("lpn = %d\tblock_for_recovery = %d\tcomplete_flag = %d\n", rec->sub->lpn, rec->block_for_recovery, rec->sub_r_complete_flag);
					//printf("broken_lpn = %d\trecovery_lpn = %d\tblock_for_recovery = %d\tcomplete_flag = %d\n", rec->sub->lpn, subs[i]->lpn, rec->block_for_recovery, rec->sub_r_complete_flag);
				}
				//�����ǰ�ָ���������Ķ���������ˣ�����н���ָ��������ָ�������ҳд�뻺��
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
				//����Ѿ�ȷ�������Ӧ�Ļָ�������������ѭ������������ͻָ�����Ϊ���һ��
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
			//���ڶ���ͷ
			if (sub == ssd->channel_head[channel].subs_r_head)                              
			{
				//����������>=2
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
				//�����ڶ�β
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
		//����ڶ���ͷ��ֱ��ɾ��
	if (sub == ssd->channel_head[channel].subs_r_head)                                  
	{
		// 2�������Ͻڵ�
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
			//���ҵ��������ڶ�β��ɾ���ýڵ㲢�޸Ķ�βָ�룬����ֻ��ɾ���ýڵ�
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


// ����sub request�����ͷֱ��ͨ���Ķ�/д�����������ɾ��������
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
	if (sub == head)                                   /*������������channel������ɾ��*/
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
/*��plane����die�л�ȡ��һ�������channel��chip��die��planeλ����ͬ������*/
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
*д������Ĵ�����
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
/*��plane����die�л�ȡ��һ�������channel��chip��die��planeλ����ͬ������*/
struct sub_request *get_first_write_sub(struct ssd_info *ssd, unsigned int channel, unsigned int chip, unsigned int die, unsigned int *plane)
{
	struct sub_request *sub;
	sub = ssd->channel_head[channel].subs_w_head;
	if(plane == NULL)
	{
		while(sub != NULL)
		{
			if(sub->current_state == SR_WAIT)
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
			if(sub->current_state == SR_WAIT)
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
/*������������Ƿ�����multi_plane����*/
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
	//��alloction��ʱ���Ѿ��޸�����������
	//ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num--;
	//ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn = sub->lpn;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].valid_state= sub->state;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].free_state=((~sub->state)&full_page);
	//ÿ��д��������ĥ���
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
		// �������ڿ���ִ��multi_plane_write�������������ͨ��д
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
				if (d_sub->location->plane == 15)
					printf("error\n");
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
	//����ڶ���ͷ��ֱ��ɾ��
	if (sub == ssd->channel_head[channel].subs_w_head)                                  
	{
		// 2�������Ͻڵ�
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
			//���ҵ��������ڶ�β��ɾ���ýڵ㲢�޸Ķ�βָ�룬����ֻ��ɾ���ýڵ�
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
	unsigned int band_num = 0 ,p_ch = 0, first_parity = 0;//��block�еĵڼ���������������У��λ��λ�á�
	


	long long time=0;
	struct sub_request * sub=NULL, * p=NULL,*sub_r = NULL,*tmp = NULL,*sub_ch=NULL;
	struct sub_request * sub_twoplane_one=NULL, * sub_twoplane_two=NULL;
	struct sub_request * sub_interleave_one=NULL, * sub_interleave_two=NULL;

	first_parity = ssd->parameter->channel_number - ssd->band_head[ssd->current_band[0]].ec_modle; //�������е�һ��У����ͨ����
	//Ѱ�ֵ�У��ͨ������δд������ҳ����������ͨ��
	if((size(ssd->strip_bits[0]) < first_parity) && (channel >= first_parity)){
		//printf("The strip0 is not full\n");
		return FAILURE;
	}
	/************************************************************************************************************************
	*д��������������ط�һ����channel_head[channel].subs_w_head������һ����ssd->subs_w_head������Ҫ��֤������һ�����в�Ϊ��
	*ͬʱ������Ĵ�����Ϊ��̬����;�̬���䡣
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
		if (ssd->parameter->allocation_scheme==0)                                       /*��̬����*/
		{
			/*
			band_num = ssd->current_band;
			first_parity = ssd->parameter->channel_number - ssd->band_head[ssd->current_band].ec_modle; //�������е�һ��У����ͨ����
			//��ssd->sub_w_head��ȡ��һ��sub��д���ͨ����
			if(channel < first_parity)
			{*/
			chip_token=ssd->channel_head[channel].token;                            /*chip����*/
			if (*channel_busy_flag==0)
			{
				//��chip�ĵ�ǰ״̬���У�����һ��״̬Ҳ���У�������һ��״̬��Ԥ��ʱ��С�ڵ��ڵ�ǰʱ��
				if((ssd->channel_head[channel].chip_head[chip_token].current_state==CHIP_IDLE)||((ssd->channel_head[channel].chip_head[chip_token].next_state==CHIP_IDLE)&&(ssd->channel_head[channel].chip_head[chip_token].next_state_predict_time<=ssd->current_time)))				
				{
					die_token=ssd->channel_head[channel].chip_head[chip_token].token;	//��ȡ��оƬ�е�die����
					//��ʹ�ø߼�����
					if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))       
					{
						sub=find_write_sub_request(ssd,channel); //��ssd��д����������в��ҵ�ǰ״̬ΪSR_WAIT����û�и��²�����д�������ҵ�֮������ӵ���Ӧͨ���е�д���������
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
							plane_token=ssd->channel_head[channel].chip_head[chip_token].die_head[die_token].token; //��ȡ��ǰоƬ��ǰdie�е�plane����
							//get_ppn ������һ���µ�����ҳ������д�롣��ô���������д������+1��
							//����������Ѿ�д��������ҳ����ôд��У�����ݡ�
							if(get_ppn(ssd,channel,chip_token,die_token,plane_token,sub) == NULL)  //�ڵ�ǰpalne�еĻ�Ծ���л�ȡ����д���page������lpn-ppnӳ���ϵ�����ǿ��е�һ��ҳ������������ӳ���ϵ����ǰ����д������ҳ�������дУ�����󣬲������Ӧͨ����
								return FAILURE;
							
							//ssd->current_band = get_band_id_from_ppn(ssd, sub->ppn);
							*change_current_time_flag=0;

							if(ssd->parameter->ad_priority2==0)
							{
								ssd->real_time_subreq--;
							}
							go_one_step(ssd,sub,NULL,SR_W_TRANSFER,NORMAL);       /*ִ����ͨ��״̬��ת�䡣*/
							delete_w_sub_request(ssd,channel,sub);                /*ɾ����������д������*/
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
										ssd->write_flash_gc_count++;//move number �ڶ�����ʱ�Ѿ�����
									}
								}
							#endif
							
							*channel_busy_flag=1;
							/**************************************************************************
							*����forѭ��ǰ���޸�����
							*�����token�ı仯��ȫȡ���������channel chip die plane��д�Ƿ�ɹ� 
							*�ɹ��˾�break û�ɹ�token��Ҫ�仯ֱ���ҵ���д�ɹ���channel chip die plane
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
							*channel_busy_flag=1;                                 /*ִ����һ�����󣬴��������ݣ�ռ�������ߣ���Ҫ��������һ��channel*/
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
		else if(ssd->parameter->allocation_scheme==1)                                     /*��̬����*/
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
							                                                            
							if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))     /*��ִ�и߼�����*/
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
										if ((sub->current_state==SR_WAIT)&&(sub->location->channel==channel)&&(sub->location->chip==chip)&&(sub->location->die==die))      /*����������ǵ�ǰdie������*/
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
											copy_back(ssd, channel,chip, die,sub);      /*�������ִ��copyback�߼������ô���ú���copy_back(ssd, channel,chip, die,sub)����д������*/
											*change_current_time_flag=0;
										} 
										else
										{
											static_write(ssd, channel,chip, die,sub);   /*����ִ��copyback�߼������ô����static_write(ssd, channel,chip, die,sub)����������д������*/ 
											*change_current_time_flag=0;
										}
										
										delete_w_sub_request(ssd,channel,sub);
										*channel_busy_flag=1;
										break;
									}
								}
							} 
							else                                                        /*����߼�����*/
							{
								if (dynamic_advanced_process(ssd,channel,chip)==NULL)
								{
									*channel_busy_flag=0;
								}
								else
								{
									*channel_busy_flag=1;                               /*ִ����һ�����󣬴��������ݣ�ռ�������ߣ���Ҫ��������һ��channel*/
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
*д������Ĵ�����
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
	unsigned int band_num = 0 ,p_ch = 0 ;//��block�еĵڼ���������������У��λ��λ�á�
	


	long long time=0;
	struct sub_request * sub=NULL, * p=NULL,*sub_r = NULL,*tmp = NULL,*sub_ch=NULL;
	struct sub_request * sub_twoplane_one=NULL, * sub_twoplane_two=NULL;
	struct sub_request * sub_interleave_one=NULL, * sub_interleave_two=NULL;

	/************************************************************************************************************************
	*д��������������ط�һ����channel_head[channel].subs_w_head������һ����ssd->subs_w_head������Ҫ��֤������һ�����в�Ϊ��
	*ͬʱ������Ĵ�����Ϊ��̬����;�̬���䡣
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
		if (ssd->parameter->allocation_scheme==0)                                       /*��̬����*/
		{
			#ifdef USE_EC
				band_num = ssd->current_band;
				p_ch = ssd->parameter->channel_number - ssd->band_head[ssd->current_band].ec_modle; //�������е�һ��У����ͨ����
				//if(p_ch > channel) //����������ҳ��δд��
					//return SUCCESS; //д�������������أ������������Ĵ���
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

				if(p_ch == channel)//��ͨ����У��ҳ���ڵ�ͨ����
					if((ssd->strip_bit == 0)||(ssd->page_num % PARITY_SIZE != 0))//������������ҳ��û��д����
						return SUCCESS;//д�������������أ������������Ĵ���/**/

				if(ssd->strip_bit&(1<<channel))//�����ͨ���Ѿ�д�������ˣ�
				//	if(size(ssd->strip_bit) != PARITY_SIZE)//���Ҹ�����д���������������д��У�顣
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
				chip_token=ssd->channel_head[channel].token;                            /*chip����*/
#ifdef USE_WHITE_PARITY
				/*if(ssd->strip_bit != 0){
					while(ssd->chip_num != chip_token) //�����ƶ����������ڵ�chip
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
					//��chip�ĵ�ǰ״̬���У�����һ��״̬Ҳ���У�������һ��״̬��Ԥ��ʱ��С�ڵ��ڵ�ǰʱ��
					if((ssd->channel_head[channel].chip_head[chip_token].current_state==CHIP_IDLE)||((ssd->channel_head[channel].chip_head[chip_token].next_state==CHIP_IDLE)&&(ssd->channel_head[channel].chip_head[chip_token].next_state_predict_time<=ssd->current_time)))				
					{
						if((ssd->channel_head[channel].subs_w_head==NULL)&&(ssd->subs_w_head==NULL)) 
						{
							break;
						}
						die_token=ssd->channel_head[channel].chip_head[chip_token].token;	//��ȡ��оƬ�е�die����
						//��ʹ�ø߼�����
						if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))       
						{
							sub=find_write_sub_request(ssd,channel); //��ssd��д����������в��ҵ�ǰ״̬ΪSR_WAIT����û�и��²�����д�������ҵ�֮������ӵ���Ӧͨ���е�д���������
							if(sub==NULL)
							{
								break;
							}
							/*if(sub->lpn ==2282361)
								printf("");*/
							if(sub->current_state==SR_WAIT)
							{
								plane_token=ssd->channel_head[channel].chip_head[chip_token].die_head[die_token].token; //��ȡ��ǰоƬ��ǰdie�е�plane����
								//get_ppn ������һ���µ�����ҳ������д�롣��ô���������д������+1��
								//����������Ѿ�д��������ҳ����ôд��У�����ݡ�
								get_ppn(ssd,channel,chip_token,die_token,plane_token,sub);  //�ڵ�ǰpalne�еĻ�Ծ���л�ȡ����д���page������lpn-ppnӳ���ϵ�����ǿ��е�һ��ҳ������������ӳ���ϵ����ǰ����д������ҳ�������дУ�����󣬲������Ӧͨ����
								//plane����+1
								ssd->channel_head[channel].chip_head[chip_token].die_head[die_token].token=(ssd->channel_head[channel].chip_head[chip_token].die_head[die_token].token+1)%ssd->parameter->plane_die;

								*change_current_time_flag=0;

								if(ssd->parameter->ad_priority2==0)
								{
									ssd->real_time_subreq--;
								}
								go_one_step(ssd,sub,NULL,SR_W_TRANSFER,NORMAL);       /*ִ����ͨ��״̬��ת�䡣*/
								delete_w_sub_request(ssd,channel,sub);                /*ɾ����������д������*/
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
											ssd->write_flash_gc_count++;//move number �ڶ�����ʱ�Ѿ�����
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
								*����forѭ��ǰ���޸�����
								*�����token�ı仯��ȫȡ���������channel chip die plane��д�Ƿ�ɹ� 
								*�ɹ��˾�break û�ɹ�token��Ҫ�仯ֱ���ҵ���д�ɹ���channel chip die plane
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
								*channel_busy_flag=1;                                 /*ִ����һ�����󣬴��������ݣ�ռ�������ߣ���Ҫ��������һ��channel*/
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
		else if(ssd->parameter->allocation_scheme==1)                                     /*��̬����*/
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
							                                                            
							if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))     /*��ִ�и߼�����*/
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
										if ((sub->current_state==SR_WAIT)&&(sub->location->channel==channel)&&(sub->location->chip==chip)&&(sub->location->die==die))      /*����������ǵ�ǰdie������*/
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
											copy_back(ssd, channel,chip, die,sub);      /*�������ִ��copyback�߼������ô���ú���copy_back(ssd, channel,chip, die,sub)����д������*/
											*change_current_time_flag=0;
										} 
										else
										{
											static_write(ssd, channel,chip, die,sub);   /*����ִ��copyback�߼������ô����static_write(ssd, channel,chip, die,sub)����������д������*/ 
											*change_current_time_flag=0;
										}
										
										delete_w_sub_request(ssd,channel,sub);
										*channel_busy_flag=1;
										break;
									}
								}
							} 
							else                                                        /*����߼�����*/
							{
								if (dynamic_advanced_process(ssd,channel,chip)==NULL)
								{
									*channel_busy_flag=0;
								}
								else
								{
									*channel_busy_flag=1;                               /*ִ����һ�����󣬴��������ݣ�ռ�������ߣ���Ҫ��������һ��channel*/
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
*�����������Ҫ���������ض��������д�������״̬�仯����
*********************************************************/
struct ssd_info* process(struct ssd_info* ssd)
{

	/*********************************************************************************************************
	*flag_die��ʾ�Ƿ���Ϊdie��busy��������ʱ��ǰ����-1��ʾû�У���-1��ʾ��������
	*flag_die��ֵ��ʾdie��,old ppn��¼��copyback֮ǰ������ҳ�ţ������ж�copyback�Ƿ���������ż��ַ�����ƣ�
	*two_plane_bit[8],two_plane_place[8]�����Ա��ʾͬһ��channel��ÿ��die��������������
	*chg_cur_time_flag��Ϊ�Ƿ���Ҫ������ǰʱ��ı�־λ������Ϊchannel����busy������������ʱ����Ҫ������ǰʱ�䣻
	*��ʼ��Ϊ��Ҫ��������Ϊ1�����κ�һ��channel�����˴��������������ʱ�����ֵ��Ϊ0����ʾ����Ҫ������
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
	*�ж��Ƿ��ж�д�������������ôflag��Ϊ0��û��flag��Ϊ1
	*��flagΪ1ʱ����ssd����gc������ʱ�Ϳ���ִ��gc����
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
		if (ssd->gc_request > 0)                                                            /*SSD����gc����������*/
		{
			gc(ssd, 0, 1);                                                                  /*���gcҪ������channel�����������*/
		}
		return ssd;
	}
	else
	{
		ssd->flag = 0;
	}

	time = ssd->current_time;
	if (ssd->channel_head[1].subs_r_head == NULL && ssd->channel_head[1].subs_r_tail != NULL)
		printf("yes");
	services_2_r_cmd_trans_and_complete(ssd);                                            /*����ǰ״̬��SR_R_C_A_TRANSFER���ߵ�ǰ״̬��SR_COMPLETE��������һ״̬��SR_COMPLETE������һ״̬Ԥ��ʱ��С�ڵ�ǰ״̬ʱ��*/

	random_num = ssd->program_count % ssd->parameter->channel_number;                        /*����һ�����������֤ÿ�δӲ�ͬ��channel��ʼ��ѯ*/

	/*****************************************
	*ѭ����������channel�ϵĶ�д������
	*���������������д���ݣ�����Ҫռ�����ߣ�
	******************************************/
	for (chan = 0; chan < ssd->parameter->channel_number; chan++)
	{
		i = (random_num + chan) % ssd->parameter->channel_number;
		flag = 0;
		flag_gc = 0;                                                                       /*ÿ�ν���channelʱ����gc�ı�־λ��Ϊ0��Ĭ����Ϊû�н���gc����*/
		if ((ssd->channel_head[i].current_state == CHANNEL_IDLE) || (ssd->channel_head[i].next_state == CHANNEL_IDLE && ssd->channel_head[i].next_state_predict_time <= ssd->current_time))
		{
			if (ssd->gc_request > 0)                                                       /*��gc��������Ҫ����һ�����ж�*/
			{
				if (ssd->channel_head[i].gc_command != NULL)
				{
					flag_gc = gc(ssd, i, 0);                                                 /*gc��������һ��ֵ����ʾ�Ƿ�ִ����gc���������ִ����gc���������channel�����ʱ�̲��ܷ�������������*/
				}
				if (flag_gc == 1)                                                          /*ִ�й�gc��������Ҫ�����˴�ѭ��*/
				{
					continue;
				}
			}

			sub = ssd->channel_head[i].subs_r_head;                                        /*�ȴ��������*/
			services_2_r_wait(ssd, i, &flag, &chg_cur_time_flag);                           /*�����ڵȴ�״̬�Ķ�������*/

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
	*flag_die��ʾ�Ƿ���Ϊdie��busy��������ʱ��ǰ����-1��ʾû�У���-1��ʾ��������
	*flag_die��ֵ��ʾdie��,old ppn��¼��copyback֮ǰ������ҳ�ţ������ж�copyback�Ƿ���������ż��ַ�����ƣ�
	*two_plane_bit[8],two_plane_place[8]�����Ա��ʾͬһ��channel��ÿ��die��������������
	*chg_cur_time_flag��Ϊ�Ƿ���Ҫ������ǰʱ��ı�־λ������Ϊchannel����busy������������ʱ����Ҫ������ǰʱ�䣻
	*��ʼ��Ϊ��Ҫ��������Ϊ1�����κ�һ��channel�����˴��������������ʱ�����ֵ��Ϊ0����ʾ����Ҫ������
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
	*�ж��Ƿ��ж�д�������������ôflag��Ϊ0��û��flag��Ϊ1
	*��flagΪ1ʱ����ssd����gc������ʱ�Ϳ���ִ��gc����
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
		if (ssd->gc_request>0)                                                            /*SSD����gc����������*/
		{
#ifdef USE_WHITE_PARITY
			white_parity_gc(ssd);
#else
			gc(ssd,0,1);                                                                  /*���gcҪ������channel�����������*/
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
	services_2_r_cmd_trans_and_complete(ssd);		/*����ǰ״̬��SR_R_C_A_TRANSFER���ߵ�ǰ״̬��SR_COMPLETE��������һ״̬��SR_COMPLETE������һ״̬Ԥ��ʱ��С�ڵ�ǰ״̬ʱ��*/
#ifdef USE_WHITE_PARITY
	random_num=ssd->band_channel; 
	ssd->change_current_t_flag = 1;
#else
	random_num=ssd->program_count%ssd->parameter->channel_number;	/*����һ�����������֤ÿ�δӲ�ͬ��channel��ʼ��ѯ*/
#endif
	/*****************************************
	*ѭ����������channel�ϵĶ�д������
	*���������������д���ݣ�����Ҫռ�����ߣ�
	******************************************/
	////��ssd->token��λ�ÿ�ʼ�������ڰ�����˳��д�룬���������������1������������е�д�������ڵ�ǰ����д�꣬��ֻ�账��������ͨ���Ķ�����
	//��2������������е�д��������Ҫд��������������ڴ���������ͨ���Ķ�������ٽ�����д������д��
	for(chan=0;chan<ssd->parameter->channel_number;chan++)	     
	{
		i=(random_num+chan)%ssd->parameter->channel_number;  
		data_page_num = ssd->parameter->channel_number - ssd->band_head[ssd->current_band[0]].ec_modle;
		//i = chan;
		flag=0;	
		#ifdef USE_WHITE_PARITY 
		ssd->channel_head[i].busy_flag = flag;
		#endif	
		flag_gc=0; 		/*ÿ�ν���channelʱ����gc�ı�־λ��Ϊ0��Ĭ����Ϊû�н���gc����*/
		// ��ͨ����ǰ״̬ΪIDLE������һ��״̬ΪIDLE����һ״̬Ԥ��ʱ��С�ڵ�ǰʱ�䣬��ִ�����²���
		if((ssd->channel_head[i].current_state==CHANNEL_IDLE)||(ssd->channel_head[i].next_state==CHANNEL_IDLE&&ssd->channel_head[i].next_state_predict_time<=ssd->current_time))		
		{
			/*
			//����ͨ��δд����ʱ�򣬿���GC������д����ʱ���Ƚ���У�����ݴ���
			if(find_first_zero(ssd, ssd->strip_bits[0]) < data_page_num){
				if (ssd->gc_request>0) 	//��gc��������Ҫ����һ�����ж�
				{
					#ifdef USE_WHITE_PARITY
					if (ssd->channel_head[0].gc_command!=NULL)
					{
						if(((ssd->subs_w_head == NULL)&&(ssd->channel_head[i].subs_r_head == NULL))||(ssd->gc_request > 1000))
							flag_gc = white_parity_gc(ssd);
					}
					#else
					if (ssd->channel_head[i].gc_command!=NULL)
						flag_gc = gc(ssd,i,0);	//gc��������һ��ֵ����ʾ�Ƿ�ִ����gc���������ִ����gc���������channel�����ʱ�̲��ܷ�������������
					#endif 
					
				}
				#ifndef USE_WHITE_PARITY
					if (flag_gc==1)	//ִ�й�gc��������Ҫ�����˴�ѭ��
					{
						continue;
					}
				#endif 
			}
			*/
			//printf("channel = %d\n", i);
			//���strip_bitδд�����ݣ�����δд���ݿ��Ӧ��channel��chip�Ƿ���У������У����ȷ����Ӧͨ����һ��д�����󣬸���strip_bit��
			//��strip_bit��sizeΪ����������ҳ�����������У�����������Ӧͨ����
			
			//�����ǰ��������ҳδд�����ҵ�ǰͨ��δд��
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
			sub=ssd->channel_head[i].subs_r_head;	/*�ȴ��������*/
			if(flag == 0){
				//printf("services_2_r_wait\n");
				services_2_r_wait(ssd,i,&flag,&chg_cur_time_flag);	/*�����ڵȴ�״̬�Ķ����������������������flag��ֵΪ1��chg_cur_time_flag��ֵΪ0*/
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
*��ssd֧�ָ߼�����ʱ��������������þ��Ǵ���߼������д������
*��������ĸ���������ѡ�����ָ߼�����������ֻ����д���󣬶������Ѿ����䵽ÿ��channel��������ִ��ʱ֮�����ѡȡ��Ӧ�����
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
	
	/*��������䷽ʽ��ֻ�������ض���channel��ѡȡ�ȴ������������*/
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

		if (subs_count==0)                                                            /*û��������Է��񣬷���NULL*/
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
			if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)        /*ȫ���߼��������ʹ��*/
			{
				if (subs_count>1)                                                    /*��1�����Ͽ���ֱ�ӷ����д����*/
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
			else                                                                     /*����ִ��copyback*/
			{
				if (subs_count>1)                                                    /*��1�����Ͽ���ֱ�ӷ����д����*/
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

	if ((ssd->parameter->allocation_scheme==0))                                           /*ȫ��̬���䣬��Ҫ��ssd->subs_w_head��ѡȡ�ȴ������������*/
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
				if ((sub->update==NULL)||((sub->update!=NULL)&&((sub->update->current_state==SR_COMPLETE)||((sub->update->next_state==SR_COMPLETE)&&(sub->update->next_state_predict_time<=ssd->current_time)))))    //û����Ҫ��ǰ������ҳ
				{
					subs[subs_count]=sub;
					subs_count++;
				}						
			}
			
			p=sub;
			sub=sub->next_node;	
		}

		if (subs_count==0)                                                               /*û��������Է��񣬷���NULL*/
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
			*two plane,interleave������ʹ��
			*�����channel�ϣ�ѡ��interleave_two_planeִ��
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
	else                                                                                  /*��̬���䷽ʽ��ֻ�������ض���channel��ѡȡ�ȴ������������*/
	{
		                                                                                  /*�ھ�̬���䷽ʽ�У�����channel�ϵ���������ͬһ��die�ϵ���Щplane��ȷ��ʹ��ʲô����*/
		
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

			if (subs_count==0)                                                            /*û��������Է��񣬷���NULL*/
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
				if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)        /*ȫ���߼��������ʹ��*/
				{
					if (subs_count>1)                                                    /*��1�����Ͽ���ֱ�ӷ����д����*/
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
				else                                                                     /*����ִ��copyback*/
				{
					if (subs_count>1)                                                    /*��1�����Ͽ���ֱ�ӷ����д����*/
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
*ִ��д������ʱ��Ϊ��ͨ��д�������ȡppn
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
*Ϊ�߼������ȡppn
*���ݲ�ͬ����������ͬһ��block��˳��д��Ҫ��ѡȡ���Խ���д������ppn��������ppnȫ����ΪʧЧ��
*��ʹ��two plane����ʱ��Ϊ��Ѱ����ͬˮƽλ�õ�ҳ��������Ҫֱ���ҵ�������ȫ�հ׵Ŀ飬���ʱ��ԭ��
*�Ŀ�û�����ֻ꣬�ܷ����⣬�ȴ��´�ʹ�ã�ͬʱ�޸Ĳ��ҿհ�page�ķ���������ǰ����Ѱ��free���Ϊ��ֻ
*Ҫinvalid block!=64���ɡ�
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
	
	if (ssd->parameter->allocation_scheme==DYNAMIC_ALLOCATION)                         /*��̬�������*/ 
	{
		if((command==INTERLEAVE_TWO_PLANE)||(command==COPY_BACK))                      /*INTERLEAVE_TWO_PLANE�Լ�COPY_BACK�����*/
		{
			for(i=0;i<subs_count;i++)
			{
				die=ssd->channel_head[channel].chip_head[chip].token;
				if(i<ssd->parameter->die_chip)                                         /*Ϊÿ��subs[i]��ȡppn��iС��die_chip*/
				{
					plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
					get_ppn(ssd,channel,chip,die,plane,subs[i]);
					ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane+1)%ssd->parameter->plane_die;
				}
				else                                                                  
				{   
					/*********************************************************************************************************************************
					*����die_chip��i��ָ���subs[i]��subs[i%ssd->parameter->die_chip]��ȡ��ͬλ�õ�ppn
					*����ɹ��Ļ�ȡ������multi_plane_flag=TRUE��ִ��compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE_TWO_PLANE);
					*����ִ��compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE);
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
				compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE_TWO_PLANE);/*����д������Ĵ���ʱ�䣬��д�������״̬ת��*/		
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
			*INTERLEAVE�߼�����Ĵ�����������TWO_PLANE�߼�����Ĵ����
			*��Ϊtwo_plane��Ҫ����ͬһ��die���治ͬplane��ͬһλ�õ�page����interleaveҪ�����ǲ�ͬdie����ġ�
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
					state=find_level_page(ssd,channel,chip,die,subs[0],subs[1]);        /*Ѱ����subs[0]��ppnλ����ͬ��subs[1]��ִ��TWO_PLANE�߼�����*/
					if(state!=SUCCESS)
					{
						get_ppn_for_normal_command(ssd,channel,chip,subs[0]);           /*û�ҵ�����ô�͵���ͨ����������*/
						return FAILURE;
					}
					else
					{
						valid_subs_count=2;
					}
				}
				else if(j>1)
				{
					state=make_level_page(ssd,subs[0],subs[j]);                         /*Ѱ����subs[0]��ppnλ����ͬ��subs[j]��ִ��TWO_PLANE�߼�����*/
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
	else                                                                              /*��̬��������*/
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
	}//elseb ��̬��������
	return SUCCESS;
}


/***********************************************
*��������������sub0��sub1��ppn���ڵ�pageλ����ͬ
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
	*��̬����������
	*sub1��plane�Ǹ���sub0��ssd->channel_head[channel].chip_head[chip].die_head[die].token���ƻ�ȡ��
	*sub1��channel��chip��die��block��page����sub0����ͬ
	************************************************************************************************/
	if(ssd->parameter->allocation_scheme==DYNAMIC_ALLOCATION)                             
	{
		old_plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
		for(i=0;i<ssd->parameter->plane_die;i++)
		{
			plane1=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].add_reg_ppn==-1)
			{
				find_active_block(ssd,channel,chip,die,plane1);                               /*��plane1���ҵ���Ծ��*/
				block1=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].active_block;

				/*********************************************************************************************
				*ֻ���ҵ���block1��block0��ͬ�����ܼ�������Ѱ����ͬ��page
				*��Ѱ��pageʱ�Ƚϼ򵥣�ֱ����last_write_page����һ��д��page��+1�Ϳ����ˡ�
				*����ҵ���page����ͬ����ô���ssd����̰����ʹ�ø߼���������Ϳ�����С��page �����page��£
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
						if (ssd->parameter->greed_MPW_ad==1)                                  /*����̰����ʹ�ø߼�����*/
						{                                                                   
							//make_same_level(ssd,channel,chip,die,plane1,active_block1,page0); /*С��page��ַ�����page��ַ��*/
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
			flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page0);          /*������������þ��Ǹ���page1����Ӧ������ҳ�Լ�location����map��*/
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
	else                                                                                      /*��̬��������*/
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
						//make_same_level(ssd,channel,chip,die,plane1,active_block1,page0);    /*С��page��ַ�����page��ַ��*/
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
*�����Ĺ�����Ϊtwo plane����Ѱ�ҳ�������ͬˮƽλ�õ�ҳ�������޸�ͳ��ֵ���޸�ҳ��״̬
*ע�������������һ������make_level_page����������make_level_page�����������sub1��sub0��pageλ����ͬ
*��find_level_page�������������ڸ�����channel��chip��die��������λ����ͬ��subA��subB��
*******************************************************************************************************/
Status find_level_page(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,struct sub_request *subA,struct sub_request *subB)       
{
	unsigned int i,planeA,planeB,active_blockA,active_blockB,pageA,pageB,aim_page,old_plane;
	struct gc_operation *gc_node;

	old_plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
    
	/************************************************************
	*�ڶ�̬����������
	*planeA����ֵΪdie�����ƣ����planeA��ż����ôplaneB=planeA+1
	*planeA����������ôplaneA+1��Ϊż��������planeB=planeA+1
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
	else                                                                                     /*��̬������������ֱ�Ӹ�ֵ��planeA��planeB*/
	{
		planeA=subA->location->plane;
		planeB=subB->location->plane;
	}
	find_active_block(ssd,channel,chip,die,planeA);                                          /*Ѱ��active_block*/
	find_active_block(ssd,channel,chip,die,planeB);
	active_blockA=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].active_block;
	active_blockB=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].active_block;

	
    
	/*****************************************************
	*���active_block��ͬ����ô������������������ͬ��page
	*����ʹ��̰���ķ����ҵ�������ͬ��page
	******************************************************/
	if (active_blockA==active_blockB)
	{
		pageA=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockA].last_write_page+1;      
		pageB=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockB].last_write_page+1;
		if (pageA==pageB)                                                                    /*�������õ�ҳ������ͬһ��ˮƽλ����*/
		{
			flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockA,pageA);
			flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockB,pageB);
		} 
		else
		{
			if (ssd->parameter->greed_MPW_ad==1)                                             /*̰����ʹ�ø߼�����*/
			{
				if (pageA<pageB)                                                            
				{
					aim_page=pageB;
					make_same_level(ssd,channel,chip,die,planeA,active_blockA,aim_page);     /*С��page��ַ�����page��ַ��*/
				}
				else
				{
					aim_page=pageA;
					make_same_level(ssd,channel,chip,die,planeB,active_blockB,aim_page);    
				}
				flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockA,aim_page);
				flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockB,aim_page);
			} 
			else                                                                             /*����̰����ʹ�ø߼�����*/
			{
				subA=NULL;
				subB=NULL;
				ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane;
				return FAILURE;
			}
		}
	}
	/*********************************
	*����ҵ�������active_block����ͬ
	**********************************/
	else
	{   
		pageA=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockA].last_write_page+1;      
		pageB=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockB].last_write_page+1;
		if (pageA<pageB)
		{
			if (ssd->parameter->greed_MPW_ad==1)                                             /*̰����ʹ�ø߼�����*/
			{
				/*******************************************************************************
				*��planeA�У���active_blockB��ͬλ�õĵ�block�У���pageB��ͬλ�õ�page�ǿ��õġ�
				*Ҳ����palneA�е���Ӧˮƽλ���ǿ��õģ�������Ϊ��planeB�ж�Ӧ��ҳ��
				*��ô��Ҳ��planeA��active_blockB�е�page��pageB��£
				********************************************************************************/
				if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockB].page_head[pageB].free_state==PG_SUB)    
				{
					make_same_level(ssd,channel,chip,die,planeA,active_blockB,pageB);
					flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockB,pageB);
					flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockB,pageB);
				}
                /********************************************************************************
				*��planeA�У���active_blockB��ͬλ�õĵ�block�У���pageB��ͬλ�õ�page�ǿ��õġ�
				*��ô��Ҫ����Ѱ��block����Ҫ������ˮƽλ����ͬ��һ��ҳ
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
					*�������������
					*1��planeA��planeB�е�active_blockA��pageAλ�ö����ã���ô��ͬplane ����ͬλ�ã���blockAΪ׼
					*2��planeA��planeB�е�active_blockB��pageAλ�ö����ã���ô��ͬplane ����ͬλ�ã���blockBΪ׼
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
*�����Ĺ������޸��ҵ���pageҳ��״̬�Լ���Ӧ��dram��ӳ����ֵ
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
	else                                                                                      /*����߼�ҳ�����˸��£���Ҫ��ԭ����ҳ��ΪʧЧ*/
	{
		ppn=ssd->dram->map->map_entry[sub->lpn].pn;
		location=find_location(ssd,ppn);
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=0;        //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=0;         //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=0;
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;
		if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num==ssd->parameter->page_block)    //��block��ȫ��invalid��ҳ������ֱ��ɾ��
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
*�����Ĺ��ܾ���������λ�ò�ͬ��pageλ����ͬ
*********************************************/
struct ssd_info *make_same_level(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,unsigned int block,unsigned int aim_page)
{
	int i=0,step,page;
	struct direct_erase *new_direct_erase,*direct_erase_node;

	page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page+1;                  /*��Ҫ�����ĵ�ǰ��Ŀ�дҳ��*/
	step=aim_page-page;
	while (i<step)
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page+i].valid_state=0;     /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page+i].free_state=0;      /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page+i].lpn=0;

		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num++;

		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num--;

		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;

		i++;
	}

	ssd->waste_page_count+=step;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page=aim_page-1;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num==ssd->parameter->page_block)    /*��block��ȫ��invalid��ҳ������ֱ��ɾ��*/
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
*�ڴ���߼������д������ʱ����������Ĺ��ܾ��Ǽ��㴦��ʱ���Լ������״̬ת��
*���ܻ����Ǻ����ƣ���Ҫ���ƣ��޸�ʱע��Ҫ��Ϊ��̬����Ͷ�̬�����������
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
		for(i=0;i<max_subs_num;i++)
		{
			if(subs[i]!=NULL)
			{
				last_sub=subs[i];
				subs[i]->current_state=SR_W_TRANSFER;
				subs[i]->current_time=ssd->current_time;
				subs[i]->next_state=SR_COMPLETE;
				subs[i]->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(subs[i]->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				if(subs[i]->lpn == -2)
				{
#ifdef CALCULATION
					subs[i]->complete_time=subs[i]->next_state_predict_time + 82000;
#else
					subs[i]->complete_time=subs[i]->next_state_predict_time;
#endif
				}
				else
					subs[i]->complete_time=subs[i]->next_state_predict_time;
				delete_w_sub_request(ssd,channel,subs[i]);
				//delete_from_channel(ssd,channel,subs[i]);
			}
		}
		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=last_sub->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;	
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
					subs[i]->current_time=last_sub->complete_time+ssd->parameter->time_characteristics.tDBSY;
				}
				
				subs[i]->next_state=SR_COMPLETE;


				subs[i]->next_state_predict_time=subs[i]->current_time+7*ssd->parameter->time_characteristics.tWC+(subs[i]->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				if(subs[i]->lpn == -2)
				{
#ifdef CALCULATION
					subs[i]->complete_time=subs[i]->next_state_predict_time + 82000;
#else
					subs[i]->complete_time=subs[i]->next_state_predict_time;
#endif
				}
				else
					subs[i]->complete_time=subs[i]->next_state_predict_time;

				last_sub=subs[i];
				delete_w_sub_request(ssd,channel,subs[i]);
				//delete_from_channel(ssd,channel,subs[i]);
			}
		}
		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=last_sub->complete_time;

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
					subs[i]->current_time=last_sub->complete_time;
				}
				subs[i]->next_state=SR_COMPLETE;
				subs[i]->next_state_predict_time=subs[i]->current_time+7*ssd->parameter->time_characteristics.tWC+(subs[i]->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				if(subs[i]->lpn == -2)
				{
#ifdef CALCULATION
					subs[i]->complete_time=subs[i]->next_state_predict_time + 82000;
#else
					subs[i]->complete_time=subs[i]->next_state_predict_time;
#endif
				}
				else
					subs[i]->complete_time=subs[i]->next_state_predict_time;
				last_sub=subs[i];
				delete_w_sub_request(ssd,channel,subs[i]);
				//delete_from_channel(ssd,channel,subs[i]);
			}
		}
		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=last_sub->complete_time;

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
		if(subs[0]->lpn == -2)
		{
#ifdef CALCULATION
			subs[0]->complete_time=subs[0]->next_state_predict_time + 82000;
#else
			subs[0]->complete_time=subs[0]->next_state_predict_time;
#endif
		}
		else
			subs[0]->complete_time=subs[0]->next_state_predict_time;
		//delete_from_channel(ssd,channel,subs[0]);
		delete_w_sub_request(ssd,channel,subs[0]);
		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=subs[0]->complete_time;

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
*�����Ĺ��ܾ��ǰ��������ssd->subs_w_head����ssd->channel_head[channel].subs_w_head��ɾ��
******************************************************************************************/
struct ssd_info *delete_from_channel(struct ssd_info *ssd,unsigned int channel,struct sub_request * sub_req)
{
	struct sub_request *sub,*p;
    
	/******************************************************************
	*��ȫ��̬�������������ssd->subs_w_head��
	*������ȫ��̬�������������ssd->channel_head[channel].subs_w_head��
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
				
				if (sub==ssd->subs_w_head)                                                     /*������������sub request������ɾ��*/
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
				if (sub==ssd->channel_head[channel].subs_w_head)                               /*������������channel������ɾ��*/
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
	get_ppn(ssd,channel,chip,die,sub1->location->plane,sub1);                                  /*�ҳ�����ppnһ���Ƿ���������������ͬ��plane��,����ʹ��copyback����*/
	ppn1=sub1->ppn;

	old_ppn2=ssd->dram->map->map_entry[sub2->lpn].pn;
	get_ppn(ssd,channel,chip,die,sub2->location->plane,sub2);                                  /*�ҳ�����ppnһ���Ƿ���������������ͬ��plane��,����ʹ��copyback����*/
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
	get_ppn(ssd,channel,chip,die,0,sub1);                                                     /*�ҳ�����ppnһ���Ƿ���������������ͬ��plane��,����ʹ��copyback����*/
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
*�����Ĺ������ڴ����������ĸ߼�����ʱ����Ҫ����one_page��ƥ�������һ��page��two_page
*û���ҵ����Ժ�one_pageִ��two plane����interleave������ҳ,��Ҫ��one_page�����һ���ڵ�
*��command == TWO_PLANEʱ��
*Ѱ����one_page����ͬchip��die��block��page����ͬplane���ҵ�ǰ״̬ҲΪSR_WAIT��two_page
*��command == INTERLEAVEʱ��
*Ѱ����one_page����ͬchip����ͬdie���ҵ�ǰ״̬ҲΪSR_WAIT��two_page
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
				{//ͬһоƬ��ͬһdie�о�����ͬ��block��page�����������ڲ�ͬ��planeִ��two_plane����
					if (one_page->location->plane!=two_page->location->plane)
					{
						return two_page;                                                       /*�ҵ�����one_page����ִ��two plane������ҳ*/
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
			if (two_page==NULL)                                                               /*û���ҵ����Ժ�one_pageִ��two_plane������ҳ,��Ҫ��one_page�����һ���ڵ�*/
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
				else if ((one_page->location->chip==two_page->location->chip)&&(one_page->location->die!=two_page->location->die))  //interleave���ͬһchip�в�ͬdie������������ͬʱִ��
				{
					return two_page;                                                           /*�ҵ�����one_page����ִ��interleave������ҳ*/
				}
				else
				{
					two_page=two_page->next_node;
				}
		     }
		    if (two_page==NULL)                                                                /*û���ҵ����Ժ�one_pageִ��interleave������ҳ,��Ҫ��one_page�����һ���ڵ�*/
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
*�ڴ����������߼�����ʱ������������ǲ��ҿ���ִ�и߼������sub_request
**************************************************************************/
int find_interleave_twoplane_sub_request(struct ssd_info * ssd, unsigned int channel,struct sub_request **sub_request_one,struct sub_request **sub_request_two,unsigned int command)
{
	*sub_request_one=ssd->channel_head[channel].subs_r_head;
	while (*sub_request_one!=NULL)
	{
		*sub_request_two=find_interleave_twoplane_page(ssd,*sub_request_one,command);		/*�ҳ�����������two_plane����interleave��read�����󣬰���λ��������ʱ������*/
		if (*sub_request_two==NULL)
		{
			*sub_request_one=(*sub_request_one)->next_node;
		}
		else if (*sub_request_two!=NULL)                                                            /*�ҵ�����������ִ��two plane������ҳ*/
		{
			//printf("two_plane_read\n");
			break;
		}
	}

	if (*sub_request_two!=NULL)
	{
		if (ssd->request_queue!=ssd->request_tail)      
		{                                                                                         /*ȷ��interleave read���������ǵ�һ�������������*/
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
*��������ǳ���Ҫ�����������״̬ת�䣬�Լ�ʱ��ļ��㶼ͨ���������������
*����д�������ִ����ͨ����ʱ��״̬���Լ�ʱ��ļ���Ҳ��ͨ����������������
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
	*������ͨ����ʱ�����������Ŀ��״̬��Ϊ���¼������SR_R_READ��SR_R_C_A_TRANSFER��SR_R_DATA_TRANSFER
	*д�������Ŀ��״ֻ̬��SR_W_TRANSFER
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
			    *���Ŀ��״̬��ָflash���ڶ����ݵ�״̬��sub����һ״̬��Ӧ���Ǵ�������SR_R_DATA_TRANSFER
			    *��ʱ��channel�޹أ�ֻ��chip�й�����Ҫ�޸�chip��״̬ΪCHIP_READ_BUSY����һ��״̬����CHIP_DATA_TRANSFER
			    ******************************************************************************************************/
				sub->current_time=ssd->current_time;
				sub->current_state=SR_R_READ;
				sub->next_state=SR_R_DATA_TRANSFER;
				sub->next_state_predict_time=ssd->current_time+ssd->parameter->time_characteristics.tR;

				ssd->channel_head[location->channel].current_state = CHANNEL_IDLE;
				ssd->channel_head[location->channel].current_time = ssd->current_time;
				ssd->channel_head[location->channel].next_state = CHANNEL_DATA_TRANSFER;
				ssd->channel_head[location->channel].next_state_predict_time = ssd->current_time + ssd->parameter->time_characteristics.tR;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_READ_BUSY;
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_DATA_TRANSFER;
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=ssd->current_time+ssd->parameter->time_characteristics.tR;

				break;
			}
			case SR_R_C_A_TRANSFER:
			{   
				/*******************************************************************************************************
				*Ŀ��״̬�������ַ����ʱ��sub����һ��״̬����SR_R_READ
				*���״̬��channel��chip�йأ�����Ҫ�޸�channel��chip��״̬�ֱ�ΪCHANNEL_C_A_TRANSFER��CHIP_C_A_TRANSFER
				*��һ״̬�ֱ�ΪCHANNEL_IDLE��CHIP_READ_BUSY
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
				*Ŀ��״̬�����ݴ���ʱ��sub����һ��״̬�������״̬SR_COMPLETE
				*���״̬�Ĵ���Ҳ��channel��chip�йأ�����channel��chip�ĵ�ǰ״̬��ΪCHANNEL_DATA_TRANSFER��CHIP_DATA_TRANSFER
				*��һ��״̬�ֱ�ΪCHANNEL_IDLE��CHIP_IDLE��
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
				*���Ǵ���д������ʱ��״̬��ת���Լ�ʱ��ļ���
				*��Ȼд������Ĵ���״̬Ҳ�����������ô�࣬����д�����Ǵ�����plane�д�������
				*�����Ϳ��԰Ѽ���״̬��һ��״̬�������͵���SR_W_TRANSFER���״̬������sub����һ��״̬�������״̬��
				*��ʱchannel��chip�ĵ�ǰ״̬��ΪCHANNEL_TRANSFER��CHIP_WRITE_BUSY
				*��һ��״̬��ΪCHANNEL_IDLE��CHIP_IDLE
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
		*�߼�����TWO_PLANE�Ĵ��������TWO_PLANE�߼������Ƕ�������ĸ߼�����
		*״̬ת������ͨ����һ������ͬ������SR_R_C_A_TRANSFERʱ����ʱ���Ǵ��еģ���Ϊ����һ��ͨ��channel
		*����SR_R_DATA_TRANSFERҲ�ǹ���һ��ͨ��
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
				ssd->channel_head[location->channel].next_state_predict_time=sub_twoplane_one->next_state_predict_time;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_DATA_TRANSFER;				
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_IDLE;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=sub_twoplane_one->next_state_predict_time;

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
				sub_interleave_one->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;									
				sub_interleave_one->begin_time=ssd->current_time;

				ssd->channel_head[sub_interleave_one->location->channel].chip_head[sub_interleave_one->location->chip].die_head[sub_interleave_one->location->die].plane_head[sub_interleave_one->location->plane].add_reg_ppn=sub_interleave_one->ppn;
				ssd->read_count++;

				sub_interleave_two->current_time=ssd->current_time;									
				sub_interleave_two->current_state=SR_R_C_A_TRANSFER;									
				sub_interleave_two->next_state=SR_R_READ;									
				sub_interleave_two->next_state_predict_time=sub_interleave_one->next_state_predict_time;									
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
				sub_interleave_one->current_time=ssd->current_time;					
				sub_interleave_one->current_state=SR_R_DATA_TRANSFER;		
				sub_interleave_one->next_state=SR_COMPLETE;				
				sub_interleave_one->next_state_predict_time=ssd->current_time+(sub_interleave_one->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;			
				sub_interleave_one->complete_time=sub_interleave_one->next_state_predict_time;
				
				sub_interleave_two->current_time=sub_interleave_one->next_state_predict_time;					
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