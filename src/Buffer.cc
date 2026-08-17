#include "Buffer.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/uio.h>

void Buffer::retrieve(size_t len)
{
    if (len < readableBytes())
    {
        readerIndex_ += len;   // 没消费完：只动读下标
    }
    else
    {
        retrieveAll();
    }
}

void Buffer::retrieveAll()
{
    readerIndex_ = kCheapPrepend;
    writerIndex_ = kCheapPrepend;
}

std::string Buffer::retrieveAsString(size_t len)
{
    std::string result(peek(), len);
    retrieve(len);
    return result;
}

std::string Buffer::retrieveAllAsString()
{
    return retrieveAsString(readableBytes());
}

void Buffer::append(const char* data, size_t len)
{
    ensureWritableBytes(len);
    std::copy(data, data + len, beginWrite());
    hasWritten(len);
}

void Buffer::ensureWritableBytes(size_t len)
{
    if (writableBytes() < len)
    {
        makeSpace(len);
    }
}

// 可写区不足时优先 compact（把未读数据搬到前置区起点，腾出整个尾段）；
// 前置区 + 可写区合并仍不够才真的扩容（vector 重分配）。
void Buffer::makeSpace(size_t len)
{
    if (prependableBytes() + writableBytes() < len + kCheapPrepend)
    {
        buffer_.resize(writerIndex_ + len);
    }
    else
    {
        size_t readable = readableBytes();
        std::copy(begin() + readerIndex_, begin() + writerIndex_, begin() + kCheapPrepend);
        readerIndex_ = kCheapPrepend;
        writerIndex_ = readerIndex_ + readable;
    }
}

// 用 readv 一次读两块：第一块 = 现有可写区，第二块 = 栈上 64K 兜底。
// 一次唤醒能把内核里能读的都拿走，可写区不满 64K 时避免反复触发 EPOLLIN。
ssize_t Buffer::readFd(int fd, int* savedErrno)
{
    char extrabuf[65536];
    struct iovec vec[2];
    vec[0].iov_base = begin() + writerIndex_;
    vec[0].iov_len = writableBytes();
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof extrabuf;

    const int iovcnt = (writableBytes() < sizeof extrabuf) ? 2 : 1;
    ssize_t n = ::readv(fd, vec, iovcnt);
    if (n < 0)
    {
        *savedErrno = errno;
    }
    else if (static_cast<size_t>(n) <= writableBytes())
    {
        writerIndex_ += n;   // 全落在现有可写区
    }
    else
    {
        // 现有可写区被填满，多余部分在 extrabuf 里 → 扩进 Buffer
        size_t extra = n - writableBytes();
        writerIndex_ = buffer_.size();
        append(extrabuf, extra);
    }
    return n;
}
