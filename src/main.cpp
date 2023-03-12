#include <cstdio>
#include <cmath>
#include <fstream>
#include <sstream>
#include <numeric>
#include <algorithm>
#if defined(_WIN32)
#include <windows.h>
#include "utf8_codecvt_facet.hpp"
#include "strcnv.h"
#endif
#ifdef _MSC_VER
#include "getopt.h"
#else
#include <getopt.h>
#endif
#include "mp4filex.h"
#include "mp4trackx.h"
#include "mp4v2/project.h"

struct Option {
    const char *src, *dst, *timecodeFile;
    bool inplace;
    bool compressDTS;
    bool optimizeTimecode;
    bool printOnly;
    uint32_t originalTimeScale;
    uint32_t timeScale;
    int requestedTimeScale;
    int audioDelay;
    int audioTimeDelta;
    std::vector<FPSRange> ranges;
    std::vector<double> timecodes;
    std::vector<std::pair<size_t, double> > averages;

    Option()
    {
        src = 0;
        dst = 0;
        timecodeFile = 0;
        inplace = false;
        compressDTS = false;
        optimizeTimecode = false;
        printOnly = false;
        requestedTimeScale = 0;
        timeScale = 1000;
        audioDelay = 0;
        audioTimeDelta = 0;
    }
    bool modified() {
        return compressDTS || audioDelay || ranges.size()
            || timecodes.size() || optimizeTimecode || requestedTimeScale > 0 || audioTimeDelta > 0;
    }
};

bool convertToExactRanges(Option &opt)
{
    struct FPSSpec {
        double delta;
        int num, denom;
    } wellKnown[] = {
        { 0, 18000, 1001 },
        { 0, 24000, 1001 },
        { 0, 25, 1 },
        { 0, 30000, 1001 },
        { 0, 50, 1 },
        { 0, 60000, 1001 }
    };
    FPSSpec *sp, *spEnd = wellKnown + 5;
    for (sp = wellKnown; sp != spEnd; ++sp)
        sp->delta = static_cast<double>(sp->denom) / sp->num * opt.timeScale;

    std::vector<FPSRange> ranges;
    std::vector<std::pair<size_t, double> >::const_iterator dp;
    for (dp = opt.averages.begin(); dp != opt.averages.end(); ++dp) {
        double delta = dp->second;
        double bound = std::max(1.0 / dp->first, 0.00048828125);
        for (sp = wellKnown; sp != spEnd; ++sp) {
            /* test if it's close enough to one of the well known rate. */
            double diff = std::abs(delta - sp->delta);
            if (diff < bound) {
                FPSRange range = { dp->first, sp->num, sp->denom };
                ranges.push_back(range);
                break;
            }
        }
        if (sp == spEnd)
            return false;
    }
    std::fprintf(stderr, "Converted to exact fps ranges\n");
    for (size_t i = 0; i < ranges.size(); ++i) {
        std::fprintf(stderr, "%d frames: fps %d/%d\n",
            ranges[i].numFrames, ranges[i].fps_num, ranges[i].fps_denom);
    }
    opt.ranges.swap(ranges);
    return true;
}

/*
 * divide sequence like 1, 2, 1, 1, 2, 3, 2, 3, 3, 2, 1, 1, 2
 * into groups:
 * (1, 2, 1, 1, 2), (3, 2, 3, 3, 2), (1, 1, 2)
 *
 * each group can hold continuous numbers within [n, n + 1] for some n.
 */
template <typename T, typename InputIterator>
void groupbyAdjacent(InputIterator begin, InputIterator end,
        std::vector<std::vector<T> > *result)
{
    std::vector<std::vector<T> > groups;
    T low = 1, high = 0;
    for (; begin != end; ++begin) {
        if (*begin < low || *begin > high) {
            groups.push_back(std::vector<T>());
            low = *begin - 1;
            high = *begin + 1;
        } else {
            T prev = groups.back().back();
            if (prev != *begin && high - low == 2) {
                low = std::min(prev, *begin);
                high = std::max(prev, *begin);
            }
        }
        groups.back().push_back(*begin);
    }
    result->swap(groups);
}

