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
1��main������initiatio()����������ʼ��ssd,��2��make_aged()����ʹSSD��Ϊaged��aged��ssd�൱��ʹ�ù�һ��ʱ���ssd��������ʧЧҳ��
non_aged��ssd���µ�ssd����ʧЧҳ��ʧЧҳ�ı��������ڳ�ʼ�����������ã�3��pre_process_page()������ǰɨһ������󣬰Ѷ�����
��lpn<--->ppnӳ���ϵ���Ƚ����ã�д�����lpn<--->ppnӳ���ϵ��д��ʱ���ٽ�����Ԥ����trace��ֹ�������Ƕ��������ݣ�4��simulate()��
���Ĵ�������trace�ļ��Ӷ�������������ɶ��������������ɣ�5��statistic_output()������ssd�ṹ�е���Ϣ���������ļ����������
ͳ�����ݺ�ƽ�����ݣ�����ļ���С��trace_output�ļ���ܴ����ϸ��6��free_all_node()�����ͷ�����main����������Ľڵ�
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
	/*��ʼ��ssd*/
	ssd = initiation(ssd);

	/*�ɻ�ssd*/
	make_aged(ssd);

	/*����superblock�Ķ�����Ԥ�������*/
	ssd = pre_process_superpage(ssd);
	//printf("strip_bit = %d\n",ssd->strip_bit);
	
	/*���Զ�����Ԥ����������lpnд��pre_function_test�ļ�*/
	printf("start the pre function test\n");
	pre_process_test(ssd,1);
	//printf("%d ", count);

	/*�������lpaneʣ��Ŀ���ҳ*/
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


	/*��ʼ����ssd���棨����д��������gc�������ݻָ���recovery����*/
	printf("begin simulating.......................\n");
	printf("\n");
	printf("\n");
	printf("   ^o^    OK, please wait a moment, and enjoy music and coffee   ^o^    \n");
	printf("buffer_size = %d\n", ssd->dram->buffer->max_buffer_sector);
	ssd=simulate(ssd);
	printf("the simulation is completed!\n");
	/*���Է��溯������lpnд��pre_function_test�ļ�*/
#ifdef DEAL_FUNCTION_TEST
	printf("\nstart the pre_process_test\n");
		pre_process_test(ssd,2);
	printf("the pre_process_test is completed!\n\n");
#endif
	printf("\nstart the bad block output\n");
	ouput_bad_block(ssd);

	output_wear_state(ssd);
	/*�������������������Ϣ�����statisticfile��.dat) ��outputfile��.out����*/
	printf("start statistic output\n");
	statistic_output(ssd);
	printf("the statistic output is completed\n\n");
	
	/*�������������*/
	printf("write_flash_sub_count = %d\n", write_flash_sub_count);  //д�������������
	printf("release_2_buffer = %d\n", release_2_buffer);		    //��bufferˢ��superpage buffer�Ĵ���
	printf("int buffer_node_miss = %d\tnot_miss_node_but_miss_lsn = %d\n", buffer_node_miss, not_miss_node_but_miss_lsn);	            //buffer�ڵ�δ���У�����buffer�ڵ㵫δ����lsn
	printf("services_2_read_in_buffer = %d\tservices_2_write_in_buffer = %d\n", services_2_read_in_buffer, services_2_write_in_buffer);	//buffer�з���Ķ�д�������
	/*ͳ��δ��ɵ�����*/
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
	/*�ָ�������ز���*/
#ifdef RECOVERY
	printf("bad_page = %d\trecovery_page = %d\n", ssd->broken_page, ssd->recovery_page_num);
	//ʣ��Ļָ������ڵ�ĸ���
	recovery_num = get_recovery_node_num(ssd);
	printf("the recovery node number is %d\n", recovery_num);
	//ʣ��Ļָ�������Ӧ�Ļָ���Ϣ���ָ���Ҫ�Ķ���־���Ѷ���־��
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
	//��ͨ��ʣ��Ķ�д������ĸ���
	for(i = 0; i <ssd->parameter->channel_number; i++)
	{
		printf("channel = %d\tread_num = %d\twrite_num = %d\n", i, sub_r_num_for_channel(ssd, i), sub_w_num_for_channel(ssd, i));
	}
	printf("\n");


	/*�ͷſռ�*/
	//free_all_node(ssd);
	getchar();
	
	//_CrtDumpMemoryLeaks();

}
