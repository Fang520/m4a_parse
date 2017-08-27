#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define BUF_LEN 10240000
#define MAX_SAMPLE_NUM 102400
#define MAX_CHUNK_NUM 10240
#define MAX_STSC_NUM 1024

typedef struct {
    int first_trunk;
    int samples_per_trunk;
    int sample_description_id;
} stsc_t;

typedef void (*sample_cb_t)(int offset, int len, FILE* fp);

char filename[64];
char m4a_buf[BUF_LEN];
int m4a_len;
int sample_freq_index;
int sample_channel;
int sample_object_type;
unsigned char asc_buf[64]; // AudioSpecificConfig
int asc_len;

stsc_t stsc_tab[MAX_STSC_NUM];
int stsc_num;
int chunk_offset_tab[MAX_CHUNK_NUM];
int chunk_num;
int sample_size_tab[MAX_SAMPLE_NUM];
int sample_num;

void get_adts_head(char head[7], int len)
{
    head[0]  = 0xff;
    head[1]  = 0xf1;
    head[2]  = (unsigned char)((sample_object_type-1)<<6) | (unsigned char)(sample_freq_index<<2);
    head[3]  = (unsigned char)(sample_channel<<6) | (unsigned char)(((len+7)>>11)&0x3); 
    head[4]  = (unsigned char)(((len+7)>>3)&0xff);
    head[5]  = 0x1f | (unsigned char)(((len+7)<<5)&0xe0);
    head[6]  = 0xfc;
}

void parse_stsc()
{
    char* pos = memmem(m4a_buf, m4a_len, "stsc", 4);
    int i;
    if (pos)
    {
        pos += 8; //number of entry
        stsc_num = htonl(*(int*)pos);
        for (i=0; i<stsc_num; i++)
        {
            pos += 4; //first chunk
            stsc_tab[i].first_trunk = htonl(*(int*)pos);
            pos += 4; //samples per chunk 
            stsc_tab[i].samples_per_trunk = htonl(*(int*)pos);
            pos += 4; //sample description ID
            stsc_tab[i].sample_description_id = htonl(*(int*)pos);
        }
        printf("stsc table len: %d\n", stsc_num);
    }
}

void parse_stco()
{
    char* pos = memmem(m4a_buf, m4a_len, "stco", 4);
    int i;
    if (pos)
    {
        pos += 8; //number of entry
        chunk_num = htonl(*(int*)pos);
        for (i=0; i<chunk_num; i++)
        {
            pos += 4; //chunk offset
            chunk_offset_tab[i] = htonl(*(int*)pos);
        }
        printf("chunk num: %d\n", chunk_num);
    }
}

void check_m4a_file()
{
    char* pos = memmem(m4a_buf, m4a_len, "stsd", 4);
    if (pos)
    {
        pos += 16;
        if (memcmp(pos, "mp4a", 4) != 0)
        {
            printf("only support m4a file, not found mp4a type\n");
            exit(1);
        }
    }
    else
    {
        printf("only support m4a file, not found stsd box\n");
        exit(1);
    }
}

// this field is variable length, param vl return the real length 
int calc_desc_len(char* pos, int* vl)
{
    int len = 0;
    int i;
    for (i=0; i<4; i++)
    { 
        int c = pos[i];
        len = (len << 7) | (c & 0x7f);
        if (!(c & 0x80))
            break;
    }
    *vl = i + 1;
    return len;    
}

void parse_esds()
{
    char* pos = memmem(m4a_buf, m4a_len, "esds", 4);
    if (pos)
    {
        int len, vl;
        pos += 8; // MP4ESDescrTag
        if (*pos != 0x03)
        {
            printf("not found MP4ESDescrTag\n");
            exit(1);
        }
        pos++;
        len = calc_desc_len(pos, &vl);
        pos += (vl + 3); // MP4DecConfigDescrTag
        if (*pos != 0x04)
        {   
            printf("not found MP4DecConfigDescrTag\n");
            exit(1);
        }
        pos++;
        len = calc_desc_len(pos, &vl);
        pos += (vl + 13); // MP4DecSpecificDescrTag
        if (*pos != 0x05)
        {
            printf("not found MP4DecSpecificDescrTag\n");
            exit(1);
        }
        pos++;
        len = calc_desc_len(pos, &vl);
        pos += vl;
        memcpy(asc_buf, pos, len);
        asc_len = len;
        sample_object_type = (asc_buf[0] & 0xf8) >> 3;
        sample_freq_index = ((asc_buf[0] & 0x7) << 1) | (asc_buf[1] >> 7);
        sample_channel = (asc_buf[1] >> 3) & 0x0f;
        printf("sample_object_type: %d\n", sample_object_type);
        printf("sample_freq_index: %d\n", sample_freq_index);
        printf("sample_channel: %d\n", sample_channel);
    }
}

