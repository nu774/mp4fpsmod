#include <cstdio>
#include <fstream>
#include <sstream>
#include <numeric>
#if defined(_WIN32)
#include "utf8_codecvt_facet.hpp"
#include "strcnv.h"
#endif
#ifdef _MSC_VER
#include "getopt.h"
#endif
#include "mp4filex.h"
#include "mp4trackx.h"

struct Option {
    const char *src;
    const char *dst;
    FPSRange *ranges;
    size_t nranges;
    const char *timecode_file;
    bool optimize_timecode;
    std::vector<double> timecodes;
    uint32_t time_scale;
};

/*
 * divide sequence like 1, 2, 1, 1, 2, 3, 2, 3, 3, 2, 1, 1, 2
 * into 
 * 1, 2, 1, 1, 2
 * and
 * 3, 2, 3, 3, 2
 * and
 * 1, 1, 2
 *
 * each group can hold continuous numbers within [n, n + 1] for some n.
 */
template <typename T, typename InputIterator>
void groupby_adjacent(InputIterator begin, InputIterator end,
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

void normalize_timecode(Option &option)
{
    std::vector<double> &tc = option.timecodes;
    std::vector<int> deltas;
    double prev = tc[0];
    for (std::vector<double>::const_iterator ii = ++tc.begin();
	    ii != tc.end(); ++ii) {
	deltas.push_back(static_cast<int>(*ii - prev));
	prev = *ii;
    }

    std::vector<std::vector<int> > groups;
    groupby_adjacent(deltas.begin(), deltas.end(), &groups);

    std::vector<double> averages;
    for (std::vector<std::vector<int> >::const_iterator kk = groups.begin();
	    kk != groups.end(); ++kk) {
	uint64_t sum = std::accumulate(kk->begin(), kk->end(), 0ULL);
	double average = static_cast<double>(sum) / kk->size();
	averages.push_back(average);
    }
    option.time_scale *= 1000;
    tc.clear();
    tc.push_back(0.0);
    for (size_t i = 0; i < groups.size(); ++i) {
	for (size_t j = 0; j < groups[i].size(); ++j)
	    tc.push_back(tc.back() + averages[i] * 1000);
    }
}

void parse_timecode_v2(Option &option, std::istream &is)
{
    std::string line;
    bool is_float = false;
    while (std::getline(is, line)) {
	if (line.size() && line[0] == '#')
	    continue;
	double stamp;
	if (strchr(line.c_str(), '.')) is_float = true;
	if (std::sscanf(line.c_str(), "%lf", &stamp) == 1)
	    option.timecodes.push_back(stamp);
    }
    if (option.optimize_timecode && !is_float && option.timecodes.size())
	normalize_timecode(option);
    else if (is_float) {
	option.time_scale *= 1000;
	for (size_t i = 0; i < option.timecodes.size(); ++i)
	    option.timecodes[i] = option.timecodes[i] * 1000;
    }
}

#ifdef _WIN32
void load_timecode_v2(Option &option)
{
    std::wstring wfname = m2w(option.timecode_file, utf8_codecvt_facet());

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
    parse_timecode_v2(option, ss);
}
#else
void load_timecode_v2(Option &option)
{
    std::ifstream ifs(option.timecode_file);
    if (!ifs)
	throw std::runtime_error("Can't open timecode file");
    parse_timecode_v2(option, ifs);
}
#endif

void execute(Option &option)
{
    try {
	MP4FileX file(2);
	file.Read(option.src, 0);
	MP4TrackId trackId = file.FindTrackId(0, MP4_VIDEO_TRACK_TYPE);
	mp4v2::impl::MP4Atom *trackAtom = file.FindTrackAtom(trackId, 0);
	MP4TrackX track(&file, trackAtom);
	if (option.nranges)
	    track.SetFPS(option.ranges, option.nranges);
	else {
	    load_timecode_v2(option);
	    track.SetTimeCodes(&option.timecodes[0],
		    option.timecodes.size(),
		    option.time_scale);
	}
	file.SaveTo(option.dst);
    } catch (mp4v2::impl::MP4Error *e) {
	handle_mp4error(e);
    }
}

void usage()
{
    std::fputs(
"usage: mp4fpsmod [-r NFRAMES:FPS ] [-t TIMECODE_V2_FILE ] -o DEST SRC\n"
"  -t: Use this option to specify timecodes using timecode v2 file.\n"
"  -x: Use this option to optimize timecode entry in timecode file.\n"
"  -r: Use this option to specify fps and the range which fps is applied to.\n"
"      You can specify -r option more than two times to produce VFR movie.\n"
"  NFRAMES: integer, number of frames\n"
"  FPS: integer, or fraction value. You can specity FPS like 25 or 30000/1001\n"
	,stderr);
    std::exit(1);
}

int main1(int argc, char **argv)
{
    try {
	int ch;
	std::vector<FPSRange> spec;
	const char *dstname = 0;
	const char *timecode_file = 0;
	bool optimize_timecode = false;

	while ((ch = getopt(argc, argv, "r:t:o:x")) != EOF) {
	    if (ch == 'r') {
		int nframes, num, denom = 1;
		if (sscanf(optarg, "%d:%d/%d", &nframes, &num, &denom) < 2)
		    usage();
		FPSRange range = { nframes, num, denom };
		spec.push_back(range);
	    } else if (ch == 't') {
		timecode_file = optarg;
	    } else if (ch == 'o') {
		dstname = optarg;
	    } else if (ch == 'x') {
		optimize_timecode = true;
	    }
	}
	argc -= optind;
	argv += optind;
	if (argc == 0 || dstname == 0)
	    usage();
	if (spec.size() == 0 && timecode_file == 0)
	    usage();

	Option option;
	option.src = argv[0];
	option.dst = dstname;
	option.ranges = spec.size() ? &spec[0] : 0;
	option.nranges = spec.size();
	option.timecode_file = timecode_file;
	option.optimize_timecode = optimize_timecode;
	option.time_scale = 1000;

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
