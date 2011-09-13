#include "mp4filex.h"

using mp4v2::impl::MP4File;
using mp4v2::impl::MP4Track;
using mp4v2::impl::MP4RootAtom;
using mp4v2::platform::io::File;

MP4FileCopy::MP4FileCopy(MP4File *file)
	: m_mp4file(reinterpret_cast<MP4FileX*>(file)),
	  m_src(reinterpret_cast<MP4FileX*>(file)->m_file),
	  m_dst(0)
{
    m_nchunks = 0;
    size_t numTracks = file->GetNumberOfTracks();
    for (size_t i = 0; i < numTracks; ++i) {
	ChunkInfo ci;
	ci.current = 1;
	ci.final = m_mp4file->m_pTracks[i]->GetNumberOfChunks();
	ci.time = MP4_INVALID_TIMESTAMP;
	m_state.push_back(ci);
	m_nchunks += ci.final;
    }
}

void MP4FileCopy::start(const char *path)
{
    m_mp4file->m_file = 0;
    m_mp4file->Open(path, File::MODE_CREATE, 0);
    m_dst = m_mp4file->m_file;
    m_mp4file->SetIntegerProperty("moov.mvhd.modificationTime",
	mp4v2::impl::MP4GetAbsTimestamp());
    dynamic_cast<MP4RootAtom*>(m_mp4file->m_pRootAtom)->BeginOptimalWrite();
}

void MP4FileCopy::finish()
{
    if (!m_dst)
	return;
    try {
	MP4RootAtom *root = dynamic_cast<MP4RootAtom*>(m_mp4file->m_pRootAtom);
	root->FinishOptimalWrite();
    } catch (...) {
	delete m_dst;
	m_dst = 0;
	m_mp4file->m_file = m_src;
	throw;
    }
    delete m_dst;
    m_dst = 0;
    m_mp4file->m_file = m_src;
}

bool MP4FileCopy::copyNextChunk()
{
    uint32_t nextTrack = -1;
    MP4Timestamp nextTime = MP4_INVALID_TIMESTAMP;
    size_t numTracks = m_mp4file->GetNumberOfTracks();
    for (size_t i = 0; i < numTracks; ++i) {
	MP4Track *track = m_mp4file->m_pTracks[i];
	if (m_state[i].current > m_state[i].final)
	    continue;
	if (m_state[i].time == MP4_INVALID_TIMESTAMP) {
	    MP4Timestamp time = track->GetChunkTime(m_state[i].current);
	    m_state[i].time = mp4v2::impl::MP4ConvertTime(time,
		    track->GetTimeScale(), m_mp4file->GetTimeScale());
	}
	if (m_state[i].time > nextTime)
	    continue;
	if (m_state[i].time == nextTime &&
		std::strcmp(track->GetType(), MP4_HINT_TRACK_TYPE))
	    continue;
	nextTime = m_state[i].time;
	nextTrack = i;
    }
    if (nextTrack == -1) return false;
    MP4Track *track = m_mp4file->m_pTracks[nextTrack];
    m_mp4file->m_file = m_src;
    uint8_t *chunk;
    uint32_t size;
    track->ReadChunk(m_state[nextTrack].current, &chunk, &size);
    m_mp4file->m_file = m_dst;
    track->RewriteChunk(m_state[nextTrack].current, chunk, size);
    MP4Free(chunk);
    m_state[nextTrack].current++;
    m_state[nextTrack].time = MP4_INVALID_TIMESTAMP;
    return true;
}
