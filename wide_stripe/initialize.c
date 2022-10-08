/*****************************************************************************************************************************
This project was supported by the National Basic Research 973 Program of China under Grant No.2011CB302301
Huazhong University of Science and Technology (HUST)   Wuhan National Laboratory for Optoelectronics

FileName： initialize.c
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
#include "states.h"

#define FALSE		0
#define TRUE		1

#define ACTIVE_FIXED	0
#define ACTIVE_ADJUST	1


/************************************************************************
* Compare function for AVL Tree                                        
************************************************************************/
extern int keyCompareFunc(TREE_NODE *p , TREE_NODE *p1)
{
	struct buffer_group *T1=NULL,*T2=NULL;

	T1=(struct buffer_group*)p;
	T2=(struct buffer_group*)p1;



	if(T1->group< T2->group) return 1;
	if(T1->group> T2->group) return -1;

	return 0;
}


extern int freeFunc(TREE_NODE *pNode)
{
	
	if(pNode!=NULL)
	{
		free((void *)pNode);
	}
	
	
	pNode=NULL;
	return 1;
}


/**********   initiation   ******************
*modify by zhouwen
*November 08,2011
*initialize the ssd struct to simulate the ssd hardware
*1.this function allocate memory for ssd structure 
*2.set the infomation according to the parameter file
*******************************************/
struct ssd_info *initiation(struct ssd_info *ssd)
{
	unsigned int x=0,y=0,i=0,j=0,k=0,l=0,m=0,n=0;
	errno_t err;
	char buffer[300];
	char out_file[300],dat_file[300],ascii_file[300];
	//char  *rate ;
	struct parameter_value *parameters;
	FILE *fp=NULL;
	
	printf("input parameter file name:");
	/*gets(ssd->parameterfilename);*/
 	strcpy_s(ssd->parameterfilename,16,"page.parameters");

	printf("\ninput trace file name:");
	/*gets(ssd->tracefilename);*/
	//strcpy_s(ssd->tracefilename,25,"Exchange.ascii");
//	strcat(ascii_file,ASCII);
	sprintf(ascii_file,"%s",ASCII);
	strcat(ascii_file,".ascii");
	strcpy_s(ssd->tracefilename,35,ascii_file);
	//strcpy_s(ssd->tracefilename,35,"DisplayAdsDataServer.ascii");
	//strcpy_s(ssd->tracefilename,35,"BuildServer.ascii");
	//strcpy_s(ssd->tracefilename,25,"example1.ascii");

#ifdef USE_PARITY
		printf("\ninput output file name:");
		/*gets(ssd->outputfilename);*/
	#ifdef USE_BLACK_PARITY 
		sprintf(out_file,"%s",USE_BLACK_PARITY);
	#endif
	#ifdef USE_WHITE_PARITY 
		sprintf(out_file,"%s",USE_WHITE_PARITY);
	#endif
	#ifdef GC
		strcat(out_file,GC);
	#endif 
	#ifdef BUF
		strcat(out_file,BUF);
	#endif 
		strcat (out_file,ASCII);
		strcat (out_file,".out");
		strcpy_s(ssd->outputfilename,80,out_file);
		printf("%s",ssd->outputfilename);
		//strcpy_s(ssd->outputfilename,30,"PExchange.dat");
		//strcpy_s(ssd->outputfilename,30,"ngcPCFS.dat");
		//strcpy_s(ssd->outputfilename,30,"PDisplayAdsDataServer.dat");
		//strcpy_s(ssd->outputfilename,30,"PBuildServer.dat");

		printf("\ninput statistic file name:");
		/*gets(ssd->statisticfilename);*/
	#ifdef USE_BLACK_PARITY
		sprintf(dat_file,"%s",USE_BLACK_PARITY);
	#endif
	#ifdef USE_WHITE_PARITY
		sprintf(dat_file,"%s",USE_WHITE_PARITY);
	#endif
	#ifdef GC
		strcat(dat_file,GC);
	#endif 
	#ifdef BUF
		strcat(dat_file,BUF);
	#endif 
		strcat (dat_file,ASCII);
		strcat (dat_file,".dat");
		strcpy_s(ssd->statisticfilename,80,dat_file);
		printf("%s\n",ssd->statisticfilename);
		//strcpy_s(ssd->statisticfilename,30,"PExchange.out");
		//strcpy_s(ssd->statisticfilename,30,"ngcPCFS5.out");

#else
	printf("\ninput output file name:");
	/*gets(ssd->outputfilename);*/

