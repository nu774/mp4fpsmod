#include "mp4v2wrapper.h"

std::string format_mp4error(const mp4v2::impl::Exception &e)
{
    return std::string("libmp4v2: ") + e.msg();
}

