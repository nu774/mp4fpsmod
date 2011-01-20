#include <vector>
#include <algorithm>
#include "mp4trackx.h"

using mp4v2::impl::MP4File;
using mp4v2::impl::MP4Track;
using mp4v2::impl::MP4Atom;

int gcd(int a, int b) { return !b ? a : gcd(b, a % b); }

int lcm(int a, int b) { return b * (a / gcd(a, b)); }

int fps2tsdelta(int num, int denom, int timeScale)
{
    return timeScale / num * denom;
}

MP4TrackX::MP4TrackX(MP4File *pFile, MP4Atom *pTrackAtom)
    : MP4Track(pFile, pTrackAtom)
{
    FetchStts();
    FetchCtts();
    for (size_t i = 0; i < m_sampleTimes.size(); ++i)
	m_ctsIndex.push_back(i);
    std::sort(m_ctsIndex.begin(), m_ctsIndex.end(), CTSComparator(this));
}

void MP4TrackX::SetFPS(FPSRange *fpsRanges, size_t numRanges)
{
    uint32_t total = 0;
    FPSRange *fp, *endp = fpsRanges + numRanges;
    for (fp = fpsRanges; fp != endp; ++fp) {
	if (fp->numFrames > 0)
	    total += fp->numFrames;
	else {
	    fp->numFrames = GetNumberOfSamples() - total;
	    total += fp->numFrames;
	}
    }
    if (total != m_sampleTimes.size())
	throw std::runtime_error(
		"Total number of frames differs from the movie");

    int timeScale = 1;
    for (fp = fpsRanges; fp != endp; ++fp)
	timeScale = lcm(fp->fps_num, timeScale);

    m_pTimeScaleProperty->SetValue(timeScale);

    uint64_t duration = UpdateStts(fpsRanges, endp, timeScale);
    m_pMediaDurationProperty->SetValue(0);
    UpdateDurations(duration);
    
    if (m_pCttsCountProperty) {
	int64_t initialDelay = UpdateCtts();
	int64_t movieDuration = m_pTrackDurationProperty->GetValue();
	UpdateElst(movieDuration, initialDelay);
    }
    UpdateModificationTimes();
}

void MP4TrackX::FetchStts()
{
    uint32_t numStts = m_pSttsCountProperty->GetValue();
    uint64_t dts = 0;
    for (uint32_t i = 0; i < numStts; ++i) {
	uint32_t sampleCount = m_pSttsSampleCountProperty->GetValue(i);
	uint32_t sampleDelta = m_pSttsSampleDeltaProperty->GetValue(i);
	for (uint32_t j = 0; j < sampleCount; ++j) {
	    SampleTime st = { dts, 0 };
	    m_sampleTimes.push_back(st);
	    dts += sampleDelta;
	}
    }
}

void MP4TrackX::FetchCtts()
{
    if (!m_pCttsCountProperty)
	return;
    {
	uint32_t numCtts = m_pCttsCountProperty->GetValue();
	SampleTime *sp = &m_sampleTimes[0];
	for (uint32_t i = 0; i < numCtts; ++i) {
	    uint32_t sampleCount = m_pCttsSampleCountProperty->GetValue(i);
	    uint32_t ctsOffset = m_pCttsSampleOffsetProperty->GetValue(i);
	    for (uint32_t j = 0; j < sampleCount; ++j) {
		sp->ctsOffset = ctsOffset;
		++sp;
	    }
	}
    }
    {
	SampleTime *sp = &m_sampleTimes[0];
	for (size_t i = 0; i < m_sampleTimes.size(); ++i) {
	    sp->cts = sp->dts + sp->ctsOffset;
	    ++sp;
	}
    }
}

uint64_t MP4TrackX::UpdateStts(
	const FPSRange *begin, const FPSRange *end, uint32_t timeScale)
{
    int32_t count = static_cast<int32_t>(m_pSttsCountProperty->GetValue());
    m_pSttsCountProperty->IncrementValue(-1 * count);
    m_pSttsSampleCountProperty->SetCount(0);
    m_pSttsSampleDeltaProperty->SetCount(0);
    
    uint64_t offset = 0;
    uint32_t frame = 0;
    for (const FPSRange *fp = begin; fp != end; ++fp) {
	int delta = fps2tsdelta(fp->fps_num, fp->fps_denom, timeScale);
	m_pSttsSampleCountProperty->AddValue(fp->numFrames);
	m_pSttsSampleDeltaProperty->AddValue(delta);
	m_pSttsCountProperty->IncrementValue();
	for (uint32_t i = 0; i < fp->numFrames; ++i) {
	    m_sampleTimes[frame].dts = offset;
	    m_sampleTimes[m_ctsIndex[frame]].cts = offset;
	    offset += delta; 
	    ++frame;
	}
    }
    return offset;
}

int64_t MP4TrackX::UpdateCtts()
{
    int64_t maxdiff = 0;
    std::vector<SampleTime>::iterator is;
    for (is = m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is) {
	int64_t diff = is->dts - is->cts;
	if (diff > maxdiff) maxdiff = diff;
    }
    for (is = m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is) {
	is->cts += maxdiff;
	is->ctsOffset = is->cts - is->dts;
    }
    
    int32_t count = static_cast<int32_t>(m_pCttsCountProperty->GetValue());
    m_pCttsCountProperty->IncrementValue(-1 * count);
    m_pCttsSampleCountProperty->SetCount(0);
    m_pCttsSampleOffsetProperty->SetCount(0);

    int32_t offset = -1;
    size_t cttsIndex = -1;
    for (is = m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is) {
	if (is->ctsOffset != offset) {
	    offset = is->ctsOffset;
	    ++cttsIndex;
	    m_pCttsCountProperty->IncrementValue();
	    m_pCttsSampleCountProperty->AddValue(0);
	    m_pCttsSampleOffsetProperty->AddValue(offset);
	}
	m_pCttsSampleCountProperty->IncrementValue(1, cttsIndex);
    }
    return maxdiff;
}

void MP4TrackX::UpdateElst(int64_t duration, int64_t mediaTime)
{
    if (!m_pElstCountProperty)
	AddEdit();
    m_pElstMediaTimeProperty->SetValue(mediaTime);
    m_pElstDurationProperty->SetValue(duration);
}