	sprintf(out_file,"%s","Origin");
#ifdef GC
	strcat(out_file,GC);
#endif 
#ifdef BUF
	strcat(out_file,BUF);
#endif 
	strcat (out_file,ASCII);
	strcat (out_file,".out");
	strcpy_s(ssd->outputfilename,80,out_file);
	printf("%s\n",ssd->outputfilename);
	//strcpy_s(ssd->outputfilename,30,"Exchange.dat");
	//strcpy_s(ssd->outputfilename,30,"CFS.dat");
	//strcpy_s(ssd->outputfilename,30,"BDisplayAdsDataServer.dat");
	//strcpy_s(ssd->outputfilename,30,"BuildServer.dat");

	printf("\ninput statistic file name:");
	/*gets(ssd->statisticfilename);*/
	sprintf(dat_file,"%s","Origin");
#ifdef GC
	strcat(dat_file,GC);
#endif 
#ifdef BUF
	strcat(dat_file,BUF);
#endif 
	strcat (dat_file,ASCII);
	strcat (dat_file,".dat");
	strcpy_s(ssd->statisticfilename,80,dat_file);
	printf("%s\n",ssd->statisticfilename);
	//strcpy_s(ssd->statisticfilename,30,"Exchange.out");
	//strcpy_s(ssd->statisticfilename,30,"CFS.out");
	//strcpy_s(ssd->statisticfilename,30,"BDisplayAdsDataServer.out");
	//strcpy_s(ssd->statisticfilename,30,"BuildServer.out");
#endif

	/*strcpy_s(ssd->statisticfilename2 ,16,"statistic2.dat");*/

	//导入ssd的配置文件
	parameters=load_parameters(ssd->parameterfilename);
	ssd->parameter=parameters;
	ssd->min_lsn=0x7fffffff;
	ssd->block_num =  ssd->parameter->chip_num*ssd->parameter->die_chip*ssd->parameter->plane_die*ssd->parameter->block_plane;
	ssd->page=ssd->block_num*ssd->parameter->page_block;
#ifdef RECOVERY
	ssd->broken_page = 0;
	ssd->recovery_page_num = 0;
	ssd->recovery_head = NULL;
	ssd->recovery_tail = NULL;
#endif
	
	
	//初始化 dram
	ssd->dram = (struct dram_info *)malloc(sizeof(struct dram_info));
	alloc_assert(ssd->dram,"ssd->dram");
	memset(ssd->dram,0,sizeof(struct dram_info));
	initialize_dram(ssd);

	//初始化通道
	ssd->channel_head=(struct channel_info*)malloc(ssd->parameter->channel_number * sizeof(struct channel_info));
	alloc_assert(ssd->channel_head,"ssd->channel_head");
	memset(ssd->channel_head,0,ssd->parameter->channel_number * sizeof(struct channel_info));
	initialize_channels(ssd );
#ifdef USE_EC	
	//初始化条带
	ssd->band_num = ssd->parameter->block_plane; //以跨plane具有相同ID的block组织superblock，则条带数为plane中块的个数
	//ssd->band_num = ssd->parameter->chip_channel[0] * ssd->parameter->die_chip * ssd->parameter->plane_die * ssd->parameter->block_plane;	// SSD中的条带数
	ssd->band_head = (struct band_info *)malloc(ssd->band_num * sizeof(struct band_info));
	alloc_assert(ssd->band_head,"ssd->band_head");
	memset(ssd->band_head, 0, ssd->band_num * sizeof(struct band_info));
	ssd = initialize_band(ssd);
#endif
	printf("\n");
	if((err=fopen_s(&ssd->outputfile,ssd->outputfilename,"w")) != 0)
	{
		printf("the output file can't open\n");
		return NULL;
	}

