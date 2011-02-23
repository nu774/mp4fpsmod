#ifndef _MP4FILEX
#define _MP4FILEX

#include "mp4v2wrapper.h"

class MP4FileX: public mp4v2::impl::MP4File {
public:
    MP4FileX() {}
    ~MP4FileX() { if (m_file) Close(); }
    void SaveTo(const char *fileName);
};

#endif
