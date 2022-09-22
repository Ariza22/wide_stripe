/*****************************************************************************************************************************
This project was supported by the National Basic Research 973 Program of China under Grant No.2011CB302301
Huazhong University of Science and Technology (HUST)   Wuhan National Laboratory for Optoelectronics

FileName�� ssd.c
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
#include <stdio.h>
#include <time.h>
#include <string.h>

#include "ssd.h"
#include "initialize.h"
#include "pagemap.h"
#include "flash.h"
#include "avlTree.h"
#include "states.h"
#include "recover.h"

//����Ϊ������λͼʱ�����ص�һ��δд��ͨ����(��0��ʼ��
unsigned int find_first_zero(struct ssd_info* ssd, unsigned int patten)
{
	int index;
	unsigned int i;
	for(i = 0; i < 32; i++)
	{        
		if((patten & 0x01) == 0)
		{
			break;
		}
		patten >>= 1;
	}
	return i;
}

unsigned int find_first_one(struct ssd_info* ssd, unsigned int patten)
{
	int index;
	unsigned int i;
	for (i = 0; i < 32; i++)
	{
		if ((patten | 0x01) == patten)
		{
			break;
		}
		patten >>= 1;
	}
	return i;
}

unsigned int sub_r_num_for_channel(struct ssd_info *ssd, unsigned int channel)
{
	struct sub_request *p = NULL;
	unsigned int num = 0;
	p = ssd->channel_head[channel].subs_r_head;
	while (p != NULL)
	{
		num++;
		p = p->next_node;
	}
	return num;
}
unsigned int sub_w_num_for_channel(struct ssd_info *ssd, unsigned int channel)
{
	struct sub_request *p = NULL;
	unsigned int num = 0;
	p = ssd->channel_head[channel].subs_w_head;
	while (p != NULL)
	{
		num++;
		p = p->next_node;
	}
	return num;
}
/******************simulate() *********************************************************************
*simulate()�Ǻ��Ĵ���������Ҫʵ�ֵĹ��ܰ���
*1, ��trace�ļ��л�ȡһ�����󣬹ҵ�ssd->request
*2������ssd�Ƿ���dram�ֱ�������������󣬰���Щ�������Ϊ��д�����󣬹ҵ�ssd->channel����ssd��
*3�������¼����Ⱥ���������Щ��д������
*4�����ÿ������������󶼴������������Ϣ��outputfile�ļ���
**************************************************************************************************/
struct ssd_info *simulate(struct ssd_info *ssd)
{
	int flag=1,flag1=0;
	double output_step=0;
	unsigned int a=0,b=0;
	errno_t err;
	static int simulate_test_count=0;
	unsigned int channel, chip, die, plane, active_block, page, ppn;
	unsigned int i;
	
	if((err=fopen_s(&(ssd->tracefile),ssd->tracefilename,"r"))!=0)
	{  
		printf("the trace file can't open\n");
		return NULL;
	}
	fprintf(ssd->outputfile,"      arrive           lsn     size ope     begin time    response time    process time  request sub num\n");	
	fflush(ssd->outputfile);





#ifdef BROKEN_PAGE
	for(i = 0; i < 64; i++)
	{
		ssd->channel_head[0].chip_head[0].die_head[0].plane_head[0].blk_head[0].page_head[i].bad_page_flag = TRUE;
		//ssd->channel_head[2].chip_head[0].die_head[0].plane_head[0].blk_head[0].page_head[i].bad_page_flag = TRUE;
	}
#endif
#ifdef BROKEN_BLOCK
	for (i = 0; i < ssd->parameter->block_plane; i++)
	{
		ssd->channel_head[1].chip_head[1].die_head[0].plane_head[0].blk_head[i].bad_block_flag = TRUE;
	}
#endif

	/*���˹۵�:ѭ������100�ĳɺ궨������ֱ�ۣ������Ķ�*/
	while(flag!=100)		/*��һ����Ҫ��100�����Ǳ�����get_requests�ķ���ֵ�ͺ��渳ֵƥ��*/
	{
		
		flag=get_requests(ssd);
		if(flag == 1)
		{   
			simulate_test_count++;
			if (simulate_test_count % 1000 == 0)
				printf("the simulate_test_count is %d\n",simulate_test_count);
			if (ssd->parameter->dram_capacity!=0)
			{
				buffer_management(ssd);  
				distribute(ssd); 
			} 
			else
			{
				no_buffer_distribute(ssd);
			}		
		}

		/*for(i = 0; i <ssd->parameter->channel_number; i++)
		{
		printf("channel = %d\tread_num = %d\twrite_num = %d\n", i, sub_r_num_for_channel(ssd, i), sub_w_num_for_channel(ssd, i));
		}
		printf("\n");*/

		//��д����������ʹ�ø߼����
		ssd = process(ssd); 
		//trace��Ϣ������������ʱ�䡢������������Լ��������ܣ�
		trace_output(ssd);
		//traceβ
		if(flag == 100)
			printf("the total request number is %d\n",simulate_test_count);
		if (flag == 0 && ssd->request_queue == NULL)
		{
			flag = 100;	/*Ҫ��ѭ������ƥ��*/
		}
	}
	#ifdef ACTIVE_RECOVERY
	/***********************************************************************/
	struct request *req = NULL;
	unsigned int broken_type, broken_flag;
	broken_type = DIE_FAULT;
	broken_flag = 0x00f0;
	creat_active_recovery_request(ssd);
	req = ssd->request_tail;
	active_recovery(ssd, broken_type, broken_flag, req);

	while(ssd->request_queue != NULL)
	{
		update_system_time(ssd);
		
		/*if(get_recovery_node_num(ssd) %1000 == 0)
			printf("the recovery node number is %d\n", get_recovery_node_num(ssd));*/
		//��д����������ʹ�ø߼����
		ssd = process(ssd); 
		//trace��Ϣ������������ʱ�䡢������������Լ��������ܣ�
		trace_output(ssd);
	}
	/**********************************************************************/
#endif

	fclose(ssd->tracefile);
	return ssd;
}
#ifdef ACTIVE_RECOVERY

void creat_active_recovery_request(struct ssd_info *ssd)
{
	struct request *request1 = NULL;
	request1 = (struct request*)malloc(sizeof(struct request));
	alloc_assert(request1,"request");
	memset(request1,0, sizeof(struct request));

	request1->time = ssd->current_time;
	request1->lsn = 0;
	request1->size = size;
	request1->operation = RESUME;	
	request1->begin_time = ssd->current_time;
	request1->response_time = 0;	
	request1->energy_consumption = 0;	
	request1->next_node = NULL;
	request1->distri_flag = 0;              // indicate whether this request has been distributed already
	request1->subs = NULL;
	request1->need_distr_flag = NULL;
	request1->complete_lsn_count=0;         //record the count of lsn served by buffer


	if(ssd->request_queue == NULL)          //The queue is empty
	{
		ssd->request_queue = request1;
		ssd->request_tail = request1;
		ssd->request_queue_length++;
	}
	else
	{			
		(ssd->request_tail)->next_node = request1;	
		ssd->request_tail = request1;			
		ssd->request_queue_length++;
	}
}


