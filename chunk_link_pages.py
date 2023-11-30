#!/usr/bin/env python3

# RUN IN THIS ORDER:
# osmium extract -s simple --bbox 1.234,2.345,3.456,4.567 -o foo.osm.pbf europe-latest.osm.pbf
# rm -rf build/website_pages/ && mkdir build/website_pages/ && ./chunk_link_pages.py foo.osm.pbf

import collections
import osmium
import sys

OUTPUT_DIRECTORY = "build/website_pages/"
OUTPUT_FILENAME_FORMAT = "website_node_{:04}.html"
OUTPUT_FILENAME_SKIP_KEYS = "website_node_{:04}.html"
INPUT_FILENAME_SKIP_KEYS = "/scratch/osm/url_known_false_positives.lst"
LINKS_PER_PAGE = 50
PROCESS_DEBUG_STEP = 10000
WEBSITE_TAGS = [
    "brand:website",
    "contact:lieferando",
    "contact:takeaway",
    "contact:url",
    "contact:vimeo",
    "contact:webcam",
    "contact:website",
    "destination:url",
    "facebook",
    "fee:source",
    "flickr",
    "heritage:website",
    "image:0",
    "image2",
    "image:streetsign",
    "inscription:url",
    "instagram",
    "internet",
    "market:flea_market:opening_hours:url",
    "memorial:website",
    "menu:url",
    "name:etymology:website",
    "network:website",
    "note:url",
    "opening_hours:url",
    "operator:website",
    "osmwiki",
    "picture",
    "post_office:website",
    "rail_trail:website",  # Manual find
    "railway:source",
    "source:1",
    "source:2",
    "source_2",
    "source2",
    "source:3",
    "source:heritage",
    "source:image",
    "source:office",
    "source:old_ref",
    "source:operator",
    "source:payment:contactless",
    "source:phone",
    "source:railway:radio",
    "source:railway:speed_limit_distant:speed",
    "source:railway:speed_limit:speed",
    "source:ref",
    "source_url",
    "source:url",  # Manual find
    "source:website",
    "symbol:url",
    "url",
    "url:official",
    "url:timetable",
    "video_2",
    "webcam",
    "website",
    "website_1",
    "website2",
    "website:booking",
    "website:DDB",
    "website:en",
    "website:LfDH",
    "website:menu",
    "website:orders",
    "website:regulation",
    "website:stock",
    "website:VDMT",
    "xmas:url",
]


Entry = collections.namedtuple("Entry", ["item_key", "tag_key", "url", "name"])
# "item_key" is like:
# "node=8422613760"
# "way=16773099"
# "relation=2026048"


def read_skip_keys():
    with open(INPUT_FILENAME_SKIP_KEYS, "r") as fp:
        entries = fp.read().split("\n")
    last_line = entries.pop()
    assert last_line == "", last_line
    return [tuple(key.split(" ", 1)) for key in entries]


class NodeCollector(osmium.SimpleHandler):
    def __init__(self, skip_keys):
        super().__init__()
        self.entries = list()
        self.processed = 0
        self.skip_keys = set(skip_keys)
        self.used_skip_keys = set()

    def visit(self, item_key, tags):
        name = tags.get("name") or item_key
        for website_tag in WEBSITE_TAGS:
            url = tags.get(website_tag)
            if url is None:
                continue
            if not url.startswith("https://"):
                if (item_key, website_tag) in self.skip_keys:
                    self.used_skip_keys.add((item_key, website_tag))
                else:
                    self.entries.append(Entry(item_key, website_tag, url, name))
        self.processed += 1
        if self.processed % PROCESS_DEBUG_STEP == 1:
            self.report_progress()

    def node(self, n):
        self.visit(f"node={n.id}", n.tags)

    def way(self, w):
        self.visit(f"way={w.id}", w.tags)

    def relation(self, r):
        self.visit(f"relation={r.id}", r.tags)

    def report_progress(self):
        print(f"\x1b[A  Processed {self.processed} items, will report >={len(self.entries)} entries.")


def emit_pages(entries):
    total_pages = (len(entries) + LINKS_PER_PAGE - 1) // LINKS_PER_PAGE
    for page_id in range(total_pages):
        with open(OUTPUT_DIRECTORY + OUTPUT_FILENAME_FORMAT.format(page_id), "w") as fp:
            page_entries = entries[page_id * LINKS_PER_PAGE : (page_id + 1) * LINKS_PER_PAGE]
            fp.write("<!DOCTYPE html>\n")
            fp.write("<body>\n")
            fp.write(f"<h1>Page {page_id} of {total_pages} (0-indexed)</h1>\n")
            if page_id > 0:
                fp.write(f"<a href=\"{OUTPUT_FILENAME_FORMAT.format(page_id - 1)}\">Previous page</a><br/>\n")
            if page_id < total_pages - 1:
                fp.write(f"<a href=\"{OUTPUT_FILENAME_FORMAT.format(page_id + 1)}\">Next page</a><br/>\n")
            fp.write("<ul>\n")
            for entry in page_entries:
                # FIXME: Escape!
                fp.write(f"<li><a href=\"{entry.url}\">Visit</a> | <a href=\"https://www.openstreetmap.org/{entry.item_key.replace('=', '/')}\">Inspect</a> | <a href=\"https://www.openstreetmap.org/edit?{entry.item_key}\">Edit</a> | {entry.name} (Tag {entry.tag_key})</li>\n")
            fp.write("</ul>\n")
            fp.write("</body>\n")
            if page_id > 0:
                fp.write(f"<a href=\"{OUTPUT_FILENAME_FORMAT.format(page_id - 1)}\">Previous page</a><br/>\n")
            if page_id < total_pages - 1:
                fp.write(f"<a href=\"{OUTPUT_FILENAME_FORMAT.format(page_id + 1)}\">Next page</a><br/>\n")
    print(f"Done! Wrote {total_pages} pages.")
    # https://www.openstreetmap.org/node/320634915


def emit_skip_keys(collector):
    with open(OUTPUT_FILENAME_SKIP_KEYS, "w") as fp:
        for item_key, tag_key in collector.used_skip_keys:
            fp.write(f"{item_key} {tag_key}\n")
        # Can't put a separator here easily :(
        for entry in collector.entries:
            fp.write(f"{entry.item_key} {entry.tag_key}\n")


def warn_unused_skip_keys(collector):
    unused_skip_keys = collector.skip_keys.difference(collector.used_skip_keys)
    if not unused_skip_keys:
        print("  (No unused skip_keys. Good.)")
        return
    print("WARNING: Unused skip_keys in input?!")
    for item_key, tag_key in sorted(unused_skip_keys):
        print(f"{item_key} {tag_key}")


def run(osmfile):
    print("Reading skip_keys …")
    skip_keys = read_skip_keys()
    print(f"  Found {len(skip_keys)} keys that won't be reported.")
    print("Reading pbf …")
    collector = NodeCollector(skip_keys)
    print("  ?")
    collector.apply_file(osmfile)
    collector.report_progress()  # Overwrite last "progress" line with final numbers
    print(f"Emitting {len(collector.entries)} findings …")
    emit_pages(collector.entries)
    print(f"Emitting {len(collector.used_skip_keys) + len(collector.entries)} skip_keys for next invocation …")
    emit_skip_keys(collector)
    warn_unused_skip_keys(collector)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"USAGE: {sys.argv[0]} <osmfile>", file=sys.stderr)
        sys.exit(1)

    run(sys.argv[1])
