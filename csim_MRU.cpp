#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <iostream>

//打印帮助文档
void print_help() {
    printf("Usage: ./csim [-hv] -s <num> -E <num> -b <num> -t <file>\n");
    printf("Options:\n");
    printf("  -h         Print this help massage.\n");
    printf("  -v         Optional verbose flag.\n");
    printf("  -w <num>   Number of lines per set, i.e., number of ways.\n");
    printf("  -b <num>   Number of block offset bits.\n");
    printf("  -t <file>  Trace file.\n");

    printf("\nExamples:\n");
    printf("  linux>  ./csim -w 1 -b 4 -t traces/yi.trace\n");
    printf("  linux>  ./csim -v -w 2 -b 4 -t traces/yi.trace\n");
}

struct CacheLine {
    bool valid_bit = false;
    unsigned long long tag = 0;
    unsigned long long time = 0;
};

const unsigned cacheSize = 0x1 << 19;

//初始化分配内存
void init_caches(struct CacheLine ** Cache, int const & num_sets, int const & num_ways) {
    int i, j;
    for (i = 0; i < num_sets; i++)// 对于所有set
        for (j = 0; j < num_ways; j++){// 一个set内的所有路
            Cache[i][j].valid_bit = false;
            Cache[i][j].time      = 0x0;
            Cache[i][j].tag       = 0x0;
        }
}