int update_system_time(struct ssd_info *ssd)  
{  
	char buffer[200];
	unsigned int lsn=0;
	int device,  size, ope,large_lsn, i = 0,j=0;
	struct request *request1;
	int flag = 1;
	long filepoint; 
	static int repeat = 1;
	static __int64 time_per_repeat = 0, largest_time_t = 0;
	__int64 time_t = 0;
	__int64 nearest_event_time;    

 
	time_t = ssd->current_time;
	nearest_event_time = find_nearest_event(ssd);
	if (nearest_event_time == 0x7fffffffffffffff)
	{
		ssd->current_time = time_t;           
	}
	else
	{   
		if (nearest_event_time < time_t)
		{
			ssd->current_time = time_t;
		}
		else
		{
			ssd->current_time = nearest_event_time;
		}
		/*if(nearest_event_time < time_t)
		{
			if (ssd->current_time <= nearest_event_time)
			{
				ssd->current_time = nearest_event_time;
			}
			return -1;
		}
		else
		{
			if (ssd->request_queue_length >= (unsigned int)ssd->parameter->queue_length)
			{
				ssd->current_time = nearest_event_time;
				return -1;
			} 
			else
			{
				ssd->current_time = time_t;
			}
		}*/
	}

	if(time_t < 0)
	{
		printf("error!\n");
		//while(1){}
		return 100;
	}
	return 1;
}
#endif


/********    get_request    ******************************************************
*	1.get requests that arrived already
*	2.add those request node to ssd->reuqest_queue
*	return	0: reach the end of the trace
*			-1: no request has been added
*			1: add one request to list
*SSDģ����������������ʽ:ʱ������(��ȷ��̫��) �¼�����(���������) trace����()��
*���ַ�ʽ�ƽ��¼���channel/chip״̬�ı䡢trace�ļ�����ﵽ��
*channel/chip״̬�ı��trace�ļ����󵽴���ɢ����ʱ�����ϵĵ㣬ÿ�δӵ�ǰ״̬����
*��һ��״̬��Ҫ���������һ��״̬��ÿ����һ����ִ��һ��process
********************************************************************************/
int get_requests(struct ssd_info *ssd)  
{  
	char buffer[200];
	unsigned int lsn=0;
	int device,  size, ope,large_lsn, i = 0,j=0;
	struct request *request1;
	int flag = 1;
	long filepoint; 
	static int repeat = 1;
	static __int64 time_per_repeat = 0, largest_time_t = 0;
	__int64 time_t = 0;
	__int64 nearest_event_time;    

	#ifdef DEBUG
	printf("enter get_requests,  current time:%I64u\n",ssd->current_time);
	#endif

	if (feof(ssd->tracefile))
	{
		return 0; 
	}

	filepoint = ftell(ssd->tracefile);	
	fgets(buffer, 200, ssd->tracefile); 
	sscanf(buffer,"%I64u %d %d %d %d",&time_t,&device,&lsn,&size,&ope);

#ifdef REPEAT_TIME
	time_t = time_t + time_per_repeat;
#endif 

    
	if ((device<0)&&(lsn<0)&&(size<0)&&(ope<0))
	{
		return 100;
	}
	if (lsn<ssd->min_lsn) 
	{
		ssd->min_lsn=lsn;
	}
	if (lsn>ssd->max_lsn)
	{
		ssd->max_lsn=lsn;
	}
	/**************************************************************************************************************************
	*�ϲ��ļ�ϵͳ���͸�SSD���κζ�д��������������֣�LSN��size�� LSN���߼������ţ������ļ�ϵͳ���ԣ����������Ĵ�
	*���ռ���һ�����Ե������ռ䡣���磬������260��6����ʾ������Ҫ��ȡ��������Ϊ260���߼�������ʼ���ܹ�6��������
	*large_lsn: channel�����ж��ٸ�subpage�������ٸ�sector��
	*overprovideϵ����SSD�в��������еĿռ䶼���Ը��û�ʹ�ã�����32G��SSD������10%�Ŀռ䱣�������������ã����Գ���1-provide
	***************************************************************************************************************************/
	large_lsn=(int)((ssd->parameter->subpage_page*ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num)*(1-ssd->parameter->overprovide));
#ifdef USE_EC
	large_lsn = large_lsn/(ssd->parameter->subpage_page * BAND_WITDH) *(ssd->parameter->subpage_page*(BAND_WITDH - MAX_EC_MODLE));//ԭʼ�����������
#else
#ifdef USE_PARITY
	large_lsn = large_lsn/(ssd->parameter->subpage_page*(PARITY_SIZE+1)) *(ssd->parameter->subpage_page*(PARITY_SIZE));//ԭʼ�����������
#endif
#endif
	lsn = lsn%large_lsn;

	nearest_event_time=find_nearest_event(ssd);
	if (nearest_event_time==0x7fffffffffffffff)
	{
		ssd->current_time=time_t;           
		                                                  
		//if (ssd->request_queue_length>ssd->parameter->queue_length)    //���������еĳ��ȳ����������ļ��������õĳ���                     
		//{
			//printf("error in get request , the queue length is too long\n");
		//}
	}
	else
	{   
		if(nearest_event_time<time_t)
		{//����ͨ����оƬ���ҵ�������¼���ʱ������󵽴��ʱ���磬��Ҫ���Ѷ�ȡ������ع��������ļ���
			/*******************************************************************************
			*�ع��������û�а�time_t����ssd->current_time����trace�ļ��Ѷ���һ����¼�ع�
			*filepoint��¼��ִ��fgets֮ǰ���ļ�ָ��λ�ã��ع����ļ�ͷ+filepoint��
			*int fseek(FILE *stream, long offset, int fromwhere);���������ļ�ָ��stream��λ�á�
			*���ִ�гɹ���stream��ָ����fromwhere��ƫ����ʼλ�ã��ļ�ͷ0����ǰλ��1���ļ�β2��Ϊ��׼��
			*ƫ��offset��ָ��ƫ���������ֽڵ�λ�á����ִ��ʧ��(����offset�����ļ������С)���򲻸ı�streamָ���λ�á�
			*�ı��ļ�ֻ�ܲ����ļ�ͷ0�Ķ�λ��ʽ���������д��ļ���ʽ��"r":��ֻ����ʽ���ı��ļ�	
			**********************************************************************************/


			fseek(ssd->tracefile,filepoint,0); 
			if (ssd->current_time<=nearest_event_time)
			{
				//printf("The most recent event is earlier than the time the request arrived.\t");
				ssd->current_time=nearest_event_time;
			}
			return -1;
		}
		else
		{
			if (ssd->request_queue_length >= (unsigned int)ssd->parameter->queue_length)
			{
				//printf("Exceed request queue length.\t");
				fseek(ssd->tracefile,filepoint,0);
				ssd->current_time=nearest_event_time;
				return -1;
			} 
			else
			{
				ssd->current_time=time_t;
			}
		}
	}

	if(time_t < 0)
	{
		printf("error!\n");
		//while(1){}
		return 100;
	}

	if(feof(ssd->tracefile))
	{
		#ifdef REPEAT_TIME

		if(repeat != REPEAT_TIME)
		{
			repeat++;
			time_per_repeat = time_t;
			rewind(ssd->tracefile);//���ļ�ָ��ָ���ļ�ͷ��
		}
		else
	#endif 
		{
			request1=NULL;
			return 100;
		}
	}

	request1 = (struct request*)malloc(sizeof(struct request));
	alloc_assert(request1,"request");
	memset(request1,0, sizeof(struct request));

	request1->time = time_t;
	request1->lsn = lsn;
	request1->size = size;
	request1->operation = ope;	
	request1->begin_time = time_t;
	request1->response_time = 0;	
	request1->energy_consumption = 0;	
	request1->next_node = NULL;
	request1->distri_flag = 0;              // indicate whether this request has been distributed already
	request1->subs = NULL;
	request1->need_distr_flag = NULL;
	request1->complete_lsn_count=0;         //record the count of lsn served by buffer
	filepoint = ftell(ssd->tracefile);		// set the file point

	if(ssd->request_queue == NULL)          //The queue is empty
	{
		ssd->request_queue = request1;
		ssd->request_tail = request1;
		ssd->request_queue_length++;
	}
	else
	{			
		(ssd->request_tail)->next_node = request1;	
		ssd->request_tail = request1;			
		ssd->request_queue_length++;
	}

	if (request1->operation==1)             //����ƽ�������С 1Ϊ�� 0Ϊд
	{
		//printf("READ........................................................................\n");
		ssd->ave_read_size=(ssd->ave_read_size*ssd->read_request_count+request1->size)/(ssd->read_request_count+1);
	} 
	else
	{
		//printf("WRITE........................................................................\n");
		ssd->ave_write_size=(ssd->ave_write_size*ssd->write_request_count+request1->size)/(ssd->write_request_count+1);
	}

	
	filepoint = ftell(ssd->tracefile);	
	fgets(buffer, 200, ssd->tracefile);    //Ѱ����һ������ĵ���ʱ��
	sscanf(buffer,"%I64u %d %d %d %d",&time_t,&device,&lsn,&size,&ope);
	ssd->next_request_time=time_t;
	fseek(ssd->tracefile,filepoint,0);

	return 1;
}