void averageTimecode(Option &opt)
{
    std::vector<double> &tc = opt.timecodes;
    std::vector<int> deltas;
    int prev = tc[0] + 0.5;
    for (std::vector<double>::const_iterator ii = ++tc.begin();
            ii != tc.end(); ++ii) {
        deltas.push_back(*ii - prev + 0.5);
        prev = *ii + 0.5;
    }

    std::vector<std::vector<int> > groups;
    groupbyAdjacent(deltas.begin(), deltas.end(), &groups);

    for (std::vector<std::vector<int> >::const_iterator kk = groups.begin();
            kk != groups.end(); ++kk) {
        uint64_t sum = std::accumulate(kk->begin(), kk->end(), 0ULL);
        double average = static_cast<double>(sum) / kk->size();
        opt.averages.push_back(std::make_pair(kk->size(), average));
    }
    std::fprintf(stderr, "Divided into %d group%s\n",
            int(groups.size()), (groups.size() == 1) ? "" : "s");
    for (size_t i = 0; i < opt.averages.size(); ++i) {
        std::fprintf(stderr, "%d frames: time delta %g\n",
                int(opt.averages[i].first), opt.averages[i].second);
    }

    tc.clear();
    tc.push_back(0.0);
    for (size_t i = 0; i < groups.size(); ++i) {
        for (size_t j = 0; j < groups[i].size(); ++j)
            tc.push_back(tc.back() + opt.averages[i].second);
    }
}

void rescaleTimecode(Option &opt)
{
    size_t n = opt.timecodes.size();
    if (n < 2) return;
    double delta = opt.timecodes[n-1] - opt.timecodes[n-2];
    double duration = opt.timecodes[n-1] + delta;
    double scale = 1;

    if (opt.requestedTimeScale == 0) {
        double scaleMax = 0x7fffffff / duration;
        if (scaleMax < 1.0) {
            while (scale > scaleMax)
                scale /= 10.0;
        } else if (opt.timeScale < 100) {
            scaleMax = std::min(scaleMax, 10000.0 / opt.timeScale);
            while (scale < scaleMax)
                scale *= 10.0;
            scale /= 10.0;
        }
    }
    else if (opt.requestedTimeScale > 0)
        scale = double(opt.requestedTimeScale) / opt.timeScale;
    else
        scale = double(opt.originalTimeScale) / opt.timeScale;

    for (size_t i = 0; i < n; ++i)
       opt.timecodes[i] *= scale;
    opt.timeScale *= scale;
}

void parseTimecodeV2(Option &opt, std::istream &is, size_t count)
{
    std::string line;
    bool is_float = false;
    size_t nline = 0;
    while (opt.timecodes.size() < count && std::getline(is, line)) {
        ++nline;
        if (line.size() && line[0] == '#')
            continue;
        double stamp;
        if (std::strchr(line.c_str(), '.')) is_float = true;
        if (std::sscanf(line.c_str(), "%lf", &stamp) == 1) {
            if (opt.timecodes.size() && stamp <= opt.timecodes.back()) {
                std::stringstream msg;
                msg << "Timecode is not monotone increasing! at line " << nline;
                throw std::runtime_error(msg.str());
            }
            opt.timecodes.push_back(stamp);
        }
    }
    if (!opt.timecodes.size())
        throw std::runtime_error("No entry in the timecode file");
    if (opt.timecodes.size() == count -1 && opt.timecodes.size() >= 2) {
        double last = opt.timecodes[opt.timecodes.size()-1];
        double prev = opt.timecodes[opt.timecodes.size()-2];
        opt.timecodes.push_back(last * 2 - prev);
    }
    if (opt.optimizeTimecode)
        averageTimecode(opt);
}

