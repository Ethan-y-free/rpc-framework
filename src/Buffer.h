#pragma once

#include <cstddef>
#include <string>
#include <sys/types.h>   // ssize_t
#include <vector>

// ---- Buffer：muduo 式环形缓冲 ----
//
//  | prependable |  readable(readerIndex_→writerIndex_)  |  writable(writerIndex_→end)  |
//
// retrieve 系列只移动下标、不搬运数据；append 不够才 makeSpace 扩容/compact。
// prependable 区留给后续 RPC 协议「定长头 + 变长体」在数据前面补头部字段用。
class Buffer
{
public:
    static const size_t kCheapPrepend = 8;    // 预留前置区，头部前插免搬运
    static const size_t kInitialSize = 1024;  // 初始容量

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize),
          readerIndex_(kCheapPrepend),
          writerIndex_(kCheapPrepend)
    {
    }

    size_t readableBytes() const { return writerIndex_ - readerIndex_; }
    size_t writableBytes() const { return buffer_.size() - writerIndex_; }
    size_t prependableBytes() const { return readerIndex_; }

    const char* peek() const { return begin() + readerIndex_; }   // 读起点（不消费）

    void retrieve(size_t len);          // 消费掉 len 字节（纯指针移动）
    void retrieveAll();                 // 全部消费，下标归位
    std::string retrieveAsString(size_t len);
    std::string retrieveAllAsString();

    void append(const char* data, size_t len);
    void append(const std::string& str) { append(str.data(), str.size()); }
    void append(const void* data, size_t len) { append(static_cast<const char*>(data), len); }

    char* beginWrite() { return begin() + writerIndex_; }
    const char* beginWrite() const { return begin() + writerIndex_; }
    void hasWritten(size_t len) { writerIndex_ += len; }
    void ensureWritableBytes(size_t len);

    // 从 fd 读数据进缓冲。返回实际读到的字节数（0 = EOF，-1 = 出错，errno 存 savedErrno）。
    ssize_t readFd(int fd, int* savedErrno);

private:
    char* begin() { return buffer_.data(); }
    const char* begin() const { return buffer_.data(); }
    void makeSpace(size_t len);   // 可写区不够时：能 compact 就 compact，否则扩容

    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};