/**********************************************************************************************************************************************
*����buffer�Ǹ�дbuffer������Ϊд�������ģ���Ϊ��flash��ʱ��tRΪ20us��дflash��ʱ��tprogΪ200us������Ϊд������ܽ�ʡʱ��
*�����������������buffer����buffer������ռ��channel��I/O���ߣ�û������buffer����flash����ռ��channel��I/O���ߣ����ǲ���buffer��
*д����������request�ֳ�sub_request����������Ƕ�̬���䣬sub_request�ҵ�ssd->sub_request�ϣ���Ϊ��֪��Ҫ�ȹҵ��ĸ�channel��sub_request��
*        ����Ǿ�̬������sub_request�ҵ�channel��sub_request����,ͬʱ���ܶ�̬���仹�Ǿ�̬����sub_request��Ҫ�ҵ�request��sub_request����
*        ��Ϊÿ������һ��request����Ҫ��traceoutput�ļ�������������request����Ϣ��������һ��sub_request,�ͽ����channel��sub_request��
*        ��ssd��sub_request����ժ����������traceoutput�ļ����һ���������request��sub_request����
*        sub_request����buffer����buffer����д�����ˣ����ҽ���sub_page�ᵽbuffer��ͷ(LRU)����û��������buffer�������Ƚ�buffer��β��sub_request
*        д��flash(������һ��sub_requestд���󣬹ҵ��������request��sub_request���ϣ�ͬʱ�Ӷ�̬���仹�Ǿ�̬����ҵ�channel��ssd��
*        sub_request����),�ڽ�Ҫд��sub_pageд��buffer��ͷ
***********************************************************************************************************************************************/
int services_2_read_in_buffer = 0;
int services_2_write_in_buffer = 0;
struct ssd_info *buffer_management(struct ssd_info *ssd)
{   
	unsigned int j,lsn,lpn,last_lpn,first_lpn,index,complete_flag=0, state,full_page;
	unsigned int flag=0,need_distb_flag,lsn_flag,flag1=1,active_region_flag=0;           
	struct request *new_request;
	struct buffer_group *buffer_node,key;
	unsigned int mask=0,offset1=0,offset2=0;

	#ifdef DEBUG
	printf("enter buffer_management,  current time:%I64u\n",ssd->current_time);
	#endif
	ssd->dram->current_time=ssd->current_time;
	full_page=~(0xffffffff<<ssd->parameter->subpage_page);
	
	new_request=ssd->request_tail;
	lsn=new_request->lsn;
	if (lsn == 1090464)
		printf("yes");
	lpn=new_request->lsn/ssd->parameter->subpage_page;
	last_lpn=(new_request->lsn+new_request->size-1)/ssd->parameter->subpage_page;
	first_lpn=new_request->lsn/ssd->parameter->subpage_page;

	new_request->need_distr_flag=(unsigned int*)malloc(sizeof(unsigned int)*((last_lpn-first_lpn+1)*ssd->parameter->subpage_page/32+1));
	alloc_assert(new_request->need_distr_flag,"new_request->need_distr_flag");
	memset(new_request->need_distr_flag, 0, sizeof(unsigned int)*((last_lpn-first_lpn+1)*ssd->parameter->subpage_page/32+1));
	