	printf("\n");
	if((err=fopen_s(&ssd->statisticfile,ssd->statisticfilename,"w"))!=0)
	{
		printf("the statistic file can't open\n");
		return NULL;
	}
#ifdef USE_PARITY
	ssd->parity_file = fopen("parity_num.txt","w");
#else
	ssd->parity_file = NULL;
#endif 
	if(ssd->parity_file == NULL)
		printf("open parity_num.txt failed! \n");
	else
	{
#ifdef USE_BLACK_PARITY
		fprintf(ssd->parity_file,"old : parity\n");
#endif
#ifdef  USE_WHITE_PARITY
		fprintf(ssd->parity_file,"parity_count : channel : chip : die : plane : block : page \n");
#endif 
	}




	printf("\n");
// 	if((err=fopen_s(&ssd->statisticfile2,ssd->statisticfilename2,"w"))!=0)
// 	{
// 		printf("the second statistic file can't open\n");
// 		return NULL;
// 	}

	fprintf(ssd->outputfile,"parameter file: %s\n",ssd->parameterfilename); 
	fprintf(ssd->outputfile,"trace file: %s\n",ssd->tracefilename);
	fprintf(ssd->statisticfile,"parameter file: %s\n",ssd->parameterfilename); 
	fprintf(ssd->statisticfile,"trace file: %s\n",ssd->tracefilename);
	
	fflush(ssd->outputfile);
	fflush(ssd->statisticfile);

	if((err=fopen_s(&fp,ssd->parameterfilename,"r"))!=0)
	{
		printf("\nthe parameter file can't open!\n");
		return NULL;
	}

	//fp=fopen(ssd->parameterfilename,"r");

	fprintf(ssd->outputfile,"-----------------------parameter file----------------------\n");
	fprintf(ssd->statisticfile,"-----------------------parameter file----------------------\n");
	while(fgets(buffer,300,fp))
	{
		fprintf(ssd->outputfile,"%s",buffer);
		fflush(ssd->outputfile);
		fprintf(ssd->statisticfile,"%s",buffer);
		fflush(ssd->statisticfile);
	}

	fprintf(ssd->outputfile,"\n");
	fprintf(ssd->outputfile,"-----------------------simulation output----------------------\n");
	fflush(ssd->outputfile);

	fprintf(ssd->statisticfile,"\n");
	fprintf(ssd->statisticfile,"-----------------------simulation output----------------------\n");
	fflush(ssd->statisticfile);

	fclose(fp);
	printf("initiation is completed!\n");

	return ssd;
}


struct dram_info * initialize_dram(struct ssd_info * ssd)
{
	unsigned int page_num, i;
	unsigned int superpege_num = 0;
	int large_lsn, large_lpn;
#ifndef USE_EC
	unsigned int band_num;//记录该SSD阵列中有多少个块条带。
#endif
	struct dram_info *dram=ssd->dram;
	dram->dram_capacity = ssd->parameter->dram_capacity;		
	dram->buffer = (tAVLTree *)avlTreeCreate((void*)keyCompareFunc , (void *)freeFunc);
	dram->buffer->max_buffer_sector=ssd->parameter->dram_capacity/SECTOR; //512
	//superpage_buffer缓存初始化
#ifdef USE_SUPERBLOCK
	dram->superblock_buffer = (tAVLTree *)avlTreeCreate((void *)keyCompareFunc, (void *)freeFunc);
	dram->superblock_buffer->max_buffer_sector = ssd->parameter->channel_number * ssd->parameter->chip_channel[0] * ssd->parameter->die_chip * ssd->parameter->plane_die * ssd->parameter->page_block * ssd->parameter->subpage_page;
	superpege_num = ssd->parameter->channel_number * ssd->parameter->chip_channel[0] * ssd->parameter->die_chip * ssd->parameter->plane_die;
	dram->superpage_buffer = (struct sub_request *)malloc(superpege_num * sizeof(struct sub_request));
	alloc_assert(dram->superpage_buffer, "dram->superpage_buffer");
	memset(dram->superpage_buffer, 0, superpege_num * sizeof(struct sub_request));
#endif
	//映射表初始化
	dram->map = (struct map_info *)malloc(sizeof(struct map_info));
	dram->wear_map = (struct wear_map_info*)malloc(sizeof(struct wear_map_info));
	alloc_assert(dram->map,"dram->map");
	memset(dram->map,0, sizeof(struct map_info));
	memset(dram->wear_map, 0, sizeof(struct wear_map_info));
	//页映射
	/*
	large_lsn=(int)((ssd->parameter->subpage_page*ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num)*(1-ssd->parameter->overprovide));
	large_lsn = large_lsn/(ssd->parameter->subpage_page * BAND_WITDH) *(ssd->parameter->subpage_page*(BAND_WITDH - MAX_EC_MODLE));//原始数据最大扇区
	large_lpn = large_lsn / 4;
	*/
	page_num = ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_num;//5242880
	dram->map->map_entry = (struct entry *)malloc(sizeof(struct entry) * page_num); //每个物理页和逻辑页都有对应关系
	alloc_assert(dram->map->map_entry,"dram->map->map_entry");

