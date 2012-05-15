#include <vector>
#include <algorithm>
#include "mp4trackx.h"

using mp4v2::impl::MP4File;
using mp4v2::impl::MP4Track;
using mp4v2::impl::MP4Atom;
using mp4v2::impl::MP4Property;
using mp4v2::impl::MP4IntegerProperty;
using mp4v2::impl::MP4Integer32Property;
using mp4v2::impl::MP4LanguageCodeProperty;

int gcd(int a, int b) { return !b ? a : gcd(b, a % b); }

int lcm(int a, int b) { return b * (a / gcd(a, b)); }

double fps2tsdelta(int num, int denom, int timeScale)
{
    return static_cast<double>(timeScale) / num * denom;
}

void MP4TrackX::RebuildMdhd()
{
    MP4Atom *mdhd = m_trakAtom.FindAtom("trak.mdia.mdhd");

    uint32_t timeScale = GetTimeScale();
    uint64_t duration = m_pMediaDurationProperty->GetValue();

    MP4Property *prop;
    mdhd->FindProperty("mdhd.language", &prop);
    mp4v2::impl::bmff::LanguageCode lang =
	dynamic_cast<MP4LanguageCodeProperty*>(prop)->GetValue();

    MP4Atom *mdia = mdhd->GetParentAtom();
    mdia->DeleteChildAtom(mdhd);
    delete mdhd;
    mdhd = MP4Atom::CreateAtom(m_File, mdia, "mdhd");
    mdia->InsertChildAtom(mdhd, 0);
    mdhd->Generate();

    // update caches
    mdhd->FindProperty("mdhd.timeScale", &prop);
    m_pTimeScaleProperty = dynamic_cast<MP4Integer32Property*>(prop);
    m_pTimeScaleProperty->SetValue(timeScale);

    mdhd->FindProperty("mdhd.duration", &prop);
    m_pMediaDurationProperty= dynamic_cast<MP4IntegerProperty*>(prop);
    m_pMediaDurationProperty->SetValue(duration);

    mdhd->FindProperty("mdhd.modificationTime", &prop);
    m_pMediaModificationProperty = dynamic_cast<MP4IntegerProperty*>(prop);

    mdhd->FindProperty("mdhd.language", &prop);
    dynamic_cast<MP4LanguageCodeProperty*>(prop)->SetValue(lang);
}

TrackEditor::TrackEditor(MP4TrackX *track)
    : m_track(track), m_compressDTS(false), m_audioDelay(0)
{
    m_timeScale = m_track->GetTimeScale();
    LoadDTS();
    LoadCTS();
    for (size_t i = 0; i < m_sampleTimes.size(); ++i)
	m_ctsIndex.push_back(i);
    std::sort(m_ctsIndex.begin(), m_ctsIndex.end(), CTSComparator(this));
}

void TrackEditor::SetFPS(FPSRange *fpsRanges, size_t numRanges, int timeScale)
{
    uint32_t scale = m_timeScale;
    NormalizeFPSRange(fpsRanges, fpsRanges + numRanges);
    if (timeScale == 0)
	scale = CalcTimeScale(fpsRanges, fpsRanges + numRanges);
    else if (timeScale > 0)
	scale = timeScale;
    uint64_t duration =
	CalcSampleTimes(fpsRanges, fpsRanges + numRanges, scale);
    if (duration > 0x7fffffff && timeScale == 0) {
	double t = static_cast<double>(scale) * 0x7fffffff / duration;
	for (scale = 100; scale < t; scale *= 10)
	    ;
	scale /= 10;
	duration = CalcSampleTimes(fpsRanges, fpsRanges + numRanges, scale);
    }
    m_timeScale = scale;
}

void
TrackEditor::SetTimeCodes(double *timeCodes, size_t count, uint32_t timeScale)
{
    if (count != m_track->GetNumberOfSamples())
	throw std::runtime_error(
		"timecode entry count differs from the movie");

    uint64_t ioffset = 0;
    int64_t delta = 0;
    for (size_t i = 0; i < count; ++i) {
	delta = static_cast<uint64_t>(timeCodes[i] + 0.5) - ioffset;
	ioffset += delta;
	DTS(i) = CTS(i) = ioffset;
    }
    m_timeScale = timeScale;
}

void TrackEditor::LoadDTS()
{
    uint32_t numStts = m_track->SttsCountProperty()->GetValue();
    uint64_t dts = 0;
    for (uint32_t i = 0; i < numStts; ++i) {
	uint32_t sampleCount = m_track->SttsSampleCountProperty()->GetValue(i);
	uint32_t sampleDelta = m_track->SttsSampleDeltaProperty()->GetValue(i);
	for (uint32_t j = 0; j < sampleCount; ++j) {
	    SampleTime st = { dts, dts };
	    m_sampleTimes.push_back(st);
	    dts += sampleDelta;
	}
    }
}

