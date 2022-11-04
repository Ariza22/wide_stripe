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

extern int write_flash_sub_count;
extern int release_2_buffer;
extern int buffer_node_miss;
extern int not_miss_node_but_miss_lsn;
extern int services_2_read_in_buffer;
extern int services_2_write_in_buffer;
/********************************************************************************************************************************
1，main函数中initiatio()函数用来初始化ssd,；2，make_aged()函数使SSD成为aged，aged的ssd相当于使用过一段时间的ssd，里面有失效页，
non_aged的ssd是新的ssd，无失效页，失效页的比例可以在初始化参数中设置；3，pre_process_page()函数提前扫一遍读请求，把读请求
的lpn<--->ppn映射关系事先建立好，写请求的lpn<--->ppn映射关系在写的时候再建立，预处理trace防止读请求是读不到数据；4，simulate()是
核心处理函数，trace文件从读进来到处理完成都由这个函数来完成；5，statistic_output()函数将ssd结构中的信息输出到输出文件，输出的是
统计数据和平均数据，输出文件较小，trace_output文件则很大很详细；6，free_all_node()函数释放整个main函数中申请的节点
*********************************************************************************************************************************/
void main()
{
	unsigned  int i,j,k, l;
	struct ssd_info *ssd = NULL;
	int not_process_read_num = 0, not_process_write_num = 0;
	struct request *req = NULL;
#ifdef RECOVERY
	struct recovery_operation *rec = NULL;
	unsigned int recovery_num;
#endif
	
	ssd=(struct ssd_info*)malloc(sizeof(struct ssd_info));
	alloc_assert(ssd,"ssd");
	memset(ssd,0, sizeof(struct ssd_info));
	/*初始化ssd*/
	ssd = initiation(ssd);

	/*旧化ssd*/
	make_aged(ssd);

	/*基于superblock的读请求预处理操作*/
	ssd = pre_process_superpage(ssd);
	//printf("strip_bit = %d\n",ssd->strip_bit);
	
	/*测试读请求预处理函数，将lpn写入pre_function_test文件*/
	printf("start the pre function test\n");
	pre_process_test(ssd,1);
	//printf("%d ", count);

	/*输出各个lpane剩余的空闲页*/
	printf("page_num_per_plane = %d\n", ssd->parameter->block_plane * ssd->parameter->page_block);
	for (i=0;i<ssd->parameter->channel_number;i++)
	{
		for(l = 0; l < ssd->parameter->chip_channel[0]; l++)
		{
			for (j=0;j<ssd->parameter->die_chip;j++)
			{
				for (k=0;k<ssd->parameter->plane_die;k++)
				{
					printf("%d, %d, %d, %d: %5d\n",i,l,j,k,ssd->channel_head[i].chip_head[l].die_head[j].plane_head[k].free_page);
				}
			}
		}	
	}
	printf("\n");
	fprintf(ssd->outputfile,"\t\t\t\t\t\t\t\t\tOUTPUT\n");
	fprintf(ssd->outputfile,"****************** TRACE INFO ******************\n");


	/*开始进行ssd仿真（读、写、擦除（gc）、数据恢复（recovery）等*/
	printf("begin simulating.......................\n");
	printf("\n");
	printf("\n");
	printf("   ^o^    OK, please wait a moment, and enjoy music and coffee   ^o^    \n");
	printf("buffer_size = %d\n", ssd->dram->buffer->max_buffer_sector);
	ssd=simulate(ssd);
	printf("the simulation is completed!\n");
	/*测试仿真函数，将lpn写入pre_function_test文件*/
#ifdef DEAL_FUNCTION_TEST
	printf("\nstart the pre_process_test\n");
		pre_process_test(ssd,2);
	printf("the pre_process_test is completed!\n\n");
#endif
	printf("\nstart the bad block output\n");
	ouput_bad_block(ssd);

	output_wear_state(ssd);
	/*将仿真结果和请求相关信息输出到statisticfile（.dat) 和outputfile（.out）中*/
	printf("start statistic output\n");
	statistic_output(ssd);
	printf("the statistic output is completed\n\n");
	
	/*输出打点测试数据*/
	printf("write_flash_sub_count = %d\n", write_flash_sub_count);  //写入闪存的请求数
	printf("release_2_buffer = %d\n", release_2_buffer);		    //从buffer刷到superpage buffer的次数
	printf("int buffer_node_miss = %d\tnot_miss_node_but_miss_lsn = %d\n", buffer_node_miss, not_miss_node_but_miss_lsn);	            //buffer节点未命中，命中buffer节点但未命中lsn
	printf("services_2_read_in_buffer = %d\tservices_2_write_in_buffer = %d\n", services_2_read_in_buffer, services_2_write_in_buffer);	//buffer中服务的读写请求个数
	/*统计未完成的请求*/
	req = ssd->request_queue;
	while(req != NULL)
	{
		if(req->operation == READ)
			not_process_read_num++;
		else
			not_process_write_num++;
		req = req->next_node;
	}
	printf("not_process_read_num = %d\tnot_process_write_num = %d\n", not_process_read_num, not_process_write_num);
	/*恢复操作相关测试*/
#ifdef RECOVERY
	printf("bad_page = %d\trecovery_page = %d\n", ssd->broken_page, ssd->recovery_page_num);
	//剩余的恢复操作节点的个数
	recovery_num = get_recovery_node_num(ssd);
	printf("the recovery node number is %d\n", recovery_num);
	//剩余的恢复操作对应的恢复信息（恢复需要的读标志、已读标志）
	rec = ssd->recovery_head;
	if (rec == NULL)
	{
		printf("All broken pages have been recovered already\n");
	}
	else
	{
		while (rec != NULL)
		{
			printf("block_for_recovery = %d\tcomplete_flag = %d\n", rec->block_for_recovery, rec->sub_r_complete_flag);
			rec = rec->next_node;
		}
		printf("\n");
	}
#endif
	//各通道剩余的读写子请求的个数
	for(i = 0; i <ssd->parameter->channel_number; i++)
	{
		printf("channel = %d\tread_num = %d\twrite_num = %d\n", i, sub_r_num_for_channel(ssd, i), sub_w_num_for_channel(ssd, i));
	}
	printf("\n");


	/*释放空间*/
	//free_all_node(ssd);
	getchar();
	
	//_CrtDumpMemoryLeaks();

}