	int block_num = ssd->parameter->block_plane * ssd->parameter->plane_die * ssd->parameter->die_chip * ssd->parameter->chip_num;
	dram->wear_map->wear_map_entry = (struct wear_entry*)malloc(sizeof(struct wear_entry) * block_num); //每个物理页和逻辑页都有对应关系

	for(i = 0; i < page_num; i++)
	{
		dram->map->map_entry[i].pn = 0;
		dram->map->map_entry[i].state = 0;
	}

	for (i = 0; i < block_num; i++)
	{
		dram->wear_map->wear_map_entry[i].wear_state = 0;
	}

	//memset(dram->map->map_entry,0,sizeof(struct entry) * page_num);
#ifdef USE_EC
	//memset(ssd->current_band, 0, sizeof(unsigned int) * 3);
	//memset(ssd->strip_bits, 0, sizeof(unsigned int) * 3);
	//ssd->band_num = ssd->parameter->chip_channel[0] * ssd->parameter->die_chip * ssd->parameter->plane_die * ssd->parameter->block_plane;	// SSD中的条带数
	//ssd->current_band = 0; //初始的时候，设置当前条带号为0
	ssd->strip_bit = 0;
	ssd->band_num = ssd->parameter->block_plane; //以跨plane具有相同ID的block组织superblock，则条带数为plane中块的个数
	ssd->free_superblock_num = ssd->parameter->block_plane;
	dram->map->pbn = (int(*)[BAND_WITDH])malloc(sizeof(int)*(BAND_WITDH)*ssd->band_num);
	alloc_assert(dram->map->pbn, "dram->map->pbn");
	memset(dram->map->pbn,-1,sizeof(int)*(BAND_WITDH)*ssd->band_num);

	printf("the size of pbn[1] is :%d\n",sizeof(dram->map->pbn[1]));
	printf("the size of pbn is :%d\n",sizeof(dram->map->pbn));
	printf("the size of pbn[0][2] is: %d\n",sizeof(dram->map->pbn[0][2]));
	printf("the pbn[0][2] is %d :\n",dram->map->pbn[0][2]);
#else
#ifdef USE_WHITE_PARITY
	band_num = ssd->parameter->block_plane*ssd->parameter->plane_die*ssd->parameter->die_chip*ssd->parameter->chip_channel[0];
	dram->map->pbn = (int(*)[5])malloc(sizeof(int)*(1+PARITY_SIZE)*band_num);
	alloc_assert(dram->map->pbn, "dram->map->pbn");
	memset(dram->map->pbn,-1,sizeof(int)*(1+PARITY_SIZE)*band_num);
	
	printf("the size of pbn[1] is :%d\n",sizeof(dram->map->pbn[1]));
	printf("the size of pbn is :%d\n",sizeof(dram->map->pbn));
	printf("the size of pbn[0][2] is: %d\n",sizeof(dram->map->pbn[0][2]));
	printf("the pbn[0][2] is %d :\n",dram->map->pbn[0][2]);
#endif 
#endif
	
	return dram;
}

struct page_info * initialize_page(struct page_info * p_page )
{
	p_page->valid_state =0;
	p_page->free_state = PG_SUB;
	p_page->lpn = -1;
	p_page->written_count=0;
#ifdef BROKEN_PAGE
	p_page->bad_page_flag = FALSE;
#endif
	return p_page;
}