	if(new_request->operation==READ) 
	{		
		while(lpn<=last_lpn)      		
		{
			/************************************************************************************************
			*need_distb_flag��ʾ�Ƿ���Ҫִ��distribution������1��ʾ��Ҫִ�У�buffer��û�У�0��ʾ����Ҫִ�У�
			*��1��ʾ��Ҫ�ַ���0��ʾ����Ҫ�ַ�����Ӧ���ʼȫ����Ϊ1��
			*����ƽ���������ʽ����Ļ������У����Ҷ������Ӧ�Ľڵ��Ƿ����У�
			*������������У���buffer�ڵ��ᵽ����buffer���еĶ��ף����޸���Ӧ��ͳ�Ʋ�����
			*��������У���ֻ�޸���Ӧ��ͳ�Ʋ���
			*************************************************************************************************/
			need_distb_flag=full_page;   
			key.group=lpn;
			buffer_node= (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);		// buffer node 

			while((buffer_node!=NULL)&&(lsn<(lpn+1)*ssd->parameter->subpage_page)&&(lsn<=(new_request->lsn+new_request->size-1)))             			
			{             	
				lsn_flag=full_page;
				mask=1 << (lsn%ssd->parameter->subpage_page); //�����lsn�ı�־
				if(mask>31)
				{
					printf("the subpage number is larger than 32!add some cases");
					getchar(); 		   
				}
				else if((buffer_node->stored & mask)==mask) //���buffer�д洢��lsn�������
				{
					flag=1;
					lsn_flag=lsn_flag&(~mask); //0��ʾ��buffer���ҵ���1��ʾ��Ҫ���������
				}
				// buffer����lsn
				if(flag==1)				
				{	
					// �����buffer�ڵ㲻��buffer�Ķ��ף���Ҫ������ڵ��ᵽ���ף�ʵ����LRU�㷨�������һ��˫����С�
					if(ssd->dram->buffer->buffer_head!=buffer_node)     
					{
						// ���buffer�ڵ���buffer�Ķ�β
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
					ssd->dram->buffer->read_hit++;					
					new_request->complete_lsn_count++;											
				}		
				else if(flag==0)
				{
					ssd->dram->buffer->read_miss_hit++;
				}

				need_distb_flag=need_distb_flag&lsn_flag;  //�޸ķ���ı�־λ
				
				flag=0;		
				lsn++;						
			}	

			//�޸ĵ�ǰlpn��Ӧ����need_distr_flag��ָ����ڴ������е�����
			index=(lpn-first_lpn)/(32/ssd->parameter->subpage_page);  //ÿ32λ��ʾ8��lpn����index��ʾ��Ҫ32λ�ֶε�����������12��lpn��Ҫ2��index=0��1����32λ���			
			new_request->need_distr_flag[index]=new_request->need_distr_flag[index]|(need_distb_flag<<(((lpn-first_lpn)%(32/ssd->parameter->subpage_page))*ssd->parameter->subpage_page));	
			lpn++;
			
		}
	}  
	else if(new_request->operation==WRITE)
	{
		while(lpn<=last_lpn)           	
		{	
			need_distb_flag=full_page;
			mask=~(0xffffffff<<(ssd->parameter->subpage_page));
			state=mask;

			if(lpn==first_lpn)
			{
				offset1=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-new_request->lsn);
				state=state&(0xffffffff<<offset1);
			}
			if(lpn==last_lpn)
			{
				offset2=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-(new_request->lsn+new_request->size));
				state=state&(~(0xffffffff<<offset2));
			}
			/*if (lpn == 322027)
				printf("yes");*/
			ssd=insert2buffer(ssd, lpn, state,NULL,new_request);
			if(ssd == NULL)
			{
				printf("buffer_management()\tError!\n");
			}
			lpn++;
		}
	}
	complete_flag = 1;
	for(j=0;j<=(last_lpn-first_lpn+1)*ssd->parameter->subpage_page/32;j++)
	{// �����Ƿ���������Ҫ��distribute��������
		if(new_request->need_distr_flag[j] != 0)
		{
			complete_flag = 0;
		}
	}

	/*************************************************************
	*�������ȫ����buffer���񣬸�������Ա�ֱ����Ӧ��������
	*�������dram�ķ���ʱ��Ϊ1000ns
	**************************************************************/
	if((complete_flag == 1)&&(new_request->subs==NULL))               
	{
		if(new_request->operation == READ)
			services_2_read_in_buffer++;
		else
			services_2_write_in_buffer++;
		new_request->begin_time=ssd->current_time;
		new_request->response_time=ssd->current_time+1000;            
	}

	return ssd;
}

/*****************************
*lpn��ppn��ת��
******************************/
unsigned int lpn2ppn(struct ssd_info *ssd,unsigned int lsn)
{
	int lpn, ppn;	
	struct entry *p_map = ssd->dram->map->map_entry;
#ifdef DEBUG
	printf("enter lpn2ppn,  current time:%I64u\n",ssd->current_time);
#endif
	lpn = lsn/ssd->parameter->subpage_page;			//lpn
	ppn = (p_map[lpn]).pn;
	return ppn;
}

/**********************************************************************************
*�����������������������ֻ���������д�����Ѿ���buffer_management()�����д�����
*����������к�buffer���еļ�飬��ÿ������ֽ�������󣬽���������й���channel�ϣ�
*��ͬ��channel���Լ������������
**********************************************************************************/
struct ssd_info *distribute(struct ssd_info *ssd) 
{
	unsigned int start, end, first_lsn,last_lsn,lpn,flag=0,flag_attached=0,full_page;
	unsigned int j, k, sub_size;
	int i=0;
	struct request *req = NULL;
	struct sub_request *sub = NULL;
	unsigned int* complt = NULL;


	#ifdef DEBUG
	printf("enter distribute,  current time:%I64u\n",ssd->current_time);
	#endif
	full_page=~(0xffffffff<<ssd->parameter->subpage_page);

	req = ssd->request_tail;
	if(req->response_time != 0){
		return ssd;
	}
	if (req->operation==WRITE)
	{
		return ssd;
	}

	if(req != NULL)
	{
		// ���if�ж�û�б�Ҫ
		if(req->distri_flag == 0)
		{
			// �����������ȫ����buffer���񣬻���һЩ��������Ҫ����
			if(req->complete_lsn_count != ssd->request_tail->size)
			{		
				first_lsn = req->lsn;				
				last_lsn = first_lsn + req->size;
				complt = req->need_distr_flag;
				start = first_lsn - first_lsn % ssd->parameter->subpage_page;
				end = ((last_lsn - 1)/ssd->parameter->subpage_page + 1) * ssd->parameter->subpage_page;
				i = (end - start)/32;	
				while(i >= 0)
				{	
					/*************************************************************************************
					*һ��32λ���������ݵ�ÿһλ����һ����ҳ��32/ssd->parameter->subpage_page�ͱ�ʾ�ж���ҳ��
					*�����ÿһҳ��״̬��������� req->need_distr_flag�У�Ҳ����complt�У�ͨ���Ƚ�complt��
					*ÿһ����full_page���Ϳ���֪������һҳ�Ƿ�����ɡ����û���������ͨ��creat_sub_request
					*��������������
					*************************************************************************************/
					for(j=0; j<32/ssd->parameter->subpage_page; j++)
					{	
						k = (complt[((end-start)/32-i)] >>(ssd->parameter->subpage_page*j)) & full_page;	
						if (k !=0)
						{
							lpn = start/ssd->parameter->subpage_page+ ((end-start)/32-i)*32/ssd->parameter->subpage_page + j;
							sub_size=transfer_size(ssd,k,lpn,req);    
							if (sub_size==0) 
							{
								continue;
							}
							else
							{
								sub = creat_read_sub_request(ssd, lpn, sub_size, req);
								//sub=creat_sub_request(ssd,lpn,sub_size,0,req,req->operation);
							}	
						}
					}
					i = i-1;
				}

			}
			else
			{
				req->begin_time=ssd->current_time;
				req->response_time=ssd->current_time+1000;   
			}

		}
	}
	return ssd;
}


