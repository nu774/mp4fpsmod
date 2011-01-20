#include "mp4v2wrapper.h"

std::string format_mp4error(const mp4v2::impl::MP4Error &e)
{
    std::vector<const char*> parts;
    parts.push_back("libmp4v2");
    if (e.m_where) parts.push_back(e.m_where);
    if (e.m_errstring) parts.push_back(e.m_errstring);
    if (e.m_errno) parts.push_back(std::strerror(e.m_errno));
    std::string message;
    for (size_t i = 0; i < parts.size(); ++i) {
	if (i > 0) message += ": ";
	message += parts[i];
    }
    return message;
}