int main(int argc, char* argv[]) {
    // Cache状态量
    // set index的位数，block offset的位数(决定块大小)，每个set内有多少行(亦即路数)
    unsigned num_set_bits, num_block_bits, num_ways;//s, b, E
    unsigned num_sets;// set的数目
    FILE * fp_trace = NULL;
    int opt;

    //工作变量
    char i_type[10];
    unsigned long long addr, index, tag, time_stamp, cmp_time;//指令地址，set索引和标签tag，时间戳
    int avail_line_id, evic_line_id, hit_line_id;
    
    std::string input_filename_nopfx;
    //处理输入的参数，转化为Cache的状态量
    while (-1 != (opt = getopt(argc, argv, "hw:b:t:"))){
        switch (opt) {
        case 'h':
            print_help();
            break;
        case 'w':
            num_ways = atoi(optarg);
            break;
        case 'b':
            num_block_bits = atoi(optarg);
            break;
        case 't':
            input_filename_nopfx = std::string(optarg);
            fp_trace = fopen((input_filename_nopfx+".trace").c_str(), "r");
            break;
        default:
            print_help();
            exit(EXIT_FAILURE);
            break;
        }
    }
    if (fp_trace == NULL) {//错误的文件指针
        printf("Unable to open file.\n");
        exit(EXIT_FAILURE);
    }

    // 计算Cache系统的参数
    unsigned cacheLineSize = 0x1 << num_block_bits;// 一个cache line的大小
    num_sets = cacheSize / cacheLineSize / num_ways;// 一共有多少个set
    num_set_bits = 0;
    for (unsigned i = 1; ; i*=2){
        if (i==num_sets) break;
        num_set_bits++;
    }
    printf(" ---------- Cache System Params ---------- \n");
    printf(" Cache size: %u Bytes\n", cacheSize);
    printf(" Block size: %u Bytes\n", cacheLineSize);
    printf(" Num # sets: %u\n", num_sets);
    printf(" Num # ways: %u\n", num_ways);
    printf(" Method: M R U\n");
    printf(" ----------------------------------------- \n");
    printf(" Input file: %s\n", (input_filename_nopfx+".trace").c_str());

    //按需分配内存
    struct CacheLine ** Cache = (struct CacheLine **)malloc(num_sets*sizeof(struct CacheLine *));
    for (unsigned i = 0; i < num_sets; i++)
        Cache[i] = (struct CacheLine *)malloc(num_ways*sizeof(struct CacheLine));
    //初始化Cache的所有valid bit和时间戳(置0)
    init_caches(Cache, num_sets, num_ways);
    //记录MRU位置，每个set一个
    int * MRU_location = (int *)malloc(num_sets*sizeof(int)); 
    for (unsigned i = 0; i < num_sets; i++)
        MRU_location[i] = -1;//表示尚无MRU

    //准备写出
    FILE * fp_result = fopen((input_filename_nopfx+"-B"+std::to_string(cacheLineSize)+"-w"+std::to_string(num_ways)+".log").c_str(), "w+");
    // fprintf(fp_result, "I address tag index status\n");

    //开始模拟
    unsigned cnt_miss = 0, cnt_eviction = 0;
    unsigned cnt_first_hit = 0, cnt_nonFirst_hit = 0;
    unsigned cnt_lines = 0;
    memset(i_type, 0, sizeof(i_type));
    time_stamp = 0;
    while (fscanf(fp_trace, "%s %llx", i_type, &addr) != EOF){// FIX ME: llx rather than lx
        cnt_lines++;

        index = addr >> num_block_bits;//右移抹掉offset位
        index = index & ~(ULONG_LONG_MAX << num_set_bits);//抹掉更高位的tag值 -1? or ULONG_LONG_MAX
        tag   = addr >> (num_block_bits + num_set_bits);//右移抹掉offset位和index位

        CacheLine * cacheSet = Cache[index];
        //定位到set之后在set内部逐个遍历cache line
        cmp_time = ULONG_LONG_MAX;
        hit_line_id = -1;//-1表示没有命中，否则指向命中的位置
        avail_line_id = -1;//记录可用的空路，-1表示没有
        evic_line_id  = -1;//记录可替换的空路，-1表示不需要
        // 先找MRU_location
        int MRU_line_id = MRU_location[index];
        if (MRU_line_id != -1) {// 是有效的MRU_location: 该set已经有东西被使用过了
            CacheLine & MRU_Line = cacheSet[MRU_line_id];
            if (MRU_Line.valid_bit == false){//该路无效，如有需要，可以准备放入
                avail_line_id = MRU_line_id;
            } else {//该路有效
                if (MRU_Line.tag != tag){//但不是所需的目标，记录一下它的时间戳
                    if (MRU_Line.time < cmp_time){//是当前找到的最久未用的
                        cmp_time = MRU_Line.time;
                        evic_line_id = MRU_line_id;//则标记它为可替换的
                    }
                } else {//该路就是所需的目标
                    cnt_first_hit++;
                    hit_line_id = MRU_line_id;//此时不用更新所存的MRU_location[index]
                    MRU_Line.time = time_stamp;//更新时间戳
                }
            }
        } 
        if (hit_line_id == -1) {// 有两种情况会进入对其他路的搜寻
            // Case 1: MRU_line_id==-1
            // Case 2: 搜寻MRU位置得到的数据不是想要的（此时应有avail_line_id和evic_line_id中一个为-1一个有效
            for (int i = 0; i < num_ways; i++){
                if (i == MRU_line_id) continue;// MRU_line_id==-1或者已经搜索过MRU_line_id

                if (cacheSet[i].valid_bit == false){//该路无效，如有需要，可以准备放入
                    avail_line_id = i;
                } else {//该路有效
                    if (cacheSet[i].tag != tag){//但不是所需的目标，记录一下它的时间戳
                        if (cacheSet[i].time < cmp_time){//是当前找到的最久未用的
                            cmp_time = cacheSet[i].time;
                            evic_line_id = i;//则标记它为可替换的
                        }
                    } else {//该路就是所需的目标
                        hit_line_id = i;
                        cacheSet[i].time = time_stamp;//更新时间戳
                        cnt_nonFirst_hit++;
                        MRU_location[index] = hit_line_id;//更新MRU位置为刚刚访问的
                        break;//结束对各路的搜索循环
                    }
                }
            }
        }
        // 对其他路的搜寻结束
        if (hit_line_id == -1){//没有命中，则需要替换进来
            cnt_miss++;
            fprintf(fp_result, "%s 0x%llx miss\n", i_type, addr);
            if (avail_line_id != -1) {//此时该set还有空行，不需要evic
                cacheSet[avail_line_id].valid_bit = true;
                cacheSet[avail_line_id].time = time_stamp;
                cacheSet[avail_line_id].tag = tag;
                MRU_location[index] = avail_line_id;//更新MRU位置为刚刚访问的
            } else {//已经没有空行了，那就只好evic
                cnt_eviction++;
                cacheSet[evic_line_id].valid_bit = true;
                cacheSet[evic_line_id].time = time_stamp;
                cacheSet[evic_line_id].tag = tag;
                MRU_location[index] = evic_line_id;//更新MRU位置为刚刚访问的
            }
        }
       
        // fprintf(fp_result, "%s %llx %llx %llx", i_type, addr, tag, index);
        // if (hit_line_id != -1)
        //     if (MRU_location[index] == MRU_line_id)//仍然是原来的MRU位置
        //         fprintf(fp_result, " first hit");
        //     else
        //         fprintf(fp_result, " non-first hit");
        // else{
        //     fprintf(fp_result, " miss");
        //     if (avail_line_id == -1)
        //         fprintf(fp_result, " eviction");
        // }

        // fprintf(fp_result, "\n");

        time_stamp++;//时间戳+1
    }
    
    fclose(fp_trace);
    fclose(fp_result);

    printf(" records: %u\n", cnt_lines);
    printf(" first hit: %u, first hit rate: %f\n", cnt_first_hit, (float)cnt_first_hit/(float)cnt_lines);
    printf(" non-first hit: %u, non-first hit rate: %f\n", cnt_nonFirst_hit, (float)cnt_nonFirst_hit/(float)cnt_lines);
    printf(" total hit: %u, total hit rate: %f\n", cnt_first_hit+cnt_nonFirst_hit, (float)(cnt_first_hit+cnt_nonFirst_hit)/(float)cnt_lines);
    printf(" miss: %u, miss rate: %f\n", cnt_miss, (float)cnt_miss/(float)cnt_lines);
    
    //释放内存
    for (int i = 0; i < num_sets; i++)
        free(Cache[i]);
    free(Cache);
    free(MRU_location);
    return 0;
}