/**********************************************************************
*trace_output()��������ÿһ����������������󾭹�process()�����������
*��ӡ�����ص����н����outputfile�ļ��У�����Ľ����Ҫ�����е�ʱ��
**********************************************************************/
void trace_output(struct ssd_info* ssd) {
	int flag = 1;
	__int64 start_time, end_time;
	struct request* req, * pre_node;
	struct sub_request* sub, * tmp;

#ifdef DEBUG
	printf("enter trace_output,  current time:%I64u\n", ssd->current_time);
#endif

	pre_node = NULL;
	req = ssd->request_queue;
	start_time = 0;
	end_time = 0;

	if (req == NULL)
		return;

	while (req != NULL)
	{
		sub = req->subs;
		flag = 1;
		start_time = 0;
		end_time = 0;
		if (req->response_time != 0)
		{
			fprintf(ssd->outputfile, "%16I64u %10u %6u %2u %16I64u %16I64u %10I64u\n", req->time, req->lsn, req->size, req->operation, req->begin_time, req->response_time, req->response_time - req->time);
			fflush(ssd->outputfile);

			if (req->response_time - req->begin_time == 0)
			{
				printf("the response time is 0?? \n");
				getchar();
			}

			if (req->operation == READ)
			{
				ssd->read_request_count++;
				ssd->read_avg = ssd->read_avg + (req->response_time - req->time);
			}
			else
			{
				ssd->write_request_count++;
				ssd->write_avg = ssd->write_avg + (req->response_time - req->time);
			}

			if (pre_node == NULL)
			{
				if (req->next_node == NULL)
				{
					free(req->need_distr_flag);
					req->need_distr_flag = NULL;
					free(req);
					req = NULL;
					ssd->request_queue = NULL;
					ssd->request_tail = NULL;
					ssd->request_queue_length--;
				}
				else
				{
					ssd->request_queue = req->next_node;
					pre_node = req;
					req = req->next_node;
					free(pre_node->need_distr_flag);
					pre_node->need_distr_flag = NULL;
					free((void*)pre_node);
					pre_node = NULL;
					ssd->request_queue_length--;
				}
			}
			else
			{
				if (req->next_node == NULL)
				{
					pre_node->next_node = NULL;
					free(req->need_distr_flag);
					req->need_distr_flag = NULL;
					free(req);
					req = NULL;
					ssd->request_tail = pre_node;
					ssd->request_queue_length--;
				}
				else
				{
					pre_node->next_node = req->next_node;
					free(req->need_distr_flag);
					req->need_distr_flag = NULL;
					free((void*)req);
					req = pre_node->next_node;
					ssd->request_queue_length--;
				}
			}
		}
		else
		{
			flag = 1;
			while (sub != NULL)
			{
				if (start_time == 0)
					start_time = sub->begin_time;
				if (start_time > sub->begin_time)
					start_time = sub->begin_time;
				if (end_time < sub->complete_time)
					end_time = sub->complete_time;
				if ((sub->current_state == SR_COMPLETE) || ((sub->next_state == SR_COMPLETE) && (sub->next_state_predict_time <= ssd->current_time)))	// if any sub-request is not completed, the request is not completed
				{
					sub = sub->next_subs;
				}
				else
				{
					flag = 0;
					break;
				}

			}

			if (flag == 1)
			{
				fprintf(ssd->outputfile, "%16I64u %10u %6u %2u %16I64u %16I64u %10I64u\n", req->time, req->lsn, req->size, req->operation, start_time, end_time, end_time - req->time);
				fflush(ssd->outputfile);

				if (end_time - start_time == 0)
				{
					printf("the response time is 0?? \n");
					getchar();
				}

				if (req->operation == READ)
				{
					ssd->read_request_count++;
					ssd->read_avg = ssd->read_avg + (end_time - req->time);
				}
				else
				{
					ssd->write_request_count++;
					ssd->write_avg = ssd->write_avg + (end_time - req->time);
				}

				while (req->subs != NULL)
				{
					tmp = req->subs;
					req->subs = tmp->next_subs;
					if (tmp->update != NULL)
					{
						free(tmp->update->location);
						tmp->update->location = NULL;
						free(tmp->update);
						tmp->update = NULL;
					}
					free(tmp->location);
					tmp->location = NULL;
					free(tmp);
					tmp = NULL;

				}

				if (pre_node == NULL)
				{
					if (req->next_node == NULL)
					{
						free(req->need_distr_flag);
						req->need_distr_flag = NULL;
						free(req);
						req = NULL;
						ssd->request_queue = NULL;
						ssd->request_tail = NULL;
						ssd->request_queue_length--;
					}
					else
					{
						ssd->request_queue = req->next_node;
						pre_node = req;
						req = req->next_node;
						free(pre_node->need_distr_flag);
						pre_node->need_distr_flag = NULL;
						free(pre_node);
						pre_node = NULL;
						ssd->request_queue_length--;
					}
				}
				else
				{
					if (req->next_node == NULL)
					{
						pre_node->next_node = NULL;
						free(req->need_distr_flag);
						req->need_distr_flag = NULL;
						free(req);
						req = NULL;
						ssd->request_tail = pre_node;
						ssd->request_queue_length--;
					}
					else
					{
						pre_node->next_node = req->next_node;
						free(req->need_distr_flag);
						req->need_distr_flag = NULL;
						free(req);
						req = pre_node->next_node;
						ssd->request_queue_length--;
					}

				}
			}
			else
			{
				pre_node = req;
				req = req->next_node;
			}
		}
	}
}


