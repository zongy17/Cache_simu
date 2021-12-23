#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <assert.h>

//打印帮助文档
void print_help() {
    printf("Usage: ./csim [-hv] -s <num> -E <num> -b <num> -t <file>\n");
    printf("Options:\n");
    printf("  -h         Print this help massage.\n");
    printf("  -v         Optional verbose flag.\n");
    printf("  -w <num>   Number of lines per set, i.e., number of ways.\n");
    printf("  -b <num>   Number of block offset bits.\n");
    printf("  -m <num>   Number of victim lines.\n");
    printf("  -t <file>  Trace file.\n");

    printf("\nExamples:\n");
    printf("  linux>  ./csim -w 1 -b 4 -m 0 -t traces/yi.trace\n");
    printf("  linux>  ./csim -v -w 2 -b 4 -m 512 -t traces/yi.trace\n");
}

class CacheLine {
public:
    bool valid_bit = false;
    unsigned long long tag = 0;
    unsigned long long time = 0;
    CacheLine(bool vb, unsigned long long ta): valid_bit(vb), tag(ta) {}
    CacheLine(bool vb, unsigned long long ta, unsigned long long ti): valid_bit(vb), tag(ta), time(ti) {}
};

// 注意存在全相联的victim cache里的CacheLine的tag是原来Cache中的tag和index拼接而成的
int find_CacheLine(std::vector<CacheLine> const & victims, \
                   unsigned long long tag_index, int & evict_victim_id) {
    unsigned long long cmp_time = ULONG_LONG_MAX;
    int hit_id = -1;
    for (int i = 0; i < victims.size(); i++){
        if (victims[i].valid_bit && victims[i].tag == tag_index) {
            assert(hit_id == -1);//认为victims里只能有一个tag跟目标相同的
            hit_id = i;
        } else {
            if (victims[i].time < cmp_time){//找到一个更早未用的
                cmp_time = victims[i].time;
                evict_victim_id = i;
            }
        }
    }
    return hit_id;
}

