#include <cstdio>
#if defined(_WIN32)
#include "utf8_codecvt_facet.hpp"
#include "strcnv.h"
#endif
#ifdef _MSC_VER
#include "getopt.h"
#endif
#include "mp4filex.h"
#include "mp4trackx.h"

void
execute(const char *src, const char *dst, FPSRange *ranges, size_t nranges)
{
    try {
	MP4FileX file(0);
	file.Read(src, 0);
	MP4TrackId trackId = file.FindTrackId(0, MP4_VIDEO_TRACK_TYPE);
	mp4v2::impl::MP4Atom *trackAtom = file.FindTrackAtom(trackId, 0);
	MP4TrackX track(&file, trackAtom);
	track.SetFPS(ranges, nranges);
	file.SaveTo(dst);
    } catch (mp4v2::impl::MP4Error *e) {
	handle_mp4error(e);
    }
}

void usage()
{
    std::fputs(
	"usage: mp4fpsmod [-r NFRAMES:FPS ] -o DSTFILENAME FILE\n"
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
	while ((ch = getopt(argc, argv, "r:o:")) != EOF) {
	    if (ch == 'r') {
		int nframes, num, denom = 1;
		if (sscanf(optarg, "%d:%d/%d", &nframes, &num, &denom) < 2)
		    usage();
		FPSRange range = { nframes, num, denom };
		spec.push_back(range);
	    } else if (ch == 'o') {
		dstname = optarg;
	    }
	}
	argc -= optind;
	argv += optind;
	if (argc == 0 || spec.size() == 0 || dstname == 0)
	    usage();
	execute(argv[0], dstname, &spec[0], spec.size());
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