/*******************************************************************************
*statistic_output()������Ҫ�����������һ����������ش�����Ϣ��
*1�������ÿ��plane�Ĳ���������plane_erase���ܵĲ���������erase
*2����ӡmin_lsn��max_lsn��read_count��program_count��ͳ����Ϣ���ļ�outputfile�С�
*3����ӡ��ͬ����Ϣ���ļ�statisticfile��
*******************************************************************************/
void statistic_output(struct ssd_info *ssd)
{
	unsigned int lpn_count=0,i,j,k,l,m,erase=0,plane_erase=0;
	double gc_energy=0.0;
#ifdef DEBUG
	printf("enter statistic_output,  current time:%I64u\n",ssd->current_time);
#endif

	for(i=0;i<ssd->parameter->channel_number;i++)
	{
		for(j=0;j<ssd->parameter->chip_channel[i];j++)
		{
			for(k=0;k<ssd->parameter->die_chip;k++)
			{
				for(l = 0; l < ssd->parameter->plane_die; l++){
					plane_erase=0;
					for(m=0;m<ssd->parameter->block_plane;m++)
					{
						if(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].erase_count>0)
						{
							erase=erase+ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].erase_count;
							plane_erase+=ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].erase_count;
						}
					}
					fprintf(ssd->outputfile,"the %d channel, %d chip, %d die, %d plane has : %13d erase operations\n",i,j,k,l,plane_erase);
					fprintf(ssd->statisticfile,"the %d channel, %d chip, %d die, %d plane has : %13d erase operations\n",i,j,k,l,plane_erase);
				}
			}
		}
	}
	for(i=0;i<ssd->parameter->channel_number;i++)
	{
		for(j=0;j<ssd->parameter->chip_channel[i];j++)
		{
			for(k=0;k<ssd->parameter->die_chip;k++)
			{
				for(l = 0; l < ssd->parameter->plane_die; l++){
				/*
					plane_erase=0;
					for(m=0;m<ssd->parameter->block_plane;m++)
					{
						if(ssd->channel_head[i].chip_head[0].die_head[j].plane_head[k].blk_head[m].erase_count>0)
						{
							erase=erase+ssd->channel_head[i].chip_head[0].die_head[j].plane_head[k].blk_head[m].erase_count;
							plane_erase+=ssd->channel_head[i].chip_head[0].die_head[j].plane_head[k].blk_head[m].erase_count;
						}
					}
					*/
					fprintf(ssd->outputfile,"the %d channel, %d chip, %d die, %d plane has : %13d free page\n",i,j,k,l,
						ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].free_page);
					fprintf(ssd->statisticfile,"the %d channel, %d chip, %d die, %d plane has : %13d free page \n",i,j,k,l,
						ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].free_page);
				}
			}
		}
	}

	fprintf(ssd->outputfile,"\n");
	fprintf(ssd->outputfile,"\n");
	fprintf(ssd->outputfile,"---------------------------statistic data---------------------------\n");	 
	fprintf(ssd->outputfile,"min lsn: %13d\n",ssd->min_lsn);	
	fprintf(ssd->outputfile,"max lsn: %13d\n",ssd->max_lsn);
	fprintf(ssd->outputfile,"read count: %13d\n",ssd->read_count);	  
	fprintf(ssd->outputfile,"program count: %13d",ssd->program_count);	
	fprintf(ssd->outputfile,"                        include the flash write count leaded by read requests\n");
	fprintf(ssd->outputfile,"the read operation leaded by un-covered update count: %13d\n",ssd->update_read_count);
	fprintf(ssd->outputfile,"erase count: %13d\n",ssd->erase_count);
	fprintf(ssd->outputfile,"direct erase count: %13d\n",ssd->direct_erase_count);
	fprintf(ssd->outputfile,"copy back count: %13d\n",ssd->copy_back_count);
	fprintf(ssd->outputfile,"multi-plane program count: %13d\n",ssd->m_plane_prog_count);
	fprintf(ssd->outputfile,"multi-plane read count: %13d\n",ssd->m_plane_read_count);
	fprintf(ssd->outputfile,"interleave write count: %13d\n",ssd->interleave_count);
	fprintf(ssd->outputfile,"interleave read count: %13d\n",ssd->interleave_read_count);
	fprintf(ssd->outputfile,"interleave two plane and one program count: %13d\n",ssd->inter_mplane_prog_count);
	fprintf(ssd->outputfile,"interleave two plane count: %13d\n",ssd->inter_mplane_count);
	fprintf(ssd->outputfile,"gc copy back count: %13d\n",ssd->gc_copy_back);
	fprintf(ssd->outputfile,"write flash count: %13d\n",ssd->write_flash_count);
	fprintf(ssd->outputfile,"interleave erase count: %13d\n",ssd->interleave_erase_count);
	fprintf(ssd->outputfile,"multiple plane erase count: %13d\n",ssd->mplane_erase_conut);
	fprintf(ssd->outputfile,"interleave multiple plane erase count: %13d\n",ssd->interleave_mplane_erase_count);
	fprintf(ssd->outputfile,"read request count: %13d\n",ssd->read_request_count);
	fprintf(ssd->outputfile,"write request count: %13d\n",ssd->write_request_count);
	fprintf(ssd->outputfile,"read request average size: %13f\n",ssd->ave_read_size);
	fprintf(ssd->outputfile,"write request average size: %13f\n",ssd->ave_write_size);
	if(ssd->read_request_count != 0)
		fprintf(ssd->outputfile,"read request average response time: %16I64u\n",ssd->read_avg/ssd->read_request_count);
	if(ssd->write_request_count != 0)
		fprintf(ssd->outputfile,"write request average response time: %16I64u\n", ssd->write_avg / ssd->write_request_count);
	fprintf(ssd->outputfile, "request average response time: %16I64u\n", (ssd->read_avg + ssd->write_avg) / (ssd->read_request_count + ssd->write_request_count));
	fprintf(ssd->outputfile,"buffer read hits: %13d\n",ssd->dram->buffer->read_hit);
	fprintf(ssd->outputfile,"buffer read miss: %13d\n",ssd->dram->buffer->read_miss_hit);
	fprintf(ssd->outputfile,"buffer write hits: %13d\n",ssd->dram->buffer->write_hit);
	fprintf(ssd->outputfile,"buffer write miss: %13d\n",ssd->dram->buffer->write_miss_hit);
	fprintf(ssd->outputfile,"erase: %13d\n",erase);
	fprintf(ssd->outputfile,"write flash gc count :%13d\n",ssd->write_flash_gc_count);
	fprintf(ssd->outputfile,"total_gc_move_page_count :%13d\n",ssd->total_gc_move_page_count);
	if(ssd->erase_count != 0)
		fprintf(ssd->outputfile,"avr_move page per GC :%13d\n",ssd->total_gc_move_page_count/ssd->erase_count);
	fflush(ssd->outputfile);

	fclose(ssd->outputfile);


	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile,"---------------------------statistic data---------------------------\n");	
	fprintf(ssd->statisticfile,"min lsn: %13d\n",ssd->min_lsn);	
	fprintf(ssd->statisticfile,"max lsn: %13d\n",ssd->max_lsn);
	fprintf(ssd->statisticfile,"read count: %13d\n",ssd->read_count);	  
	fprintf(ssd->statisticfile,"program count: %13d",ssd->program_count);	  
	fprintf(ssd->statisticfile,"                        include the flash write count leaded by read requests\n");
	fprintf(ssd->statisticfile,"the read operation leaded by un-covered update count: %13d\n",ssd->update_read_count);
	fprintf(ssd->statisticfile,"erase count: %13d\n",ssd->erase_count);	  
	fprintf(ssd->statisticfile,"direct erase count: %13d\n",ssd->direct_erase_count);
	fprintf(ssd->statisticfile,"copy back count: %13d\n",ssd->copy_back_count);
	fprintf(ssd->statisticfile,"multi-plane program count: %13d\n",ssd->m_plane_prog_count);
	fprintf(ssd->statisticfile,"multi-plane read count: %13d\n",ssd->m_plane_read_count);
	fprintf(ssd->statisticfile,"interleave count: %13d\n",ssd->interleave_count);
	fprintf(ssd->statisticfile,"interleave read count: %13d\n",ssd->interleave_read_count);
	fprintf(ssd->statisticfile,"interleave two plane and one program count: %13d\n",ssd->inter_mplane_prog_count);
	fprintf(ssd->statisticfile,"interleave two plane count: %13d\n",ssd->inter_mplane_count);
	fprintf(ssd->statisticfile,"gc copy back count: %13d\n",ssd->gc_copy_back);
	fprintf(ssd->statisticfile,"write flash count: %13d\n",ssd->write_flash_count);
	fprintf(ssd->statisticfile,"waste page count: %13d\n",ssd->waste_page_count);
	fprintf(ssd->statisticfile,"interleave erase count: %13d\n",ssd->interleave_erase_count);
	fprintf(ssd->statisticfile,"multiple plane erase count: %13d\n",ssd->mplane_erase_conut);
	fprintf(ssd->statisticfile,"interleave multiple plane erase count: %13d\n",ssd->interleave_mplane_erase_count);
	fprintf(ssd->statisticfile,"read request count: %13d\n",ssd->read_request_count);
	fprintf(ssd->statisticfile,"write request count: %13d\n",ssd->write_request_count);
	fprintf(ssd->statisticfile,"read request average size: %13f\n",ssd->ave_read_size);
	fprintf(ssd->statisticfile,"write request average size: %13f\n",ssd->ave_write_size);
	if(ssd->read_request_count != 0)
		fprintf(ssd->statisticfile,"read request average response time: %16I64u\n",ssd->read_avg/ssd->read_request_count);
	if(ssd->write_request_count != 0)
		fprintf(ssd->statisticfile,"write request average response time: %16I64u\n",ssd->write_avg/ssd->write_request_count);
	fprintf(ssd->statisticfile, "request average response time: %16I64u\n", (ssd->read_avg + ssd->write_avg) / (ssd->read_request_count + ssd->write_request_count));
	fprintf(ssd->statisticfile,"buffer read hits: %13d\n",ssd->dram->buffer->read_hit);
	fprintf(ssd->statisticfile,"buffer read miss: %13d\n",ssd->dram->buffer->read_miss_hit);
	fprintf(ssd->statisticfile,"buffer write hits: %13d\n",ssd->dram->buffer->write_hit);
	fprintf(ssd->statisticfile,"buffer write miss: %13d\n",ssd->dram->buffer->write_miss_hit);
	fprintf(ssd->statisticfile,"erase: %13d\n",erase);
	fprintf(ssd->statisticfile,"write flash gc count :%13d\n",ssd->write_flash_gc_count);
	fprintf(ssd->statisticfile,"total_gc_move_page_count :%13d\n",ssd->total_gc_move_page_count);
	if(ssd->erase_count != 0)
		fprintf(ssd->statisticfile,"avr_move page per GC :%13d\n",ssd->total_gc_move_page_count/ssd->erase_count);
	fflush(ssd->statisticfile);

	fclose(ssd->statisticfile);
