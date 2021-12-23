#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <unordered_set>
#include <cassert>
#include <algorithm>

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
// 保证输入的参数a是2的幂次
unsigned log2(unsigned a) {
    unsigned ans = 0;
    while (a > 1) {
        ans = ans + 1;
        a = a >> 1;
    }
    return ans;
}

class BitVector {
private:
    unsigned vec;
public:
    BitVector(): vec(0) {}//默认构造函数
    unsigned data() {return vec;}
    bool check_bit(int wayId) {
        assert(wayId >= 0);
        unsigned wayBit = 1 << wayId;
        return wayBit == (wayBit & vec); 
    }
    void active_bit(int wayId) {
        assert(wayId >= 0);
        unsigned wayBit = 1 << wayId;
        vec = vec | wayBit;
    }
    void clear_bit(int wayId) {
        assert(wayId >= 0);
        unsigned wayBit = 1 << wayId;
        vec = vec & (~wayBit);//wayBit取反后再做与操作
    }
    void reset() {vec = 0;}
};

// return the major location of a cached data (tag & major_mask)
unsigned ML(unsigned long long const tag, unsigned const major_mask) {
    return (tag & major_mask);
}

class CacheLine {
public:
    bool valid_bit;
    unsigned long long tag;
    unsigned long long time;
    CacheLine(): valid_bit(false), tag(0), time(0) {}//默认构造函数
    void copy_from(CacheLine b) {
        valid_bit = b.valid_bit;
        tag = b.tag;
        time = b.time;
    }
    void load(unsigned long long ta, unsigned long long ti) {
        valid_bit = true;
        tag = ta; time = ti;
    }
    // return if the cached data is at its major location
    bool at_my_majorLoc(int curr_wayId, unsigned const major_mask) {
        assert(curr_wayId >= 0);//现在在的位置
        return curr_wayId == ML(tag, major_mask);
    }
};

void swap(CacheLine & a, CacheLine & b) {
    bool tmp_valid_bit = a.valid_bit;
    a.valid_bit = b.valid_bit;
    b.valid_bit = tmp_valid_bit;
    unsigned long long tmp_tag = a.tag;
    a.tag = b.tag;
    b.tag = tmp_tag;
    unsigned long long tmp_time = a.time;
    a.time = b.time;
    b.time = tmp_time;
}

