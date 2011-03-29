#include <vector>
#include <algorithm>
#include "mp4trackx.h"

using mp4v2::impl::MP4File;
using mp4v2::impl::MP4Track;
using mp4v2::impl::MP4Atom;
using mp4v2::impl::MP4Property;
using mp4v2::impl::MP4IntegerProperty;

int gcd(int a, int b) { return !b ? a : gcd(b, a % b); }

int lcm(int a, int b) { return b * (a / gcd(a, b)); }

double fps2tsdelta(int num, int denom, int timeScale)
{
    return static_cast<double>(timeScale) / num * denom;
}

MP4TrackX::MP4TrackX(MP4File &pFile, MP4Atom &pTrackAtom)
    : MP4Track(pFile, pTrackAtom), m_compressDTS(false)
{
    FetchStts();
    FetchCtts();
    for (size_t i = 0; i < m_sampleTimes.size(); ++i)
	m_ctsIndex.push_back(i);
    std::sort(m_ctsIndex.begin(), m_ctsIndex.end(), CTSComparator(this));
}

void MP4TrackX::SetFPS(FPSRange *fpsRanges, size_t numRanges)
{
    uint32_t timeScale = CalcTimeScale(fpsRanges, fpsRanges + numRanges);
    uint64_t duration = CalcSampleTimes(
	fpsRanges, fpsRanges + numRanges, timeScale);
    if (duration > 0x7fffffff) {
	double t = static_cast<double>(timeScale) * 0x7fffffff / duration;
	for (timeScale = 100; timeScale < t; timeScale *= 10)
	    ;
	timeScale /= 10;
	duration = CalcSampleTimes(fpsRanges, fpsRanges + numRanges,
		timeScale);
    }
    m_timeScale = timeScale;
    m_mediaDuration = duration;
    DoEditTimeCodes();
}

