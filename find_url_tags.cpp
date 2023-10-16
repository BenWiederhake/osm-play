#include <algorithm> // sort
#include <cassert>
#include <cstdio>
#include <cstring> // strcmp
#include <functional> // greater
#include <string>
#include <unordered_set>
#include <vector>

#include <osmium/io/pbf_input.hpp>
#include <osmium/io/reader_with_progress_bar.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
//#include <osmium/relations/relations_manager.hpp>

//#include "relation_list.hpp"

//static const char* const INPUT_FILENAME = "/scratch/osm/bochum_6.99890,51.38677,7.39913,51.58303_231002.osm.pbf";
static const char* const INPUT_FILENAME = "/scratch/osm/nrwish_6.52237,49.15178,11.43809,51.85567_231002.osm.pbf";
//static const char* const INPUT_FILENAME = "/scratch/osm/europe-latest.osm.pbf";

static const char* const OUTPUT_FILENAME = "/scratch/osm/tags_used_for_urls.lst";

// Detect url-like tags by looking for https-links:
static char const* const STRING_IN_EVERY_URL = "https://";
static const size_t STRING_IN_EVERY_URL_LEN = strlen(STRING_IN_EVERY_URL);
static char const* const LENIENT_STRING_IN_EVERY_URL = "http";
static const size_t LENIENT_STRING_IN_EVERY_URL_LEN = strlen(LENIENT_STRING_IN_EVERY_URL);

bool looks_like_url(char const* const str) {
    return 0 == strncmp(str, STRING_IN_EVERY_URL, STRING_IN_EVERY_URL_LEN);
}

bool lenient_looks_like_url(char const* const str) {
    return 0 == strncmp(str, LENIENT_STRING_IN_EVERY_URL, LENIENT_STRING_IN_EVERY_URL_LEN);
}

class FindUrlHandler : public osmium::handler::Handler {
public:
    void any_object(osmium::OSMObject const& obj) {
        for (auto const& tag : obj.tags()) {
            if (!looks_like_url(tag.value())) {
                // Doesn't start with "https://".
                continue;
            }
            tags_used.emplace(tag.key());
        }
    }

    void way(const osmium::Way& way) {
        any_object(way);
    }

    void node(const osmium::Node& node) {
        any_object(node);
    }

    void relation(const osmium::Relation& relation) {
        any_object(relation);
    }

    std::unordered_set<std::string> tags_used {};
};

class StatsEntry {
public:
    size_t tag_seen_with_https {0};
    size_t tag_seen_with_url_lenient {0};
    size_t tag_seen_without_url {0};
};

class UrlStatsHandler : public osmium::handler::Handler {
public:
    void any_object(osmium::OSMObject const& obj) {
        for (auto const& tag : obj.tags()) {
            auto it = stats.find(tag.key());
            if (it == stats.end()) {
                continue;
            }
            if (looks_like_url(tag.value())) {
                it->second.tag_seen_with_https += 1;
            } else if (lenient_looks_like_url(tag.value())) {
                // Doesn't start with "https://".
                it->second.tag_seen_with_url_lenient += 1;
            } else {
                // Doesn't start with "https://".
                it->second.tag_seen_without_url += 1;
            }
        }
    }

    void way(const osmium::Way& way) {
        any_object(way);
    }

    void node(const osmium::Node& node) {
        any_object(node);
    }

    void relation(const osmium::Relation& relation) {
        any_object(relation);
    }

    std::unordered_map<std::string, StatsEntry> stats {};
};

int main() {
    printf("Running on %s\n", INPUT_FILENAME);
    printf("Pass 1: Finding relevant tags …\n");
    FindUrlHandler find_handler;
    {
        osmium::io::ReaderWithProgressBar reader{true, INPUT_FILENAME, osmium::osm_entity_bits::all};
        osmium::apply(reader, find_handler);
        reader.close();
    }

    printf("    Found %lu relevant tags. Preparing second pass …\n", find_handler.tags_used.size());
    UrlStatsHandler stats_handler;
    for (auto it = find_handler.tags_used.begin(); it != find_handler.tags_used.end(); it = find_handler.tags_used.erase(it)) {
        stats_handler.stats.insert({*it, StatsEntry{}});
    }

    printf("Pass 2: Counting stats for relevant tags …\n");
    {
        osmium::io::ReaderWithProgressBar reader{true, INPUT_FILENAME, osmium::osm_entity_bits::all};
        osmium::apply(reader, stats_handler);
        reader.close();
    }

    printf("Done counting. Writing to %s …\n", OUTPUT_FILENAME);
    FILE* fp = fopen(OUTPUT_FILENAME, "w");
    assert(fp != nullptr);
    fprintf(fp, "0TAG\t0NUM_HTTPS\t0NUM_HTTP_LENIENT\t0NUM_WEIRD\t0FRACTION_LENIENT\n");
    for (auto const& item : stats_handler.stats) {
        auto const& stats = item.second;
        double fraction = (stats.tag_seen_with_https + stats.tag_seen_with_url_lenient) * 1.0 / (stats.tag_seen_with_https + stats.tag_seen_with_url_lenient + stats.tag_seen_without_url);
        fprintf(fp, "%s\t%lu\t%lu\t%lu\t%f\n", item.first.c_str(), stats.tag_seen_with_https, stats.tag_seen_with_url_lenient, stats.tag_seen_without_url, fraction);
    }
    fclose(fp);

    printf("All done!\n");
    return 0;
}