#ifdef USE_PARITY
	fclose(ssd->parity_file);
#endif
}


/***********************************************************************************
*����ÿһҳ��״̬�����ÿһ��Ҫ�������ҳ����Ŀ��Ҳ����һ����������Ҫ�������ҳ��ҳ��
************************************************************************************/
unsigned int size(unsigned int stored)
{
	unsigned int i, total = 0, mask = 0x1;

	#ifdef DEBUG
	printf("enter size\n");
	#endif
	for(i=1;i<=32;i++)
	{
		if(stored & mask) 
			total++;
		stored>>=1;
	}
	#ifdef DEBUG
	printf("leave size\n");
	#endif
    return total;
}


/*********************************************************
*transfer_size()���������þ��Ǽ�������������Ҫ�����size
*�����е���������first_lpn��last_lpn�������ر��������Ϊ��
*��������º��п��ܲ��Ǵ���һ��ҳ���Ǵ���һҳ��һ���֣���
*Ϊlsn�п��ܲ���һҳ�ĵ�һ����ҳ��
*********************************************************/
unsigned int transfer_size(struct ssd_info *ssd,int need_distribute,unsigned int lpn,struct request *req)
{
	unsigned int first_lpn,last_lpn,state,trans_size;
	unsigned int mask=0,offset1=0,offset2=0;

	first_lpn=req->lsn/ssd->parameter->subpage_page;
	last_lpn=(req->lsn+req->size-1)/ssd->parameter->subpage_page;

	mask=~(0xffffffff<<(ssd->parameter->subpage_page));
	state=mask;
	if(lpn==first_lpn)
	{
		offset1=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-req->lsn);
		state=state&(0xffffffff<<offset1);
	}
	if(lpn==last_lpn)
	{
		offset2=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-(req->lsn+req->size));
		state=state&(~(0xffffffff<<offset2));
	}

	trans_size=size(state&need_distribute);

	return trans_size;
}


/**********************************************************************************************************  
*__int64 find_nearest_event(struct ssd_info *ssd)       
*Ѱ����������������絽����¸�״̬ʱ��,���ȿ��������һ��״̬ʱ�䣬���������¸�״̬ʱ��С�ڵ��ڵ�ǰʱ�䣬
*˵��������������Ҫ�鿴channel���߶�Ӧdie����һ״̬ʱ�䡣Int64���з��� 64 λ�����������ͣ�ֵ���ͱ�ʾֵ����
*-2^63 ( -9,223,372,036,854,775,808)��2^63-1(+9,223,372,036,854,775,807 )֮����������洢�ռ�ռ 8 �ֽڡ�
*channel,die���¼���ǰ�ƽ��Ĺؼ����أ������������ʹ�¼�������ǰ�ƽ���channel��die�ֱ�ص�idle״̬��die�е�
*������׼������
***********************************************************************************************************/
__int64 find_nearest_event(struct ssd_info *ssd) 
{
	unsigned int i,j;
	__int64 time=0x7fffffffffffffff;
	__int64 time1=0x7fffffffffffffff;
	__int64 time2=0x7fffffffffffffff;
	unsigned int static current_times = 0;
	static long long last_return_time =0;

	for (i=0;i<ssd->parameter->channel_number;i++)
	{
		if (ssd->channel_head[i].next_state==CHANNEL_IDLE)
			if(time1>ssd->channel_head[i].next_state_predict_time)
				if (ssd->channel_head[i].next_state_predict_time>ssd->current_time)    
					time1=ssd->channel_head[i].next_state_predict_time;
		for (j=0;j<ssd->parameter->chip_channel[i];j++)
		{
			if ((ssd->channel_head[i].chip_head[j].next_state==CHIP_IDLE)||(ssd->channel_head[i].chip_head[j].next_state==CHIP_DATA_TRANSFER))
				if(time2>ssd->channel_head[i].chip_head[j].next_state_predict_time)
					if (ssd->channel_head[i].chip_head[j].next_state_predict_time>ssd->current_time)    
						time2=ssd->channel_head[i].chip_head[j].next_state_predict_time;	
		}  
	} 
/* #ifdef USE_WHITE_PARITY
	if((time1 == 0x7fffffffffffffff)&&(0x7fffffffffffffff == time2))
		for (i=0;i<ssd->parameter->channel_number;i++)
		{
			if(ssd->channel_head[i].subs_r_head != NULL)
			{
				if(ssd->channel_head[i].subs_r_head->current_state == SR_WAIT)
				{
				
					if(current_times < ssd->parameter->channel_number)
					{
						if(last_return_time == ssd->current_time)
							current_times++;
						else
							current_times = 0;
						last_return_time = ssd->current_time;
						
						return ssd->current_time;
					}
					else
					{
						current_times = 0;
					}
				}
			}
		}
#endif */

	/*****************************************************************************************************
	 *timeΪ���� A.��һ״̬ΪCHANNEL_IDLE����һ״̬Ԥ��ʱ�����ssd��ǰʱ���CHANNEL����һ״̬Ԥ��ʱ��
	 *           B.��һ״̬ΪCHIP_IDLE����һ״̬Ԥ��ʱ�����ssd��ǰʱ���DIE����һ״̬Ԥ��ʱ��
	 *           C.��һ״̬ΪCHIP_DATA_TRANSFER����һ״̬Ԥ��ʱ�����ssd��ǰʱ���DIE����һ״̬Ԥ��ʱ��
	 *CHIP_DATA_TRANSFER��׼����״̬�������Ѵӽ��ʴ�����register����һ״̬�Ǵ�register����buffer�е���Сֵ 
	 *ע����ܶ�û������Ҫ���time����ʱtime����0x7fffffffffffffff ��
	*****************************************************************************************************/
	time=(time1>time2)?time2:time1;
	return time;
}