void parse_stsz()
{
    char* pos = memmem(m4a_buf, m4a_len, "stsz", 4);
    int sample_size = 0;
    int table_size = 0;
    int i;
    if (pos)
    {
        pos += 8; //sample size
        sample_size = htonl(*(int*)pos);
        printf("sample size: %d\n", sample_size);
        if (sample_size == 0)
        {
            pos += 4; //table size
            table_size = htonl(*(int*)pos);
            printf("table size: %d\n", table_size);
            for (i=0; i<table_size; i++)
            {
                pos += 4; //each sample size
                sample_size_tab[i] = htonl(*(int*)pos);
                //printf("sample %d size: %d\n", i, sample_size_tab[i]);
            }
            sample_num = i;
        }
    }
}

void load_m4a_file(char* name)
{
    FILE* fp = fopen(name, "rb");
    if (fp)
    {
        m4a_len = fread(m4a_buf, 1, BUF_LEN, fp);
        fclose(fp);
    }
    strncpy(filename, name, strlen(name) - 4);
}

void iterate_samples_from_stsc(sample_cb_t sample_cb, FILE* fp)
{
    int chunk_index = 0;
    int sample_index = 0;
    int i, j, k;

    for (i=0; i<stsc_num; i++) // stsc
    {
        int n_chunk;
        int n_sample;
        if (i == stsc_num - 1) // last stsc
            n_chunk = chunk_num - stsc_tab[i].first_trunk + 1;
        else
            n_chunk = stsc_tab[i+1].first_trunk - stsc_tab[i].first_trunk;
        n_sample = stsc_tab[i].samples_per_trunk;
        for (j=0; j<n_chunk; j++) // chunk
        {
            int chunk_offset = chunk_offset_tab[chunk_index++];
            int sample_size_total = 0;
            for (k=0; k<n_sample; k++) // sample
            {
                int size = sample_size_tab[sample_index];
                sample_cb(chunk_offset + sample_size_total, size, fp);
                sample_size_total += size;
                sample_index++;
            }
        }
    }
}

void es_cb(int offset, int len, FILE* fp)
{
    int ret = fwrite(m4a_buf + offset, 1, len, fp);
    if (ret != len)
    {
        printf("save es error\n");
    }
}

void copy_raw_to_es()
{
    char name[64];
    sprintf(name, "%s.es", filename);
    FILE* fp = fopen(name, "wb");
    if (!fp)
        return;
    iterate_samples_from_stsc(es_cb, fp);
    fclose(fp);
    printf("save es successful\n");
}

void adts_cb(int offset, int len, FILE* fp)
{
    char head[7];
    int ret;

    get_adts_head(head, len);
    ret = fwrite(head, 1, 7, fp);
    if (ret != 7)
    {
        printf("save adts head error\n");
    }    
    ret = fwrite(m4a_buf + offset, 1, len, fp);
    if (ret != len)
    {
        printf("save adts error\n");
    }
}

void copy_raw_to_adts()
{
    char name[64];
    sprintf(name, "%s.adts", filename);
    FILE* fp = fopen(name, "wb");
    if (!fp)
        return;
    iterate_samples_from_stsc(adts_cb, fp);
    fclose(fp);
    printf("save adts successful\n");
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("usage: test <file>\n");
        exit(1);
    }
    
    load_m4a_file(argv[1]);
    check_m4a_file();
    parse_esds();
    parse_stsc();
    parse_stsz();
    parse_stco();
    copy_raw_to_es();
    copy_raw_to_adts();
    return 0;
}    
