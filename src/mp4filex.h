#ifndef _MP4FILEX
#define _MP4FILEX

#include "mp4v2wrapper.h"

class MP4FileCopy;

class MP4FileX: public mp4v2::impl::MP4File {
    friend class MP4FileCopy;
};

class MP4FileCopy {
    struct ChunkInfo {
        mp4v2::impl::MP4ChunkId current, final;
        MP4Timestamp time;
    };
    MP4FileX *m_mp4file;
    uint64_t m_nchunks;
    std::vector<ChunkInfo> m_state;
    mp4v2::platform::io::File *m_src;
    mp4v2::platform::io::File *m_dst;
public:
    MP4FileCopy(mp4v2::impl::MP4File *file);
    ~MP4FileCopy() { if (m_dst) finish(); }
    void start(const char *path);
    void finish();
    bool copyNextChunk();
    uint64_t getTotalChunks() { return m_nchunks; }
};

#endif
