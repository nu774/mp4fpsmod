#ifndef _MP4TRACKX
#define _MP4TRACKX

#include "mp4v2wrapper.h"

struct SampleTime {
    uint64_t dts, cts;
    uint32_t ctsOffset;
};

struct FPSRange {
    uint32_t numFrames;
    int fps_num, fps_denom;
};

/*
 *  XXX:
 *  Ugly class only to reveal protected members of MP4Track
 *  via casting.
 *  Cannot have data members!
 */
class MP4TrackX: public mp4v2::impl::MP4Track {
public:
    void RebuildMdhd();

    void UpdateDurationsX(MP4Duration duration) {
	return UpdateDurations(duration);
    }
    MP4Duration ToMovieDurationX(MP4Duration trackDuration) {
	return ToMovieDuration(trackDuration);
    }
    void UpdateModificationTimesX() {
	return UpdateModificationTimes();
    }

    mp4v2::impl::MP4Integer32Property*& TimeScaleProperty() {
	return m_pTimeScaleProperty;
    }
    mp4v2::impl::MP4IntegerProperty* TrackDurationProperty() {
	return m_pTrackDurationProperty;
    }
    mp4v2::impl::MP4IntegerProperty*& MediaDurationProperty() {
	return m_pMediaDurationProperty;
    }
    mp4v2::impl::MP4IntegerProperty*& MediaModificationProperty() {
	return m_pMediaModificationProperty;
    }
    mp4v2::impl::MP4Integer32Property* SttsCountProperty() {
	return m_pSttsCountProperty;
    }
    mp4v2::impl::MP4Integer32Property* SttsSampleCountProperty() {
	return m_pSttsSampleCountProperty;
    }
    mp4v2::impl::MP4Integer32Property* SttsSampleDeltaProperty() {
	return m_pSttsSampleDeltaProperty;
    }
    mp4v2::impl::MP4Integer32Property* CttsCountProperty() {
	return m_pCttsCountProperty;
    }
    mp4v2::impl::MP4Integer32Property* CttsSampleCountProperty() {
	return m_pCttsSampleCountProperty;
    }
    mp4v2::impl::MP4Integer32Property* CttsSampleOffsetProperty() {
	return m_pCttsSampleOffsetProperty;
    }
    mp4v2::impl::MP4Integer32Property* ElstCountProperty() {
	return m_pElstCountProperty;
    }
    mp4v2::impl::MP4IntegerProperty* ElstMediaTimeProperty() {
	return m_pElstMediaTimeProperty;
    }
    mp4v2::impl::MP4IntegerProperty* ElstDurationProperty() {
	return m_pElstDurationProperty;
    }
};

class TrackEditor {
    struct CTSComparator {
	TrackEditor *owner_;
	CTSComparator(TrackEditor *owner): owner_(owner) {}
	bool operator()(uint32_t a, uint32_t b)
	{
	    return owner_->CompareByCTS(a, b);
	}
    };
    MP4TrackX *m_track;
    std::vector<SampleTime> m_sampleTimes;
    std::vector<uint32_t> m_ctsIndex;
    uint32_t m_timeScale;
    uint64_t m_mediaDuration;
    int64_t m_initialDelay;
    bool m_compressDTS;
public:
    TrackEditor(MP4TrackX *track);
    void EnableDTSCompression(bool enable)
    {
	m_compressDTS = enable;
    }
    void SetFPS(FPSRange *fpsRanges, size_t numRanges);
    void SetTimeCodes(double *timeCodes, size_t count, uint32_t timeScale);
    void AdjustTimeCodes();
    void DoEditTimeCodes();
    uint32_t GetTimeScale() const { return m_timeScale; }
    const std::vector<SampleTime> &GetTimeCodes() const
    {
	return m_sampleTimes;
    }
    const std::vector<uint32_t> &GetCTSIndex() const
    {
	return m_ctsIndex;
    }
private:
    bool CompareByCTS(uint32_t a, uint32_t b)
    {
	return m_sampleTimes[a].cts < m_sampleTimes[b].cts;
    }
    void GetDTS();
    void GetCTS();
    uint32_t CalcTimeScale(FPSRange *begin, const FPSRange *end);
    uint64_t CalcSampleTimes(
	    const FPSRange *begin, const FPSRange *end, uint32_t timeScale);
    void CompressDTS();
    void OffsetCTS(int64_t off);
    int64_t CalcInitialDelay();
    void UpdateStts();
    void UpdateCtts();
    void UpdateElst(int64_t duration, int64_t mediaTime);
};

#endif