const unsigned cacheSize = 0x1 << 19;

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
    unsigned major_mask = ~(ULONG_LONG_MAX << log2(num_ways));// 计算主位置的掩码，用tag的末尾log2(num_ways)位
    printf(" ---------- Cache System Params ---------- \n");
    printf(" Cache size: %u Bytes\n", cacheSize);
    printf(" Block size: %u Bytes\n", cacheLineSize);
    printf(" Num # sets: %u\n", num_sets);
    printf(" Num # ways: %u\n", num_ways);
    printf(" Method: Multi Colum\n");
    printf(" ----------------------------------------- \n");
    printf(" Input file: %s\n", (input_filename_nopfx+".trace").c_str());

    //按需分配内存
    std::vector<std::vector<CacheLine> > Cache(num_sets, std::vector<CacheLine>(num_ways));
    std::vector<std::vector<BitVector> > BitMap(num_sets, std::vector<BitVector>(num_ways));

    //准备写出
    FILE * fp_result = fopen((input_filename_nopfx+"-B"+std::to_string(cacheLineSize)+"-w"+std::to_string(num_ways)+".log").c_str(), "w+");
    // fprintf(fp_result, "I address tag index status\n");

    //开始模拟
    unsigned cnt_miss = 0, cnt_eviction = 0;
    unsigned cnt_first_hit = 0, cnt_nonFirst_hit = 0;
    unsigned cnt_lines = 0;
    memset(i_type, 0, sizeof(i_type));
    time_stamp = 0;
    // std::unordered_set<unsigned> index_set;
    unsigned long long avg_search_length = 0, cnt_search = 0;
    while (fscanf(fp_trace, "%s %llx", i_type, &addr) != EOF){
        cnt_lines++;

        index = addr >> num_block_bits;//右移抹掉offset位
        index = index & ~(ULONG_LONG_MAX << num_set_bits);//抹掉更高位的tag值 -1? or ULONG_LONG_MAX
        tag   = addr >> (num_block_bits + num_set_bits);//右移抹掉offset位和index位

        // printf("%s %llx %llx %llx", i_type, addr, tag, index);
        // if (index_set.find(index) != index_set.end())
        //     printf(" reappear index: %llx\n", index);
        // else 
        //     index_set.insert(index);

        // fprintf(fp_result, "%s %llx %llx %llx", i_type, addr, tag, index);

        std::vector<CacheLine> & cacheSet = Cache[index];
        std::vector<BitVector> & bitvecSet = BitMap[index];
        //定位到set之后在set内部逐个遍历cache line
        cmp_time = ULONG_LONG_MAX;
        hit_line_id = -1;//-1表示没有命中，否则指向命中的位置
        avail_line_id = -1;//记录可用的空路，-1表示没有
        evic_line_id  = -1;//记录可替换的空路，-1表示不需要

        // 先找MRU_location
        int A_ML = ML(tag, major_mask);//记新来的数据块位为A，其主位置为A_ML
        
        if (cacheSet[A_ML].tag == false) {//自己的主位置无效
            // 说明此前也从没有访问到过这，因此也不用找bitVector去找备选位置了
            assert(bitvecSet[A_ML].data() == 0);
            cacheSet[A_ML].load(tag, time_stamp);//而直接加载需要的数据并放在这
            // bitVector不用改动，仍为0
            cnt_miss++;
            fprintf(fp_result, "%s 0x%llx miss\n", i_type, addr);
            // printf(" cold miss");
        } else {// 自己的主位置有效
            if (cacheSet[A_ML].tag == tag) {// 命中
                cnt_first_hit++;
                cacheSet[A_ML].time = time_stamp;//更新时间戳
                // fprintf(fp_result, " first hit");
                // printf(" first hit");
            } else {// 直接映射没有命中
                // 记当前占据A_ML的数据块为B，B的主位置为B_ML
                int B_ML = ML(cacheSet[A_ML].tag, major_mask);

                unsigned search_length = 0;
                cnt_search++;// 这一轮对bit vector的搜索长度
                for (int w = 0; w < num_ways; w++) {// 依次找备选位置
                    if (w != A_ML && bitvecSet[A_ML].check_bit(w)) {//在第w路有备选
                        search_length++;
                        assert(cacheSet[w].valid_bit == true);//该备选路必为有效
                        if (cacheSet[w].tag == tag){// 在该备选路w找到了所需的tag
                            hit_line_id = w;
                            cnt_nonFirst_hit++;
                            // fprintf(fp_result, " non-first hit");
                            // printf(" non-first hit");
                            swap(cacheSet[hit_line_id], cacheSet[A_ML]);//swap(A, B)
                            cacheSet[A_ML].time = time_stamp;//更新刚刚被访问的A的时间戳！！！
                            if (B_ML == A_ML) {// AB的主位置相同
                                ;// do nothing
                            } else {// 此时在A_ML上的B处于备选位置，要修改B主位置B_ML上的bitVecotr
                                bitvecSet[B_ML].clear_bit(A_ML);//清空A_ML的位
                                bitvecSet[B_ML].active_bit(hit_line_id);//并指向B挪到的新位置
                            }
                            break;//跳出找备选位置的for循环
                        }
                    }
                }// break跳出
                
                avg_search_length += search_length;

                if (hit_line_id == -1) {// 都没有找到需要的，则从主存上调
                    cnt_miss++;
                    fprintf(fp_result, "%s 0x%llx miss\n", i_type, addr);
                    // printf(" miss");
                    for (int w = 0; w < num_ways; w++) {// 找可以替换或者需要被evict的行
                        if (cacheSet[w].valid_bit == false)
                            avail_line_id = w;
                        else if (cacheSet[w].time < cmp_time) {//是当前找到的最久未用的
                            cmp_time = cacheSet[w].time;
                            evic_line_id = w;//则标记它为可替换的
                        }
                    }
                    assert(avail_line_id != -1 || evic_line_id != -1);
                    if (avail_line_id != -1) {// 有空行的
                        assert(avail_line_id != A_ML);
                        cacheSet[avail_line_id].copy_from(cacheSet[A_ML]);// B移到空行
                        if (B_ML == A_ML) {// AB主位置相同，但是备选位置里没有A，所以这次要加上
                            //使B_ML的bitvector能追踪到它的新位置
                            bitvecSet[B_ML].active_bit(avail_line_id);
                        } else {// 要修改B主位置B_ML上的bitVecotr
                            bitvecSet[B_ML].clear_bit(A_ML);
                            bitvecSet[B_ML].active_bit(avail_line_id);
                        }
                        cacheSet[A_ML].load(tag, time_stamp);
                    } else {// 无空行，驱逐最久未用行
                        cnt_eviction++;
                        // fprintf(fp_result, " eviction");
                        // printf(" eviction");
                        // 当前占据最久未用行的数据块记为C，它的主位置为C_ML
                        int C_ML = ML(cacheSet[evic_line_id].tag, major_mask);

                        cacheSet[evic_line_id].copy_from(cacheSet[A_ML]);// B移到被驱逐的行
                        if (C_ML == evic_line_id) {// 被驱逐的行本来就是C的主位置
                            // 不需修改C_ML的bitVector
                            // 修改B_ML的bitVector指向新的位置evict_line_id
                            bitvecSet[B_ML].clear_bit(A_ML);
                            bitvecSet[B_ML].active_bit(evic_line_id);
                        } else {// 被驱逐的行是C的备选位置
                            // 则要修改C_ML的bitVector，指向evict_line_id的位失效
                            bitvecSet[C_ML].clear_bit(evic_line_id);
                            // 修改B_ML的bitVector指向新的位置evict_line_id
                            bitvecSet[B_ML].clear_bit(A_ML);
                            bitvecSet[B_ML].active_bit(evic_line_id);
                        }
                        cacheSet[A_ML].load(tag, time_stamp);
                    }
                }
    
            }

        }
       
        time_stamp++;//时间戳+1
        // fprintf(fp_result, "\n");
        // printf("\n");
    }
    
    fclose(fp_trace);
    fclose(fp_result);

    printf(" records: %u\n", cnt_lines);
    printf(" first hit: %u, first hit rate: %f\n", cnt_first_hit, (float)cnt_first_hit/(float)cnt_lines);
    printf(" non-first hit: %u, non-first hit rate: %f\n", cnt_nonFirst_hit, (float)cnt_nonFirst_hit/(float)cnt_lines);
    printf(" total hit: %u, total hit rate: %f\n", cnt_first_hit+cnt_nonFirst_hit, (float)(cnt_first_hit+cnt_nonFirst_hit)/(float)cnt_lines);
    printf(" miss: %u, miss rate: %f\n", cnt_miss, (float)cnt_miss/(float)cnt_lines);
    printf(" avg_search_length: %f\n", (float)avg_search_length/(float)cnt_search);
    
    return 0;
}