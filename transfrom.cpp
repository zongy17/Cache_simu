#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>

class CacheLine {
public:
    bool valid_bit = false;
    unsigned long long tag = 0;
    unsigned long long time = 0;
    CacheLine(bool vb, unsigned long long ta): valid_bit(vb), tag(ta) {}
    CacheLine(bool vb, unsigned long long ta, unsigned long long ti): valid_bit(vb), tag(ta), time(ti) {}
    bool operator == (CacheLine const & b) const {
        return valid_bit==b.valid_bit && tag==b.tag;//在set中进行比较的时候不需要考虑时间
    }
    bool operator < (CacheLine const & b) const {// set默认排序小的在前
        return time < b.time;//使用时间早的排在前，方便LRU策略替换
    }
};

// int main(int argc, char* argv[]) {

//     std::string input_filename_nopfx = std::string(argv[1]);

//     FILE * fp_src = fopen((input_filename_nopfx+".trace").c_str(), "r");
//     FILE * fp_new = fopen((input_filename_nopfx+"_new.trace").c_str(), "w+");

//     char i_type[10];
//     unsigned long long addr;
//     unsigned line = 0;
//     while (fscanf(fp_src, "%s %llx", i_type, &addr) != EOF){
//         if (i_type[0] == 'r')
//             i_type[0] = 'L';
//         else if (i_type[0] == 'w')
//             i_type[0] = 'S';
//         else {
//             printf(" Error at line %d: %s\n", line, i_type[0]);
//             exit(1);
//         }

//         fprintf(fp_new, " %c %llx,4\n", i_type[0], addr);
//         line++;
//     }

//     fclose(fp_src);
//     fclose(fp_new);

//     return 0;
// }

int main() 
{
    std::set<CacheLine> victim;
    victim.insert(CacheLine(true, 100, 0));
    victim.insert(CacheLine(true, 109, 4));
    victim.insert(CacheLine(true, 240, 5));
    victim.insert(CacheLine(true, 291, 9));
    victim.insert(CacheLine(true, 130, 0));
    victim.insert(CacheLine(true, 978, 10));

    if (victim.find(CacheLine(true, 240)) != victim.end()){
        CacheLine target = *victim.find(CacheLine(true, 240));
        printf(" tag: %llx, time: %llx\n", target.tag, target.time);
    }

    while (!victim.empty()) {
        printf(" tag: %llx, time: %llx\n", victim.begin()->tag, victim.begin()->time);
        victim.erase(victim.begin());
    }

    return 0;
}