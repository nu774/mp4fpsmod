#include <cstdio>
#include <fstream>
#include <sstream>
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
};

void parse_timecode_v2(std::istream &is, std::vector<double> *timeCodes)
{
    std::string line;
    while (std::getline(is, line)) {
	if (line.size() && line[0] == '#')
	    continue;
	double stamp;
	if (std::sscanf(line.c_str(), "%lf", &stamp) == 1)
	    timeCodes->push_back(stamp * 1000);
    }
}

#ifdef _WIN32
void load_timecode_v2(const char *fname, std::vector<double> *timeCodes)
{
    std::wstring wfname = m2w(fname, utf8_codecvt_facet());

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
    parse_timecode_v2(ss, timeCodes);
}
#else
void load_timecode_v2(const char *fname, std::vector<double> *timeCodes)
{
    std::ifstream ifs(fname);
    if (!ifs)
	throw std::runtime_error("Can't open timecode file");
    parse_timecode_v2(ifs, timeCodes);
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
	    std::vector<double> timeCodes;
	    load_timecode_v2(option.timecode_file, &timeCodes);
	    track.SetTimeCodes(&timeCodes[0], timeCodes.size(), 1000 * 1000);
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
	while ((ch = getopt(argc, argv, "r:t:o:")) != EOF) {
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