void
MP4TrackX::SetTimeCodes(double *timeCodes, size_t count, uint32_t timeScale)
{
    if (count != GetNumberOfSamples())
	throw std::runtime_error(
		"timecode entry count differs from the movie");

    uint64_t ioffset = 0;
    int64_t delta = 0;
    for (size_t i = 0; i < count; ++i) {
	delta = static_cast<uint64_t>(timeCodes[i]) - ioffset;
	ioffset += delta;
	m_sampleTimes[i].dts = ioffset;
	m_sampleTimes[m_ctsIndex[i]].cts = ioffset;
    }
    ioffset += delta;
    m_timeScale = timeScale;
    m_mediaDuration = ioffset;
    DoEditTimeCodes();
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

uint32_t MP4TrackX::CalcTimeScale(FPSRange *begin, const FPSRange *end)
{
    uint32_t total = 0;
    FPSRange *fp;
    for (fp = begin; fp != end; ++fp) {
	if (fp->numFrames > 0)
	    total += fp->numFrames;
	else {
	    fp->numFrames = std::max(
		static_cast<int>(GetNumberOfSamples() - total), 0);
	    total += fp->numFrames;
	}
    }
    if (total != m_sampleTimes.size())
	throw std::runtime_error(
		"Total number of frames differs from the movie");

    int timeScale = 1;
    bool exact = true;
    int min_denom = INT_MAX;
    for (fp = begin; fp != end; ++fp) {
	int g = gcd(fp->fps_num, fp->fps_denom);
	fp->fps_num /= g;
	fp->fps_denom /= g;
	if (fp->fps_denom < min_denom)
	    min_denom = fp->fps_denom;
	timeScale = lcm(fp->fps_num, timeScale);
	if (timeScale == 0 || timeScale % fp->fps_num) {
	    // LCM overflowed
	    exact = false;
	    break;
	}
    }
    if (!exact) timeScale = 1000; // pick default value
    else if (min_denom < 100) timeScale *= 100.0 / min_denom;
    return timeScale;
}

uint64_t MP4TrackX::CalcSampleTimes(
	const FPSRange *begin, const FPSRange *end, uint32_t timeScale)
{
    double offset = 0.0;
    uint32_t frame = 0;
    for (const FPSRange *fp = begin; fp != end; ++fp) {
	double delta = fps2tsdelta(fp->fps_num, fp->fps_denom, timeScale);
	for (uint32_t i = 0; i < fp->numFrames; ++i) {
	    uint64_t ioffset = static_cast<uint64_t>(offset);
	    m_sampleTimes[frame].dts = ioffset;
	    m_sampleTimes[m_ctsIndex[frame]].cts = ioffset;
	    offset += delta; 
	    ++frame;
	}
    }
    return static_cast<uint64_t>(offset);
}

int64_t MP4TrackX::CalcInitialDelay()
{
    int64_t maxdiff = 0;
    std::vector<SampleTime>::iterator is;
    for (is = m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is) {
	int64_t diff = is->dts - is->cts;
	if (diff > maxdiff) maxdiff = diff;
    }
    return maxdiff;
}

int64_t MP4TrackX::CompressDTS(int64_t initialDelay)
{
    size_t n = 0;
    double scale = static_cast<double>(m_sampleTimes[1].dts) / initialDelay;
    int64_t prev_dts = 0;
    for (n = 1; n < m_sampleTimes.size(); ++n) {
	int64_t dts = m_sampleTimes[n].dts;
	if (dts - initialDelay > prev_dts)
	    break;
	int64_t new_dts = dts * scale;
	if (new_dts == prev_dts)
	    ++new_dts;
	m_sampleTimes[n].dts = prev_dts = new_dts;
    }
    for (size_t i = n; i < m_sampleTimes.size(); ++i) {
	m_sampleTimes[i].dts -= initialDelay;
    }
    return CalcInitialDelay();
}

void MP4TrackX::CalcCTSOffset(int64_t initialDelay)
{
    std::vector<SampleTime>::iterator is;
    for (is = m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is) {
	is->cts += initialDelay;
	is->ctsOffset = is->cts - is->dts;
    }
}

void MP4TrackX::DoEditTimeCodes()
{
    int64_t initialDelay = CalcInitialDelay();
    if (m_compressDTS)
	initialDelay = CompressDTS(initialDelay);
    CalcCTSOffset(initialDelay);
    m_pTimeScaleProperty->SetValue(m_timeScale);
    m_pMediaDurationProperty->SetValue(0);
    UpdateDurations(m_mediaDuration);

    uint32_t ntracks = GetFile().GetNumberOfTracks();
    int64_t max_duration = 0;
    for (uint32_t i = 0; i < ntracks; ++i) {
	MP4Track *track = GetFile().GetTrack(GetFile().FindTrackId(i));
	MP4Atom &trackAtom = track->GetTrakAtom();
	MP4Property *pProp;
	if (trackAtom.FindProperty("trak.tkhd.duration", &pProp)) {
	    int64_t v = dynamic_cast<MP4IntegerProperty*>(pProp)->GetValue();
	    max_duration = std::max(max_duration, v);
	}
    }
    GetFile().SetDuration(max_duration);
    
    UpdateStts();
    if (m_pCttsCountProperty) {
	UpdateCtts();
	int64_t movieDuration = m_pTrackDurationProperty->GetValue();
	UpdateElst(movieDuration, initialDelay);
    }
    UpdateModificationTimes();
}

void MP4TrackX::UpdateStts()
{
    int32_t count = static_cast<int32_t>(m_pSttsCountProperty->GetValue());
    m_pSttsCountProperty->IncrementValue(-1 * count);
    m_pSttsSampleCountProperty->SetCount(0);
    m_pSttsSampleDeltaProperty->SetCount(0);
    
    uint64_t prev_dts = 0;
    int32_t prev_delta = -1;
    size_t sttsIndex = -1;
    std::vector<SampleTime>::iterator is;
    for (is = ++m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is) {
	int32_t delta = static_cast<int32_t>(is->dts - prev_dts);
	if (delta != prev_delta) {
	    ++sttsIndex;
	    m_pSttsCountProperty->IncrementValue();
	    m_pSttsSampleCountProperty->AddValue(0);
	    m_pSttsSampleDeltaProperty->AddValue(delta);
	}
	prev_dts = is->dts;
	prev_delta = delta;
	m_pSttsSampleCountProperty->IncrementValue(1, sttsIndex);
    }
    m_pSttsSampleCountProperty->IncrementValue(1, sttsIndex);
}

void MP4TrackX::UpdateCtts()
{
    int32_t count = static_cast<int32_t>(m_pCttsCountProperty->GetValue());
    m_pCttsCountProperty->IncrementValue(-1 * count);
    m_pCttsSampleCountProperty->SetCount(0);
    m_pCttsSampleOffsetProperty->SetCount(0);

    int32_t offset = -1;
    size_t cttsIndex = -1;
    std::vector<SampleTime>::const_iterator is;
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
}

void MP4TrackX::UpdateElst(int64_t duration, int64_t mediaTime)
{
    if (!m_pElstCountProperty)
	AddEdit();
    m_pElstMediaTimeProperty->SetValue(mediaTime);
    m_pElstDurationProperty->SetValue(duration);
}