#ifdef _WIN32
void loadTimecodeV2(Option &option, size_t count)
{
    std::wstring wfname = m2w(option.timecodeFile, utf8_codecvt_facet());

    HANDLE fh = CreateFileW(wfname.c_str(), GENERIC_READ,
        FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (fh == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Can't open timecode file");

    DWORD nread;
    char buffer[8192];
    std::stringstream ss;
    while (ReadFile(fh, buffer, sizeof buffer, &nread, 0) && nread > 0)
        ss.write(buffer, nread);
    CloseHandle(fh);

    ss.seekg(0);
    parseTimecodeV2(option, ss, count);
}
#else
void loadTimecodeV2(Option &option, size_t count)
{
    std::ifstream ifs(option.timecodeFile);
    if (!ifs)
        throw std::runtime_error("Can't open timecode file");
    parseTimecodeV2(option, ifs, count);
}
#endif

void printTimeCodes(const Option &opt, TrackEditor &track)
{
#ifdef _WIN32
    utf8_codecvt_facet u8codec;
    std::wstring wname = m2w(opt.timecodeFile, utf8_codecvt_facet());
    FILE *fp = std::strcmp(opt.timecodeFile, "-")
               ? _wfopen(wname.c_str(), L"w")
               : stdout;
#else
    FILE *fp = std::strcmp(opt.timecodeFile, "-")
               ? std::fopen(opt.timecodeFile, "w")
               : stdout;
#endif
    if (!fp)
        throw std::runtime_error("Can't open timecode file");
    uint32_t timeScale = track.GetTimeScale();
    std::fputs("# timecode format v2\n", fp);
    if (track.GetFrameCount()) {
        uint64_t off = track.CTS(0);
        for (size_t i = 0; i < track.GetFrameCount(); ++i) {
            uint64_t cts = track.CTS(i) - off;
            std::fprintf(fp, "%.15g\n",
                static_cast<double>(cts) / timeScale * 1000.0);
        }
    }
    std::fclose(fp);
}

std::list<double> fixAudioTimeCodes(Option& opt, mp4v2::impl::MP4File &file, TrackEditor &vtrackeditor)
{
    MP4TrackId atrackId = file.FindTrackId(0, MP4_AUDIO_TRACK_TYPE);
    MP4TrackX* atrack = reinterpret_cast<MP4TrackX*>(file.GetTrack(atrackId));
    double timescale = atrack->GetTimeScale();
    TrackEditor aeditor(reinterpret_cast<MP4TrackX*>(atrack));
    size_t cnt = aeditor.GetFrameCount();
    std::list<double> vctsList, fixedVctsList;
    for (size_t i = 0; i < vtrackeditor.GetFrameCount() + 1; ++i)
        vctsList.push_back(vtrackeditor.CTS(i));

    int64_t dts_diff = 0;
    const double alpha = 0.2;
    double timeDelta = 0;
    for (size_t i = 1; i <= cnt; ++i) {
        double dts = aeditor.DTS(i);
        double fixed = opt.audioTimeDelta * i;
        while (vctsList.size()) {
            double vcts = vctsList.front();
            if (vcts * aeditor.GetTimeScale() / vtrackeditor.GetTimeScale() > dts) break;
            double newvcts = vcts * fixed / dts;
            if (!fixedVctsList.size()) {
                fixedVctsList.push_back(newvcts);
            } else if (timeDelta == 0) {
                timeDelta = newvcts - fixedVctsList.back();
                fixedVctsList.push_back(newvcts);
            } else  {
                double lastCts = fixedVctsList.back();
                timeDelta = alpha * (newvcts - lastCts) + (1.0 - alpha) * timeDelta;
                fixedVctsList.push_back(lastCts + timeDelta);
            }
            dts_diff = newvcts - vcts;
            vctsList.pop_front();
        }
    }
    while (vctsList.size()) {
        double vcts = vctsList.front();
        fixedVctsList.push_back(vcts + dts_diff);
        vctsList.pop_front();
    }

    int nstts = atrack->SttsCountProperty()->GetValue();
    atrack->SttsCountProperty()->IncrementValue(-1 * nstts);
    atrack->SttsSampleCountProperty()->SetCount(0);
    atrack->SttsSampleDeltaProperty()->SetCount(0);
    atrack->SttsCountProperty()->IncrementValue();
    atrack->SttsSampleCountProperty()->AddValue(cnt);
    atrack->SttsSampleDeltaProperty()->AddValue(opt.audioTimeDelta);
    atrack->MediaDurationProperty()->SetValue(0);
    atrack->UpdateDurationsX(cnt * opt.audioTimeDelta);

    return fixedVctsList;
}

void execute(Option &opt)
{
    try {
        //mp4v2::impl::log.setVerbosity(MP4_LOG_VERBOSE3);
        mp4v2::impl::log.setVerbosity(MP4_LOG_NONE);
        mp4v2::impl::MP4File file;
        std::fprintf(stderr, "Reading MP4 stream...\n");
        if (opt.inplace)
            file.Modify(opt.src, 0, 0);
        else
            file.Read(opt.src, 0, 0, 0);
        std::fprintf(stderr, "Done reading\n");
        MP4TrackId trackId = file.FindTrackId(0, MP4_VIDEO_TRACK_TYPE);
        mp4v2::impl::MP4Track *track = file.GetTrack(trackId);
        // XXX
        TrackEditor editor(reinterpret_cast<MP4TrackX*>(track));
        opt.originalTimeScale = editor.GetTimeScale();
        if (opt.printOnly) {
            printTimeCodes(opt, editor);
            return;
        }
        editor.SetAudioDelay(opt.audioDelay);
        if (opt.compressDTS)
            editor.EnableDTSCompression(true);
        if (opt.ranges.size())
            editor.SetFPS(&opt.ranges[0], opt.ranges.size(),
                          opt.requestedTimeScale);
        else if (opt.timecodeFile || opt.modified()) {
            if (opt.timecodeFile)
                loadTimecodeV2(opt, editor.GetFrameCount() + 1);
            else if (opt.audioTimeDelta > 0) {
                std::list<double> vctsList = fixAudioTimeCodes(opt, file, editor);
                opt.timeScale = opt.originalTimeScale;
                for (auto it = vctsList.begin(); it != vctsList.end(); ++it) {
                    opt.timecodes.push_back(*it);
                }
                if (opt.optimizeTimecode)
                    averageTimecode(opt);
            }
            else {
                uint64_t off = editor.CTS(0);
                opt.timeScale = opt.originalTimeScale;
                for (size_t i = 0; i < editor.GetFrameCount() + 1; ++i)
                    opt.timecodes.push_back(editor.CTS(i) - off);
                if (opt.optimizeTimecode)
                    averageTimecode(opt);
            }
            if (opt.optimizeTimecode && convertToExactRanges(opt))
                editor.SetFPS(&opt.ranges[0], opt.ranges.size(),
                              opt.requestedTimeScale);
            else {
                rescaleTimecode(opt);
                editor.SetTimeCodes(&opt.timecodes[0],
                        opt.timecodes.size(),
                        opt.timeScale);
            }
        }
        if (opt.modified()) {
            editor.AdjustTimeCodes();
            editor.DoEditTimeCodes();
        }
        if (opt.inplace)
            file.Close();
        else {
            std::fprintf(stderr, "Saving MP4 stream...\n");
            MP4FileCopy copier(&file);
            copier.start(opt.dst);
            uint64_t count = copier.getTotalChunks();
            for (uint64_t i = 1; copier.copyNextChunk(); ++i) {
                std::fprintf(stderr, "\rWriting chunk %" PRId64 "/%" PRId64 "...",
                        i, count);
            }
        }
        std::fprintf(stderr, "\nOperation completed with no problem\n");
    } catch (mp4v2::impl::Exception *e) {
        handle_mp4error(e);
    }
}

const char *getversion();

void usage()
{
    std::fprintf(stderr,
"mp4fpsmod %s\n"
"(libmp4v2 " MP4V2_PROJECT_version ")\n"
"usage: mp4fpsmod [options] FILE\n"
"  -o <file>             Specify MP4 output filename.\n"
"  -i, --inplace         Edit in-place instead of creating a new file.\n"
"  -p, --print <file>    Output current timecodes into timecode-v2 format.\n"
"  -t, --tcfile <file>   Edit timecodes with timecode-v2 file.\n"
"  -x, --optimize        Optimize timecode\n"
"  -r, --fps <nframes:fps>\n"
"                        Edit timecodes with the spec.\n"
"                        You can specify -r more than two times, to produce\n"
"                        VFR movie.\n"
"                        \"nframes\" is number of frames, which \"fps\" is\n"
"                        aplied to.\n"
"                        0 as nframes means \"rest of the movie\"\n"
"                        \"fps\" is a rational or integer.\n"
"                        For example, 25 or 30000/1001.\n"
"  -c, --compress-dts    Enable DTS compression.\n"
"  -d, --delay <n>       Delay audio by n millisecond.\n"
"  -T, --timescale <keep|n>\n"
"                        keep: Keep original timescale.\n"
"                        n: Set timescale of videotrack to n.\n"
"  -A, --static-audio-timedelta <n>\n"
"                        Make timedelta of audio track static.\n"
"                        Also modify video timestamps to keep them in sync\n"
    , getversion());
    std::exit(1);
}

static struct option long_options[] = {
    { "inplace", no_argument, 0, 'i' },
    { "print", required_argument, 0, 'p' },
    { "fps", required_argument, 0, 'r' },
    { "tcfile", required_argument, 0, 't' },
    { "delay", required_argument, 0, 'd' },
    { "optimize", no_argument, 0, 'x' },
    { "compress-dts", no_argument, 0, 'c' },
    { "keep-timescale", no_argument, 0, 'k' },
    { "timescale", required_argument, 0, 'T' },
    { 0, 0, 0, 0 }
};

int main1(int argc, char **argv)
{
    try {
        std::setbuf(stderr, 0);

        Option option;
        int ch;

        while ((ch = getopt_long(argc, argv, "io:p:r:t:d:T:A:xcQ",
                        long_options, 0)) != EOF) {
            if (ch == 'i') {
                option.inplace = true;
            } else if (ch == 'r') {
                int nframes, num, denom = 1;
                if (std::sscanf(optarg, "%d:%d/%d", &nframes, &num, &denom) < 2)
                    usage();
                FPSRange range = { (uint32_t)nframes, num, denom };
                option.ranges.push_back(range);
            } else if (ch == 'p') {
                option.printOnly = true;
                option.timecodeFile = optarg;
            } else if (ch == 't') {
                option.timecodeFile = optarg;
            } else if (ch == 'd') {
                int delay;
                if (std::sscanf(optarg, "%d", &delay) != 1)
                    usage();
                option.audioDelay = delay;
            } else if (ch == 'o') {
                option.dst = optarg;
            } else if (ch == 'x') {
                option.optimizeTimecode = true;
            } else if (ch == 'c') {
                option.compressDTS = true;
            } else if (ch == 'T') {
                if (!std::strcmp(optarg, "keep"))
                    option.requestedTimeScale = -1;
                else {
                    unsigned n;
                    if (std::sscanf(optarg, "%u", &n) != 1)
                        usage();
                    option.requestedTimeScale = n;
                }
            } else if (ch == 'A') {
                int delta;
                if (std::sscanf(optarg, "%d", &delta) != 1)
                    usage();
                option.audioTimeDelta = delta;
            }
        }
        argc -= optind;
        argv += optind;
        if (argc == 0 || (!option.printOnly && !option.inplace && !option.dst))
            usage();
        if (!option.printOnly && option.timecodeFile && option.ranges.size()) {
            fprintf(stderr, "-t and -r are exclusive\n");
            return 1;
        }
        option.src = argv[0];
        execute(option);
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
}

#if defined(_WIN32)
int wmain1(int argc, wchar_t **argv)
{
    utf8_codecvt_facet codec;
    std::vector<std::string> args;
    std::vector<char*> cargs;
    for (int i = 0; i < argc; ++i)
        args.push_back(w2m(argv[i], codec));
    for (std::vector<std::string>::const_iterator ii = args.begin();
        ii != args.end(); ++ii)
        cargs.push_back(const_cast<char*>(ii->c_str()));
    cargs.push_back(0);
    return main1(argc, &cargs[0]);
}

int main()
{
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int rc = wmain1(argc, argv);
    GlobalFree(argv);
    return rc;
}
#else
int main(int argc, char **argv)
{
    return main1(argc, argv);
}
#endif
