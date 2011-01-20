#include "mp4filex.h"

using mp4v2::impl::MP4RootAtom;
using mp4v2::platform::io::File;

void MP4FileX::SaveTo(const char *fileName)
{
    File * current = m_file;
    m_file = 0;
    Open(fileName, File::MODE_CREATE, 0);
    File * dst = m_file;
    SetIntegerProperty("moov.mvhd.modificationTime",
	mp4v2::impl::MP4GetAbsTimestamp());

    dynamic_cast<MP4RootAtom*>(m_pRootAtom)->BeginOptimalWrite();
    RewriteMdat(*current, *dst);
    dynamic_cast<MP4RootAtom*>(m_pRootAtom)->FinishOptimalWrite();
    delete dst;
    m_file = 0;
}