/***********************************************
*free_all_node()���������þ����ͷ���������Ľڵ�
************************************************/
void free_all_node(struct ssd_info *ssd)
{
	unsigned int i,j,k,l,n;
	struct buffer_group *pt=NULL;
	struct direct_erase * erase_node=NULL;
	for (i=0;i<ssd->parameter->channel_number;i++)
	{
		for (j=0;j<ssd->parameter->chip_channel[0];j++)
		{
			for (k=0;k<ssd->parameter->die_chip;k++)
			{
				for (l=0;l<ssd->parameter->plane_die;l++)
				{
					for (n=0;n<ssd->parameter->block_plane;n++)
					{
						free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[n].page_head);
						ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[n].page_head=NULL;
					}
					free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head);
					ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head=NULL;
					while(ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node!=NULL)
					{
						erase_node=ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node;
						ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].erase_node=erase_node->next_node;
						free(erase_node);
						erase_node=NULL;
					}
				}
				
				free(ssd->channel_head[i].chip_head[j].die_head[k].plane_head);
				ssd->channel_head[i].chip_head[j].die_head[k].plane_head=NULL;
			}
			free(ssd->channel_head[i].chip_head[j].die_head);
			ssd->channel_head[i].chip_head[j].die_head=NULL;
		}
		free(ssd->channel_head[i].chip_head);
		ssd->channel_head[i].chip_head=NULL;
	}
	free(ssd->channel_head);
	ssd->channel_head=NULL;

	avlTreeDestroy( ssd->dram->buffer);
	ssd->dram->buffer=NULL;
	
	free(ssd->dram->map->map_entry);
	ssd->dram->map->map_entry=NULL;
	free(ssd->dram->map);
	ssd->dram->map=NULL;
	free(ssd->dram);
	ssd->dram=NULL;
	free(ssd->parameter);
	ssd->parameter=NULL;

	free(ssd);
	ssd=NULL;
}


/*****************************************************************************
*make_aged()���������þ���ģ����ʵ���ù�һ��ʱ���ssd��
*��ô���ssd����Ӧ�Ĳ�����Ҫ�ı䣬�����������ʵ���Ͼ��Ƕ�ssd�и��������ĸ�ֵ��
******************************************************************************/
struct ssd_info *make_aged(struct ssd_info *ssd)
{
	unsigned int i,j,k,l,m,n,ppn;
	int threshould,flag=0;
    
	if (ssd->parameter->aged == 1)
	{
		//threshold��ʾһ��plane���ж���ҳ��Ҫ��ǰ��ΪʧЧ
		threshould=(int)(ssd->parameter->block_plane*ssd->parameter->page_block*ssd->parameter->aged_ratio);  
		for (i=0;i<ssd->parameter->channel_number;i++)
			for (j=0;j<ssd->parameter->chip_channel[i];j++)
				for (k=0;k<ssd->parameter->die_chip;k++)
					for (l=0;l<ssd->parameter->plane_die;l++)
					{  
						flag=0;
						for (m=0;m<ssd->parameter->block_plane;m++)
						{  
							if (flag>=threshould)
							{
								break;
							}
							for (n=0;n<(ssd->parameter->page_block*ssd->parameter->aged_ratio+1);n++)
							{  
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].valid_state=0;        //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].free_state=0;         //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].page_head[n].lpn=0;  //��valid_state free_state lpn����Ϊ0��ʾҳʧЧ������ʱ�������⣬����lpn=0��������Чҳ
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].free_page_num--;
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].invalid_page_num++;
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].blk_head[m].last_write_page++;
								ssd->channel_head[i].chip_head[j].die_head[k].plane_head[l].free_page--;
								flag++;

								ppn=find_ppn(ssd,i,j,k,l,m,n);
							
							}
						} 
					}	 
	}  
	else
	{
		return ssd;
	}

	return ssd;
}


/*********************************************************************************************
*no_buffer_distribute()�����Ǵ���ssdû��dram��ʱ��
*���Ƕ�д����Ͳ�������Ҫ��buffer����Ѱ�ң�ֱ������creat_sub_request()��������������
*������������ص���Ӧͨ���Ķ�������л�д������е�β�����ٴ���
*********************************************************************************************/
struct ssd_info *no_buffer_distribute(struct ssd_info *ssd)
{
	unsigned int lsn,lpn,last_lpn,first_lpn,complete_flag=0, state;
	unsigned int flag=0,flag1=1,active_region_flag=0;           //to indicate the lsn is hitted or not
	unsigned int last_flag;
	unsigned int parity_size=0,parity_state=0;
	unsigned int start_lpn =0;
	struct request *req=NULL;
	struct sub_request *sub=NULL,*sub_r=NULL,*update=NULL;
	struct local *loc=NULL;
	struct channel_info *p_ch=NULL;


	
	unsigned int mask=0; 
	unsigned int offset1=0, offset2=0;
	unsigned int sub_size=0;
	unsigned int sub_state=0;
	unsigned int origin_lpn =0;

	

	ssd->dram->current_time=ssd->current_time;
	req=ssd->request_tail;       
	lsn=req->lsn;
	lpn=req->lsn/ssd->parameter->subpage_page;
	last_lpn=(req->lsn+req->size-1)/ssd->parameter->subpage_page;
	first_lpn=req->lsn/ssd->parameter->subpage_page;
	if(req->begin_time == 1862767438000000)
		printf("deal error");

	if(req->operation==READ)        
	{		
		while(lpn<=last_lpn) 		
		{
			#ifdef USE_BLACK_PARITY
				origin_lpn = lpn;
				lpn = spread_lpn(ssd,lpn);
			#endif
			sub_state=(ssd->dram->map->map_entry[lpn].state&0x7fffffff);
			sub_size=size(sub_state);
			sub=creat_sub_request(ssd,lpn,sub_size,sub_state,req,req->operation);
			#ifdef USE_BLACK_PARITY
				lpn = shrink_lpn(ssd,lpn);
			#endif
			lpn++;
		}
	}
	else if(req->operation==WRITE)
	{
		while(lpn<=last_lpn)     	
		{	
			mask=~(0xffffffff<<(ssd->parameter->subpage_page));
			state=mask;
			last_flag = 0;
			if(lpn==first_lpn)
			{
				offset1=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-req->lsn);
				state=state&(0xffffffff<<offset1);
			}
			if(lpn==last_lpn)
			{ 
				offset2=ssd->parameter->subpage_page-((lpn+1)*ssd->parameter->subpage_page-(req->lsn+req->size));
				state=state&(~(0xffffffff<<offset2));
				last_flag =1;
			}
			if(lsn == 6512231)
				printf("5480958\n");

			sub_size=size(state);
			#ifdef USE_BLACK_PARITY
				origin_lpn = lpn;
				lpn = spread_lpn(ssd,lpn);
			#endif
			sub=creat_sub_request(ssd,lpn,sub_size,state,req,req->operation);

			#ifdef USE_BLACK_PARITY
			parity_state = parity_state|sub->state;//sub->state contain the state of update state ;
			if(start_lpn == 0)
				start_lpn = lpn;
			if((last_flag)||(lpn%(1+PARITY_SIZE) >= PARITY_SIZE-1))
			{
				parity_size = size(parity_state);
				creat_parity_sub_request(ssd,start_lpn,lpn,parity_size,parity_state,req,req->operation);
				parity_state = 0 ; 
				start_lpn = 0;
			}
			lpn = shrink_lpn(ssd,lpn);
			#endif

			lpn++;
		}
	}

	return ssd;
}