void TrackEditor::LoadCTS()
{
    if (m_track->CttsCountProperty())
    {
	uint32_t numCtts = m_track->CttsCountProperty()->GetValue();
	SampleTime *sp = &m_sampleTimes[0];
	for (uint32_t i = 0; i < numCtts; ++i) {
	    uint32_t sampleCount =
		m_track->CttsSampleCountProperty()->GetValue(i);
	    int32_t ctsOffset
		= m_track->CttsSampleOffsetProperty()->GetValue(i);
	    for (uint32_t j = 0; j < sampleCount; ++j) {
		sp->cts = sp->dts + ctsOffset;
		++sp;
	    }
	}
    }
}

void TrackEditor::NormalizeFPSRange(FPSRange *begin, const FPSRange *end)
{
    uint32_t total = 0;
    FPSRange *fp;
    for (fp = begin; fp != end; ++fp) {
	if (fp->numFrames > 0)
	    total += fp->numFrames;
	else {
	    fp->numFrames = std::max(
		static_cast<int>(m_track->GetNumberOfSamples() - total), 0);
	    total += fp->numFrames;
	}
    }
    if (total != m_sampleTimes.size())
	throw std::runtime_error(
		"Total number of frames differs from the movie");
}

uint32_t TrackEditor::CalcTimeScale(FPSRange *begin, const FPSRange *end)
{
    int timeScale = 1;
    bool exact = true;
    int min_denom = INT_MAX;
    FPSRange *fp;
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

uint64_t TrackEditor::CalcSampleTimes(
	const FPSRange *begin, const FPSRange *end, uint32_t timeScale)
{
    double offset = 0.0;
    uint32_t frame = 0;
    for (const FPSRange *fp = begin; fp != end; ++fp) {
	double delta = fps2tsdelta(fp->fps_num, fp->fps_denom, timeScale);
	for (uint32_t i = 0; i < fp->numFrames; ++i) {
	    uint64_t ioffset = static_cast<uint64_t>(offset + 0.5);
	    DTS(frame) = CTS(frame) = ioffset;
	    offset += delta; 
	    ++frame;
	}
    }
    return static_cast<uint64_t>(offset + 0.5);
}

int64_t TrackEditor::CalcInitialDelay()
{
    int64_t maxdiff = 0;
    std::vector<SampleTime>::iterator is;
    for (is = m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is) {
	int64_t diff = is->dts - is->cts;
	if (diff > maxdiff) maxdiff = diff;
    }
    return maxdiff;
}

const double TC_COMPRESS_SCALE_MIN = 0.5;

/*
 * TimeCode is a functor/function, such that timeCode(n) returns the
 * reference to nth timecode.
 */
template <typename TimeCode>
void TrackEditor::CompressTimeCodes(int64_t offset, TimeCode timeCode)
{
    if (!GetFrameCount())
	return;
    size_t n = 0;
    double off = timeCode(1);
    double scale = std::max((off / (offset + off)), TC_COMPRESS_SCALE_MIN);
    int64_t prev = 0;
    for (n = 1; n < GetFrameCount(); ++n) {
	int64_t orig = timeCode(n);
	if (orig - offset >= prev + (off * scale)) break;
	int64_t cur = orig * scale + 0.5;
	if (cur == prev) ++cur;
	timeCode(n) = prev = cur;
    }
    for (size_t i = n; i < GetFrameCount(); ++i)
	timeCode(i) -= offset;
}

const double TC_DELAY_SCALE = 4.0;

template <typename TimeCode>
void TrackEditor::DelayTimeCodes(int64_t offset, TimeCode timeCode)
{
    if (!GetFrameCount())
	return;
    size_t n = 0;
    double off = timeCode(1);
    double factor = 2.0;
    double scale;
    do {
	scale = (offset / factor + off) / off;
	factor += 1.0;
    } while (scale > TC_DELAY_SCALE);
    int64_t prev = 0;
    for (n = 1; n < GetFrameCount(); ++n) {
	int64_t orig = timeCode(n);
	int64_t cur = orig * scale + 0.5;
	if (cur >= orig + offset)
	    break;
	timeCode(n) = prev = cur;
    }
    for (size_t i = n; i < GetFrameCount(); ++i)
	timeCode(i) += offset;
}

void TrackEditor::OffsetCTS(int64_t off)
{
    std::vector<SampleTime>::iterator is;
    for (is = m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is)
	is->cts += off;
}

void TrackEditor::AdjustTimeCodes()
{
    std::binder1st<std::mem_fun1_t<uint64_t&, TrackEditor, size_t> >
	ctsf = std::bind1st(std::mem_fun(&TrackEditor::CTS), this),
	dtsf = std::bind1st(std::mem_fun(&TrackEditor::DTS), this);

    if (m_compressDTS && m_audioDelay < 0) {
	if (m_audioDelay < 0) {
	    int64_t delay = -1 * GetAudioDelayInTimeScale();
	    DelayTimeCodes(delay, ctsf);
	    DelayTimeCodes(delay, dtsf);
	}
    }
    if (CTS(0) > 0)
	OffsetCTS(CTS(0) * -1);
    if (m_compressDTS && m_audioDelay > 0)
	CompressTimeCodes(GetAudioDelayInTimeScale(), ctsf);
    m_initialDelay = CalcInitialDelay();
    if (m_compressDTS) {
	CompressTimeCodes(m_initialDelay, dtsf);
	m_initialDelay = CalcInitialDelay();
    }
    OffsetCTS(m_initialDelay);
}

void TrackEditor::DoEditTimeCodes()
{
    MP4File &file = m_track->GetFile();
    uint32_t ntracks = file.GetNumberOfTracks();

    if (m_timeScale != m_track->GetTimeScale() ||
	GetMediaDuration() != m_track->MediaDurationProperty()->GetValue())
    {
	m_track->RebuildMdhd();
	m_track->TimeScaleProperty()->SetValue(m_timeScale);
	m_track->MediaDurationProperty()->SetValue(0);
	m_track->UpdateDurationsX(GetMediaDuration());

	int64_t max_duration = 0;
	for (uint32_t i = 0; i < ntracks; ++i) {
	    MP4Track *track = file.GetTrack(file.FindTrackId(i));
	    MP4Atom &trackAtom = track->GetTrakAtom();
	    MP4Property *pProp;
	    if (trackAtom.FindProperty("trak.tkhd.duration", &pProp)) {
		int64_t v =
		    dynamic_cast<MP4IntegerProperty*>(pProp)->GetValue();
		max_duration = std::max(max_duration, v);
	    }
	}
	file.SetDuration(max_duration);
    }
    
    UpdateStts();
    if (m_track->CttsCountProperty()) {
	UpdateCtts();
    }
    int64_t delay = m_initialDelay;
    if (!m_compressDTS && m_audioDelay > 0)
	delay += GetAudioDelayInTimeScale();
    UpdateElst(m_track, delay);
    m_track->UpdateModificationTimesX();
    for (uint32_t i = 0; i < ntracks; ++i) {
	MP4TrackX *track = reinterpret_cast<MP4TrackX*>(
	    file.GetTrack(file.FindTrackId(i)));
	if (std::strcmp(track->GetType(), "soun"))
	    continue;
	delay = (!m_compressDTS && m_audioDelay < 0)
	    ? -1 * m_audioDelay / 1000.0 * track->GetTimeScale()
	    : 0;
	UpdateElst(track, delay);
	track->UpdateModificationTimesX();
    }
}

void TrackEditor::UpdateStts()
{
    int32_t count = static_cast<int32_t>(
	    m_track->SttsCountProperty()->GetValue());
    m_track->SttsCountProperty()->IncrementValue(-1 * count);
    m_track->SttsSampleCountProperty()->SetCount(0);
    m_track->SttsSampleDeltaProperty()->SetCount(0);
    
    uint64_t prev_dts = 0;
    int32_t prev_delta = -1;
    size_t sttsIndex = -1;
    std::vector<SampleTime>::iterator is;
    for (is = ++m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is) {
	int32_t delta = static_cast<int32_t>(is->dts - prev_dts);
	if (delta != prev_delta) {
	    ++sttsIndex;
	    m_track->SttsCountProperty()->IncrementValue();
	    m_track->SttsSampleCountProperty()->AddValue(0);
	    m_track->SttsSampleDeltaProperty()->AddValue(delta);
	}
	prev_dts = is->dts;
	prev_delta = delta;
	m_track->SttsSampleCountProperty()->IncrementValue(1, sttsIndex);
    }
    m_track->SttsSampleCountProperty()->IncrementValue(1, sttsIndex);
}

void TrackEditor::UpdateCtts()
{
    int32_t count = static_cast<int32_t>(
	    m_track->CttsCountProperty()->GetValue());
    m_track->CttsCountProperty()->IncrementValue(-1 * count);
    m_track->CttsSampleCountProperty()->SetCount(0);
    m_track->CttsSampleOffsetProperty()->SetCount(0);

    int32_t offset = -1;
    size_t cttsIndex = -1;
    std::vector<SampleTime>::const_iterator is;
    for (is = m_sampleTimes.begin(); is != m_sampleTimes.end(); ++is) {
	int32_t ctsoff = is->cts - is->dts;
	if (ctsoff != offset) {
	    offset = ctsoff;
	    ++cttsIndex;
	    m_track->CttsCountProperty()->IncrementValue();
	    m_track->CttsSampleCountProperty()->AddValue(0);
	    m_track->CttsSampleOffsetProperty()->AddValue(offset);
	}
	m_track->CttsSampleCountProperty()->IncrementValue(1, cttsIndex);
    }
}

void TrackEditor::UpdateElst(MP4TrackX *track, int64_t mediaTime)
{
    int64_t duration = track->TrackDurationProperty()->GetValue();
    if (track->ElstCountProperty()) {
	size_t count = track->ElstCountProperty()->GetValue();
	for (size_t i = 0; i < count; ++i)
	    track->DeleteEdit(i + 1);
    }
    if (mediaTime) {
	track->AddEdit();
	track->ElstMediaTimeProperty()->SetValue(mediaTime);
	track->ElstDurationProperty()->SetValue(duration);
    }
}