struct blk_info * initialize_block(struct blk_info * p_block,struct parameter_value *parameter)
{
	unsigned int i;
	struct page_info * p_page;
#ifdef BROKEN_BLOCK
	p_block->bad_block_flag = FALSE;
#endif
	p_block->free_page_num = parameter->page_block;	// all pages are free
	p_block->last_write_page = -1;	// no page has been programmed
	p_block->page_head = (struct page_info *)malloc(parameter->page_block * sizeof(struct page_info));
	//p_block->rber_random_seed = ((double)(rand() % 10000 - 5000)) / 10000000;
	alloc_assert(p_block->page_head,"p_block->page_head");
	memset(p_block->page_head,0,parameter->page_block * sizeof(struct page_info));
	for(i = 0; i < parameter->page_block; i++)
	{
		p_page = &(p_block->page_head[i]);
		initialize_page(p_page );
	}
	return p_block;
}

struct plane_info * initialize_plane(struct plane_info * p_plane,struct parameter_value *parameter )
{
	unsigned int i;
	struct blk_info * p_block;
	p_plane->add_reg_ppn = -1;  //plane 里面的额外寄存器additional register -1 表示无数据
	p_plane->free_page=parameter->block_plane*parameter->page_block;

	p_plane->blk_head = (struct blk_info *)malloc(parameter->block_plane * sizeof(struct blk_info));
	alloc_assert(p_plane->blk_head,"p_plane->blk_head");
	memset(p_plane->blk_head,0,parameter->block_plane * sizeof(struct blk_info));
	srand((unsigned)time(NULL));
	for(i = 0; i<parameter->block_plane; i++)
	{
		p_block = &(p_plane->blk_head[i]);
		initialize_block( p_block ,parameter);			
	}
	return p_plane;
}

struct die_info * initialize_die(struct die_info * p_die,struct parameter_value *parameter,long long current_time )
{
	unsigned int i;
	struct plane_info * p_plane;

	p_die->token=0;
		
	p_die->plane_head = (struct plane_info*)malloc(parameter->plane_die * sizeof(struct plane_info));
	alloc_assert(p_die->plane_head,"p_die->plane_head");
	memset(p_die->plane_head,0,parameter->plane_die * sizeof(struct plane_info));

	for (i = 0; i<parameter->plane_die; i++)
	{
		p_plane = &(p_die->plane_head[i]);
		initialize_plane(p_plane,parameter );
	}

	return p_die;
}

struct chip_info * initialize_chip(struct chip_info * p_chip,struct parameter_value *parameter,long long current_time )
{
	unsigned int i;
	struct die_info *p_die;
	
	p_chip->current_state = CHIP_IDLE;
	p_chip->next_state = CHIP_IDLE;
	p_chip->current_time = current_time;
	p_chip->next_state_predict_time = 0;			
	p_chip->die_num = parameter->die_chip;
	p_chip->plane_num_die = parameter->plane_die;
	p_chip->block_num_plane = parameter->block_plane;
	p_chip->page_num_block = parameter->page_block;
	p_chip->subpage_num_page = parameter->subpage_page;
	p_chip->ers_limit = parameter->ers_limit;
	p_chip->token=0;
	p_chip->ac_timing = parameter->time_characteristics;		
	p_chip->read_count = 0;
	p_chip->program_count = 0;
	p_chip->erase_count = 0;

	p_chip->die_head = (struct die_info *)malloc(parameter->die_chip * sizeof(struct die_info));
	alloc_assert(p_chip->die_head,"p_chip->die_head");
	memset(p_chip->die_head,0,parameter->die_chip * sizeof(struct die_info));

	for (i = 0; i<parameter->die_chip; i++)
	{
		p_die = &(p_chip->die_head[i]);
		initialize_die( p_die,parameter,current_time );	
	}

	return p_chip;
}

struct ssd_info * initialize_channels(struct ssd_info * ssd )
{
	unsigned int i,j;
	struct channel_info * p_channel;
	struct chip_info * p_chip;

	// set the parameter of each channel
	for (i = 0; i< ssd->parameter->channel_number; i++)
	{
		p_channel = &(ssd->channel_head[i]);
		p_channel->chip = ssd->parameter->chip_channel[i];
		p_channel->current_state = CHANNEL_IDLE;
		p_channel->next_state = CHANNEL_IDLE;
		
		p_channel->chip_head = (struct chip_info *)malloc(ssd->parameter->chip_channel[i]* sizeof(struct chip_info));
		alloc_assert(p_channel->chip_head,"p_channel->chip_head");
		memset(p_channel->chip_head,0,ssd->parameter->chip_channel[i]* sizeof(struct chip_info));

		for (j = 0; j< ssd->parameter->chip_channel[i]; j++)
		{
			p_chip = &(p_channel->chip_head[j]);
			initialize_chip(p_chip,ssd->parameter,ssd->current_time );
		}
	}

