#include <cstdlib>
#include <cstdio>
#include <string>
#include <iostream>

#define MAX_BIT 32

unsigned cnt_0[MAX_BIT];
unsigned cnt_1[MAX_BIT];
unsigned long long mask[MAX_BIT];
float avg[MAX_BIT];
float var[MAX_BIT];

void init() {
    for (unsigned i = 0; i < MAX_BIT; i++){
        cnt_0[i] = 0;
        cnt_1[i] = 0;
        avg[i] = 0.0;
        // var[i] = 0.0;
    }
    mask[0] = 0x1;
    for (unsigned i = 1; i < MAX_BIT; i++)
        mask[i] = mask[i-1] << 1;
    for (unsigned i = 0; i < MAX_BIT; i++)
        printf("mask[%d]: %llx\n", i, mask[i]);
}

void convert(unsigned long long addr) {
    for (unsigned i = 0; i < MAX_BIT; i++) {
        if ((addr & mask[i]) == mask[i]) 
            cnt_1[i] += 1;
        else
            cnt_0[i] += 1;
    }
}

int main(int argc, char * argv[]) 
{
    std::string filename_nopfx = argv[1];
    FILE * f_src = fopen((filename_nopfx+".trace").c_str(), "r");

    init();
    char i_type[10];
    unsigned long long addr;
    unsigned cnt = 0;
    while (fscanf(f_src, "%s %llx", i_type, &addr) != EOF){
        convert(addr);
        cnt++;
    }

    for (unsigned i = MAX_BIT-1; i >= 0; i--){
        avg[i] = (float)cnt_1[i] / (float)cnt;
        printf("%f ", avg[i]);
    }
    printf("\n");

    fclose(f_src);
    return 0;
}