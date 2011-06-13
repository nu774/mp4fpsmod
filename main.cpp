#include <cstdio>
#include <cmath>
#include <fstream>
#include <sstream>
#include <numeric>
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

struct Option {
    const char *src, *dst, *timecodeFile;
    bool compressDTS;
    bool optimizeTimecode;
    bool printOnly;
    uint32_t timeScale;
    int audioDelay;
    std::vector<FPSRange> ranges;
    std::vector<double> timecodes;

    Option()
    {
	src = 0;
	dst = 0;
	timecodeFile = 0;
	compressDTS = false;
	optimizeTimecode = false;
	printOnly = false;
	timeScale = 1000;
	audioDelay = 0;
    }
    bool modified() {
	return compressDTS || audioDelay || ranges.size()
	    || timecodes.size() || optimizeTimecode;
    }
};

bool convertToExactRanges(Option &opt)
{
    struct FPSSpec {
	double delta;
	int num, denom;
    } wellKnown[] = {
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
    double prev = opt.timecodes[0];
    for (std::vector<double>::const_iterator dp = ++opt.timecodes.begin();
	    dp != opt.timecodes.end(); ++dp) {
	double delta = *dp - prev;
	for (sp = wellKnown; sp != spEnd; ++sp) {
	    /* test if it's close enough to one of the well known rate. */
	    double diff = std::abs(delta - sp->delta);
	    if (diff / delta < 0.00048828125) {
		if (ranges.size() && ranges.back().fps_num == sp->num)
		    ++ranges.back().numFrames;
		else {
		    FPSRange range = { 1, sp->num, sp->denom };
		    ranges.push_back(range);
		}
		if (sp != wellKnown) {
		    /* move matched spec to the first position */
		    FPSSpec tmp = *sp;
		    *sp = wellKnown[0];
		    wellKnown[0] = tmp;
		}
		break;
	    }
	}
	if (sp == spEnd)
	    return false;
	prev = *dp;
    }
    ++ranges.back().numFrames;
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
    double prev = tc[0];
    for (std::vector<double>::const_iterator ii = ++tc.begin();
	    ii != tc.end(); ++ii) {
	deltas.push_back(static_cast<int>(*ii - prev));
	prev = *ii;
    }

    std::vector<std::vector<int> > groups;
    groupbyAdjacent(deltas.begin(), deltas.end(), &groups);

    std::vector<double> averages;
    for (std::vector<std::vector<int> >::const_iterator kk = groups.begin();
	    kk != groups.end(); ++kk) {
	uint64_t sum = std::accumulate(kk->begin(), kk->end(), 0ULL);
	double average = static_cast<double>(sum) / kk->size();
	averages.push_back(average);
    }
    std::fprintf(stderr, "Divided into %d group%s\n",
	    groups.size(), (groups.size() == 1) ? "" : "s");
    for (size_t i = 0; i < groups.size(); ++i) {
	std::fprintf(stderr, "%d frames: time delta %g\n",
		groups[i].size(), averages[i]);
    }

    tc.clear();
    tc.push_back(0.0);
    for (size_t i = 0; i < groups.size(); ++i) {
	for (size_t j = 0; j < groups[i].size(); ++j)
	    tc.push_back(tc.back() + averages[i]);
    }
}

void rescaleTimecode(Option &opt)
{
    size_t n = opt.timecodes.size();
    double delta = opt.timecodes[n-1] - opt.timecodes[n-2];
    double duration = opt.timecodes[n-1] + delta;

    double scale = 1;
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
    for (size_t i = 0; i < n; ++i)
       opt.timecodes[i] *= scale;
    opt.timeScale *= scale;
}

void parseTimecodeV2(Option &opt, std::istream &is)
{
    std::string line;
    bool is_float = false;
    while (std::getline(is, line)) {
	if (line.size() && line[0] == '#')
	    continue;
	double stamp;
	if (std::strchr(line.c_str(), '.')) is_float = true;
	if (std::sscanf(line.c_str(), "%lf", &stamp) == 1)
	    opt.timecodes.push_back(stamp);
    }
    if (!opt.timecodes.size())
	throw std::runtime_error("No entry in the timecode file");
    if (opt.optimizeTimecode && !is_float)
	averageTimecode(opt);
    if ((opt.optimizeTimecode || is_float) && opt.timecodes.size() > 1)
	rescaleTimecode(opt);
}

#ifdef _WIN32
void loadTimecodeV2(Option &option)
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
    parseTimecodeV2(option, ss);
}
#else
void loadTimecodeV2(Option &option)
{
    std::ifstream ifs(option.timecodeFile);
    if (!ifs)
	throw std::runtime_error("Can't open timecode file");
    parseTimecodeV2(option, ifs);
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

void execute(Option &opt)
{
    try {
	mp4v2::impl::log.setVerbosity(MP4_LOG_NONE);
	MP4FileX file;
	std::fprintf(stderr, "Reading MP4 stream...\n");
	file.Read(opt.src, 0);
	std::fprintf(stderr, "Done reading\n");
	MP4TrackId trackId = file.FindTrackId(0, MP4_VIDEO_TRACK_TYPE);
	mp4v2::impl::MP4Track *track = file.GetTrack(trackId);
	// XXX
	TrackEditor editor(reinterpret_cast<MP4TrackX*>(track));
	if (opt.printOnly) {
	    printTimeCodes(opt, editor);
	    return;
	}
	editor.SetAudioDelay(opt.audioDelay);
	if (opt.compressDTS)
	    editor.EnableDTSCompression(true);
	if (opt.ranges.size())
	    editor.SetFPS(&opt.ranges[0], opt.ranges.size());
	else if (opt.timecodeFile || opt.modified()) {
	    if (opt.timecodeFile)
    		loadTimecodeV2(opt);
	    else {
		uint64_t off = editor.CTS(0);
		opt.timeScale = editor.GetTimeScale();
		for (size_t i = 0; i < editor.GetFrameCount(); ++i)
		    opt.timecodes.push_back(editor.CTS(i) - off);
		if (opt.optimizeTimecode)
		    averageTimecode(opt);
		rescaleTimecode(opt);
	    }
	    if (opt.optimizeTimecode && convertToExactRanges(opt))
		editor.SetFPS(&opt.ranges[0], opt.ranges.size());
	    else
		editor.SetTimeCodes(&opt.timecodes[0],
			opt.timecodes.size(),
			opt.timeScale);
	}
	if (opt.modified()) {
	    editor.AdjustTimeCodes();
	    editor.DoEditTimeCodes();
	}
	std::fprintf(stderr, "Saving MP4 stream...\n");
	file.SaveTo(opt.dst);
	std::fprintf(stderr, "Operation completed with no problem\n");
    } catch (mp4v2::impl::Exception *e) {
	handle_mp4error(e);
    }
}

const char *getversion();

void usage()
{
    std::fprintf(stderr,
"mp4fpsmod %s\n"
"usage: mp4fpsmod [options] FILE\n"
"  -o <file>             Specify MP4 output filename.\n"
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
    , getversion());
    std::exit(1);
}

static struct option long_options[] = {
    { "print", required_argument, 0, 'p' },
    { "fps", required_argument, 0, 'r' },
    { "tcfile", required_argument, 0, 't' },
    { "delay", required_argument, 0, 'd' },
    { "optimize", no_argument, 0, 'x' },
    { "compress-dts", no_argument, 0, 'c' },
    { 0, 0, 0, 0 }
};

int main1(int argc, char **argv)
{
    try {
	std::setbuf(stderr, 0);

	Option option;
	int ch;

	while ((ch = getopt_long(argc, argv, "o:p:r:t:d:xcQ",
			long_options, 0)) != EOF) {
	    if (ch == 'r') {
		int nframes, num, denom = 1;
		if (std::sscanf(optarg, "%d:%d/%d", &nframes, &num, &denom) < 2)
		    usage();
		FPSRange range = { nframes, num, denom };
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
	    }
	}
	argc -= optind;
	argv += optind;
	if (argc == 0 || (!option.printOnly && option.dst == 0))
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