	return ssd;
}

#ifdef USE_EC

/*************************************************
*初始化条带信息
*add by wangxianpeng
*2019/09/10
**************************************************/
struct ssd_info* initialize_band(struct ssd_info *ssd)
{
	unsigned int i;
	for(i = 0; i < ssd->band_num; i++){
		//ssd->band_head[i].ec_modle = rand() % 2 + 1;
		ssd->band_head[i].ec_modle = 1;
		ssd->band_head[i].pe_cycle = 0;
	}
	return ssd;
}
#endif

/*************************************************
*将page.parameters里面的参数导入到ssd->parameter里
*modify by zhouwen
*November 8,2011
**************************************************/
struct parameter_value *load_parameters(char parameter_file[30])
{
	FILE * fp;
	FILE * fp1;
	FILE * fp2;
	errno_t ferr;
	struct parameter_value *p;
	char buf[BUFSIZE];
	int i;
	int pre_eql,next_eql;
	int res_eql;
	char *ptr;

	p = (struct parameter_value *)malloc(sizeof(struct parameter_value));
	alloc_assert(p,"parameter_value");
	memset(p,0,sizeof(struct parameter_value));
	p->queue_length=5;
	memset(buf,0,BUFSIZE);
		
	if((ferr = fopen_s(&fp,parameter_file,"r"))!= 0)
	{	
		printf("the file parameter_file error!\n");	
		return p;
	}
	if((ferr = fopen_s(&fp1,"parameters_name.txt","w"))!= 0)
	{	
		printf("the file parameter_name error!\n");	
		return p;
	}
	if((ferr = fopen_s(&fp2,"parameters_value.txt","w")) != 0)
	{	
		printf("the file parameter_value error!\n");	
		return p;
	}
    