const unsigned cacheSize = 0x1 << 19;
// const unsigned cacheSize = 0x1 << 12;

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
    unsigned num_victims;//victim cache的数目
    FILE * fp_trace = NULL;
    int opt;

    //工作变量
    char i_type[10];
    unsigned long long addr, index, tag, time_stamp, cmp_time;//指令地址，set索引和标签tag，时间戳
    int avail_line_id, evic_line_id, hit_line_id;
    
    std::string input_filename_nopfx;
    //处理输入的参数，转化为Cache的状态量
    while (-1 != (opt = getopt(argc, argv, "hw:b:m:t:"))){
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
        case 'm':
            num_victims = atoi(optarg);
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
    for (unsigned i = 1; ; i*=2) {
        if (i==num_sets) break;
        num_set_bits++;
    }
    printf(" ---------- Cache System Params ---------- \n");
    printf(" Cache size: %u Bytes\n", cacheSize);
    printf(" Block size: %u Bytes\n", cacheLineSize);
    printf(" Num # sets: %u\n", num_sets);
    printf(" Num # ways: %u\n", num_ways);
    printf(" Victim Cache: %u Units\n", num_victims);
    printf(" ----------------------------------------- \n");
    printf(" Input file: %s\n", (input_filename_nopfx+".trace").c_str());

    //按需分配内存
    CacheLine ** Cache = (struct CacheLine **)malloc(num_sets*sizeof(struct CacheLine *));
    for (int i = 0; i < num_sets; i++)
        Cache[i] = (CacheLine *)malloc(num_ways*sizeof(struct CacheLine));
    //初始化Cache的所有valid bit和时间戳(置0)
    init_caches(Cache, num_sets, num_ways);
    // victim cache 使用的也是LRU替换策略
    std::vector<CacheLine> victim_Cache;

    //准备写出
    FILE * fp_result = fopen((input_filename_nopfx+"-B"+std::to_string(cacheLineSize)+"-w"+std::to_string(num_ways)+"-m"+std::to_string(num_victims)+".log").c_str(), "w+");
    // fprintf(fp_result, "I address tag index status\n");

    //开始模拟
    unsigned cnt_miss = 0, cnt_eviction = 0;
    unsigned cnt_first_hit = 0, cnt_nonFirst_hit = 0;
    unsigned cnt_lines = 0;
    memset(i_type, 0, sizeof(i_type));
    time_stamp = 0;
    while (fscanf(fp_trace, "%s %llx", i_type, &addr) != EOF){
        cnt_lines++;

        index = addr >> num_block_bits;//右移抹掉offset位
        index = index & ~(ULONG_LONG_MAX << num_set_bits);//抹掉更高位的tag值 -1? or ULONG_LONG_MAX
        tag   = addr >> (num_block_bits + num_set_bits);//右移抹掉offset位和index位

        // fprintf(fp_result, "%s %llx %llx %llx", i_type, addr, tag, index);

        CacheLine * cacheSet = Cache[index];
        //定位到set之后在set内部逐个遍历cache line
        cmp_time = ULONG_LONG_MAX;
        hit_line_id = -1;//-1表示没有命中，否则指向命中的位置
        avail_line_id = -1;//记录可用的空路，-1表示没有
        evic_line_id  = -1;//记录可替换的空路，-1表示不需要
        for (int i = 0; i < num_ways; i++){
            if (cacheSet[i].valid_bit == false){//该路无效，如有需要，可以准备放入
                avail_line_id = i;
            } else {//该路有效
                if (cacheSet[i].tag != tag){//但不是所需的目标，记录一下它的时间戳
                    if (cacheSet[i].time < cmp_time){//是当前找到的最久未用的
                        cmp_time = cacheSet[i].time;
                        evic_line_id = i;//则标记它为可替换的
                    }
                } else {//该路就是所需的目标
                    cnt_first_hit++;//首次命中：在Cache中命中的当作首次命中（直接映射）
                    // fprintf(fp_result, " first hit");
                    hit_line_id = i;
                    cacheSet[i].time = time_stamp;//更新时间戳
                    break;//结束对各路的搜索循环
                }
            }
        }// break跳出
        
        if (hit_line_id == -1) {//没有在Cache命中
            // 到victim_Cache中去查看
            int victim_id, evict_victim_id = -1;
            unsigned long long tag_index = (tag<<num_set_bits) + index;// 注意优先级！
            if ((victim_id = find_CacheLine(victim_Cache, tag_index, evict_victim_id)) != -1){// 在victim_Cache中找到了
                cnt_nonFirst_hit++;
                // fprintf(fp_result, " non-first hit");
                // 注意从全相联的victim_Cache还原回Cache时要截取一部分作为tag，并更新时间戳
                CacheLine target(true, victim_Cache[victim_id].tag >> num_set_bits, time_stamp);
                victim_Cache.erase(victim_Cache.begin() + victim_id);// 要从victim_cache中替换回Cache中
                
                assert(avail_line_id != -1 || evic_line_id != -1);
                if (avail_line_id != -1) {//此时该set还有空行，不需要从Cache中evic
                    cacheSet[avail_line_id] = target;
                } else {// 没有空行，将从Cache替换下来的放入victim_Cache
                    CacheLine cacheLine_putin(true, (cacheSet[evic_line_id].tag<<num_set_bits) + index,\
                                                 cacheSet[evic_line_id].time);
                    victim_Cache.push_back(cacheLine_putin);
                    assert(victim_Cache.size() <= num_victims);
                    cacheSet[evic_line_id] = target;//目标数据回归Cache
                }
            } else {// 在victim_Cache中也没有找到，只能从主存上加载并一步加载到Cache中
                cnt_miss++;
                fprintf(fp_result, "%s 0x%lld miss\n", i_type, addr);

                assert(avail_line_id != -1 || evic_line_id != -1);
                if (avail_line_id != -1) {
                    cacheSet[avail_line_id] = CacheLine(true, tag, time_stamp);
                } else {// 没有空行，只能替换，被替换下来的先在victim_Cache中寻求机会
                    CacheLine cacheLine_putin(true, (cacheSet[evic_line_id].tag<<num_set_bits) + index,\
                                                 cacheSet[evic_line_id].time);
                    if (victim_Cache.size() < num_victims) {//victim_Cache是否还有容量
                        victim_Cache.push_back(cacheLine_putin);//放入victim_Cache
                    } else {// 容量不足，从victim_Cache中驱逐出一个最久未用的
                        cnt_eviction++;
                        // fprintf(fp_result, " eviction");
                        if (num_victims > 0) {// 得确保victim_Cache真的存在才行
                            assert(evict_victim_id != -1);
                            victim_Cache[evict_victim_id] = CacheLine(true, \
                                (cacheSet[evic_line_id].tag<<num_set_bits) + index, cacheSet[evic_line_id].time);
                        }
                    }
                    cacheSet[evic_line_id] = CacheLine(true, tag, time_stamp);
                }
            }
        }


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
    return 0;
}