// Manual operations on the resulting data:
// - Remove tags that have <= 5 proper "https://" links
// - Remove tags that >= 10k "weird" links. These are mostly tags with <5% lenient links, and we intentionally include "note:de" (16% lenient links, rest is prose text) and "image" (72% lenient links, rest is apparently filenames in wikimedia)
//   Note: This implies that we should lint "image" for precisely that, since wikimedia files are prefixed by "File:", and existence of these files can be resonably easily checked.
// - Remove tags that have <= 45% lenient links. This takes care of most "source:*" tags, but keeps the "internet" tag (which is sometimes supposed to be a "website" tag).
// - Manually remove the following tags, as they are too noisy anyway, or used mostly for non-https-things:
//    * architect:wikipedia ("de:John Doe")
//    * closed:website (probably offline anyway)
//    * contact:facebook (usernames)
//    * contact:google_plus (usernames)
//    * contact:instagram (usernames)
//    * contact:linkedin (usernames)
//    * contact:pinterest (usernames)
//    * contact:tiktok (usernames)
//    * contact:twitter (usernames)
//    * contact:xing (usernames)
//    * contact:youtube (usernames)
//    * disused:contact:facebook (usernames; probably offline anyway)
//    * disused:website (probably offline anyway)
//    * old_website (probably offline anyway)
//    * removed:contact:facebook (usernames; probably offline anyway)
//    * removed:contact:instagram (usernames; probably offline anyway)
//    * source_ref ("interpolation", "extrapolation", "sign", "video", … same with all source:* tags)
//    * source:ref
//    * source:access
//    * source:amenity
//    * source:destination
//    * source:electrified
//    * source:end_date
//    * source:proposed:name
//    * source:railway:position
//    * source:railway:ref
//    * source:shop
//    * video (The 5249 https-links are actually spam, and the 1641 "weird" contents are the meaningsful strings "yes" and "no".)
//   Note: We should lint these tags, especially wiki pages and tags with well-known "normal" values.

// Remaining tags:
// - brand:website
// - contact:atom
// - contact:lieferando
// - contact:rss
// - contact:takeaway
// - contact:url
// - contact:vimeo
// - contact:webcam
// - contact:website
// - destination:url
// - disused:contact:website
// - facebook
// - fee:source
// - flickr
// - heritage:website
// - image:0
// - image:streetsign
// - image2
// - inscription:url
// - instagram
// - internet
// - market:flea_market:opening_hours:url
// - memorial:website
// - menu:url
// - name:etymology:website
// - network:website
// - note:url
// - opening_hours:url
// - operator:website
// - osmwiki
// - picture
// - post_office:website
// - railway:source
// - removed:contact:twitter
// - removed:contact:website
// - removed:contact:youtube
// - removed:website
// - source_2
// - source_url
// - source:1
// - source:2
// - source:3
// - source:heritage
// - source:image
// - source:office
// - source:old_ref
// - source:operator
// - source:payment:contactless
// - source:phone
// - source:railway:radio
// - source:railway:speed_limit_distant:speed
// - source:railway:speed_limit:speed
// - source:ref
// - source:website
// - source2
// - symbol:url
// - url
// - url:official
// - url:timetable
// - video_2
// - was:website
// - webcam
// - website
// - website_1
// - website:booking
// - website:DDB
// - website:en
// - website:LfDH
// - website:menu
// - website:orders
// - website:regulation
// - website:stock
// - website:VDMT
// - website2
// - xmas:url
// Some of these should probably not be in use, at all.
