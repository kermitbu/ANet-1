#ifndef ANET_AEBUFFER_H
#define ANET_AEBUFFER_H

#include <cstring>
#include <stdint.h>

// typedef struct {
//     uint8_t *buff;
//     size_t size;
//     size_t read_idx;
//     size_t write_idx;
// } buffer_t;

// buffer_t *alloc_buffer();

// void free_buffer(buffer_t *buffer);

// void check_buffer_size(buffer_t *buffer, size_t avlid_size);

// size_t get_readable_size(buffer_t *buffer);

// size_t get_writeable_size(buffer_t *buffer);

#define DEFAULT_BUFF_SIZE 4096
class buffer_t final {
public:
    buffer_t()
    {
        buff = new uint8_t[DEFAULT_BUFF_SIZE];
        size = DEFAULT_BUFF_SIZE;
        read_idx = 0;
        write_idx = 0;
    }

    ~buffer_t()
    {
        delete[] buff;
        buff = nullptr;
        read_idx = 0;
        write_idx = 0;
    }

    void check_buffer_size(size_t avlid_size)
    {
        if (read_idx > DEFAULT_BUFF_SIZE) {
            size_t data_len = get_readable_size();
            memmove(buff, buff + read_idx, data_len);
            read_idx = 0;
            write_idx = data_len;
        }
        if (get_writeable_size() < avlid_size) {
            size_t new_size = size + avlid_size;     
            auto new_buff = new uint8_t[new_size];
            memcpy(new_buff, buff, size);
            delete []buff;
            buff = new_buff;
            size = new_size;
        }
    }

    size_t get_readable_size()
    {
        return write_idx - read_idx;
    }

    size_t get_writeable_size()
    {
        return size - write_idx;
    }

    uint8_t* buff;
    size_t size;
    size_t read_idx;
    size_t write_idx;
};


#endif //ANET_AEBUFFER_H