	while(fgets(buf,200,fp)){
		if(buf[0] =='#' || buf[0] == ' ') continue;
		ptr=strchr(buf,'=');
		if(!ptr) continue; 
		
		pre_eql = ptr - buf;
		next_eql = pre_eql + 1;

		while(buf[pre_eql-1] == ' ') pre_eql--;
		buf[pre_eql] = 0;		//添加字符串结束标志
		if((res_eql=strcmp(buf,"chip number")) ==0){			
			sscanf(buf + next_eql,"%d",&p->chip_num);
		}else if((res_eql=strcmp(buf,"dram capacity")) ==0){
			sscanf(buf + next_eql,"%d",&p->dram_capacity);
		}else if((res_eql=strcmp(buf,"cpu sdram")) ==0){
			sscanf(buf + next_eql,"%d",&p->cpu_sdram);
		}else if((res_eql=strcmp(buf,"channel number")) ==0){
			sscanf(buf + next_eql,"%d",&p->channel_number); 
		}else if((res_eql=strcmp(buf,"die number")) ==0){
			sscanf(buf + next_eql,"%d",&p->die_chip); 
		}else if((res_eql=strcmp(buf,"plane number")) ==0){
			sscanf(buf + next_eql,"%d",&p->plane_die); 
		}else if((res_eql=strcmp(buf,"block number")) ==0){
			sscanf(buf + next_eql,"%d",&p->block_plane); 
		}else if((res_eql=strcmp(buf,"page number")) ==0){
			sscanf(buf + next_eql,"%d",&p->page_block); 
		}else if((res_eql=strcmp(buf,"subpage page")) ==0){
			sscanf(buf + next_eql,"%d",&p->subpage_page); 
		}else if((res_eql=strcmp(buf,"page capacity")) ==0){
			sscanf(buf + next_eql,"%d",&p->page_capacity); 
		}else if((res_eql=strcmp(buf,"subpage capacity")) ==0){
			sscanf(buf + next_eql,"%d",&p->subpage_capacity); 
		}else if((res_eql=strcmp(buf,"t_PROG")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tPROG); 
		}else if((res_eql=strcmp(buf,"t_DBSY")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tDBSY); 
		}else if((res_eql=strcmp(buf,"t_BERS")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tBERS); 
		}else if((res_eql=strcmp(buf,"t_CLS")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCLS); 
		}else if((res_eql=strcmp(buf,"t_CLH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCLH); 
		}else if((res_eql=strcmp(buf,"t_CS")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCS); 
		}else if((res_eql=strcmp(buf,"t_CH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCH); 
		}else if((res_eql=strcmp(buf,"t_WP")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tWP); 
		}else if((res_eql=strcmp(buf,"t_ALS")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tALS); 
		}else if((res_eql=strcmp(buf,"t_ALH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tALH); 
		}else if((res_eql=strcmp(buf,"t_DS")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tDS); 
		}else if((res_eql=strcmp(buf,"t_DH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tDH); 
		}else if((res_eql=strcmp(buf,"t_WC")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tWC); 
		}else if((res_eql=strcmp(buf,"t_WH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tWH); 
		}else if((res_eql=strcmp(buf,"t_ADL")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tADL); 
		}else if((res_eql=strcmp(buf,"t_R")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tR); 
		}else if((res_eql=strcmp(buf,"t_AR")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tAR); 
		}else if((res_eql=strcmp(buf,"t_CLR")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCLR); 
		}else if((res_eql=strcmp(buf,"t_RR")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRR); 
		}else if((res_eql=strcmp(buf,"t_RP")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRP); 
		}else if((res_eql=strcmp(buf,"t_WB")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tWB); 
		}else if((res_eql=strcmp(buf,"t_RC")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRC); 
		}else if((res_eql=strcmp(buf,"t_REA")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tREA); 
		}else if((res_eql=strcmp(buf,"t_CEA")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCEA); 
		}else if((res_eql=strcmp(buf,"t_RHZ")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRHZ); 
		}else if((res_eql=strcmp(buf,"t_CHZ")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCHZ); 
		}else if((res_eql=strcmp(buf,"t_RHOH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRHOH); 
		}else if((res_eql=strcmp(buf,"t_RLOH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRLOH); 
		}else if((res_eql=strcmp(buf,"t_COH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tCOH); 
		}else if((res_eql=strcmp(buf,"t_REH")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tREH); 
		}else if((res_eql=strcmp(buf,"t_IR")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tIR); 
		}else if((res_eql=strcmp(buf,"t_RHW")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRHW); 
		}else if((res_eql=strcmp(buf,"t_WHR")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tWHR); 
		}else if((res_eql=strcmp(buf,"t_RST")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_characteristics.tRST); 
		}else if((res_eql=strcmp(buf,"erase limit")) ==0){
			sscanf(buf + next_eql,"%d",&p->ers_limit); 
		}else if((res_eql=strcmp(buf,"flash operating current")) ==0){
			sscanf(buf + next_eql,"%lf",&p->operating_current); 
		}else if((res_eql=strcmp(buf,"flash supply voltage")) ==0){
			sscanf(buf + next_eql,"%lf",&p->supply_voltage); 
		}else if((res_eql=strcmp(buf,"dram active current")) ==0){
			sscanf(buf + next_eql,"%lf",&p->dram_active_current); 
		}else if((res_eql=strcmp(buf,"dram standby current")) ==0){
			sscanf(buf + next_eql,"%lf",&p->dram_standby_current); 
		}else if((res_eql=strcmp(buf,"dram refresh current")) ==0){
			sscanf(buf + next_eql,"%lf",&p->dram_refresh_current); 
		}else if((res_eql=strcmp(buf,"dram voltage")) ==0){
			sscanf(buf + next_eql,"%lf",&p->dram_voltage); 
		}else if((res_eql=strcmp(buf,"address mapping")) ==0){
			sscanf(buf + next_eql,"%d",&p->address_mapping); 
		}else if((res_eql=strcmp(buf,"wear leveling")) ==0){
			sscanf(buf + next_eql,"%d",&p->wear_leveling); 
		}else if((res_eql=strcmp(buf,"gc")) ==0){
			sscanf(buf + next_eql,"%d",&p->gc); 
		}else if((res_eql=strcmp(buf,"clean in background")) ==0){
			sscanf(buf + next_eql,"%d",&p->clean_in_background); 
		}else if((res_eql=strcmp(buf,"overprovide")) ==0){
			sscanf(buf + next_eql,"%f",&p->overprovide); 
		}else if((res_eql=strcmp(buf,"gc threshold")) ==0){
			sscanf(buf + next_eql,"%f",&p->gc_threshold); 
		}else if((res_eql=strcmp(buf,"buffer management")) ==0){
			sscanf(buf + next_eql,"%d",&p->buffer_management); 
		}else if((res_eql=strcmp(buf,"scheduling algorithm")) ==0){
			sscanf(buf + next_eql,"%d",&p->scheduling_algorithm); 
		}else if((res_eql=strcmp(buf,"quick table radio")) ==0){
			sscanf(buf + next_eql,"%f",&p->quick_radio); 
		}else if((res_eql=strcmp(buf,"related mapping")) ==0){
			sscanf(buf + next_eql,"%d",&p->related_mapping); 
		}else if((res_eql=strcmp(buf,"striping")) ==0){
			sscanf(buf + next_eql,"%d",&p->striping); 
		}else if((res_eql=strcmp(buf,"interleaving")) ==0){
			sscanf(buf + next_eql,"%d",&p->interleaving); 
		}else if((res_eql=strcmp(buf,"pipelining")) ==0){
			sscanf(buf + next_eql,"%d",&p->pipelining); 
		}else if((res_eql=strcmp(buf,"time_step")) ==0){
			sscanf(buf + next_eql,"%d",&p->time_step); 
		}else if((res_eql=strcmp(buf,"small large write")) ==0){
			sscanf(buf + next_eql,"%d",&p->small_large_write); 
		}else if((res_eql=strcmp(buf,"active write threshold")) ==0){
			sscanf(buf + next_eql,"%d",&p->threshold_fixed_adjust); 
		}else if((res_eql=strcmp(buf,"threshold value")) ==0){
			sscanf(buf + next_eql,"%d",&p->threshold_value); 
		}else if((res_eql=strcmp(buf,"active write")) ==0){
			sscanf(buf + next_eql,"%d",&p->active_write); 
		}else if((res_eql=strcmp(buf,"gc hard threshold")) ==0){
			sscanf(buf + next_eql,"%f",&p->gc_hard_threshold); 
		}else if((res_eql=strcmp(buf,"allocation")) ==0){
			sscanf(buf + next_eql,"%d",&p->allocation_scheme); 
		}else if((res_eql=strcmp(buf,"static_allocation")) ==0){
			sscanf(buf + next_eql,"%d",&p->static_allocation); 
		}else if((res_eql=strcmp(buf,"dynamic_allocation")) ==0){
			sscanf(buf + next_eql,"%d",&p->dynamic_allocation); 
		}else if((res_eql=strcmp(buf,"advanced command")) ==0){
			sscanf(buf + next_eql,"%d",&p->advanced_commands); 
		}else if((res_eql=strcmp(buf,"advanced command priority")) ==0){
			sscanf(buf + next_eql,"%d",&p->ad_priority); 
		}else if((res_eql=strcmp(buf,"advanced command priority2")) ==0){
			sscanf(buf + next_eql,"%d",&p->ad_priority2); 
		}else if((res_eql=strcmp(buf,"greed CB command")) ==0){
			sscanf(buf + next_eql,"%d",&p->greed_CB_ad); 
		}else if((res_eql=strcmp(buf,"greed MPW command")) ==0){
			sscanf(buf + next_eql,"%d",&p->greed_MPW_ad); 
		}else if((res_eql=strcmp(buf,"aged")) ==0){
			sscanf(buf + next_eql,"%d",&p->aged); 
		}else if((res_eql=strcmp(buf,"aged ratio")) ==0){
			sscanf(buf + next_eql,"%f",&p->aged_ratio); 
		}else if((res_eql=strcmp(buf,"queue_length")) ==0){
			sscanf(buf + next_eql,"%d",&p->queue_length); 
		}else if((res_eql=strncmp(buf,"chip number",11)) ==0)
		{
			sscanf(buf+12,"%d",&i);
			sscanf(buf + next_eql,"%d",&p->chip_channel[i]); 
		}else{
			printf("don't match\t %s\n",buf);
		}
		
		memset(buf,0,BUFSIZE);
		
	}
	fclose(fp);
	fclose(fp1);
	fclose(fp2);

	return p;
}


