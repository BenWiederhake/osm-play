#!/usr/bin/env python3

# The built-in module "struct" cannot deal with mixed endianess in a single read, so we use numpy instead:
# https://stackoverflow.com/a/62168627
import numpy as np
import random

FILENAME_INPUT = "/scratch/osm/simplified-land-polygons-complete-3857/simplified_land_polygons.shp"
FILENAME_OUTPUT = "/scratch/osm/land_polygons.svg"
FILENAME_MAX_DIM = 2000
MERC_PER_DEGREE = 20_037_508.34 / 180  # ?!?!
RELEVANT_BBOX = [0 * MERC_PER_DEGREE, 46 * MERC_PER_DEGREE, 24 * MERC_PER_DEGREE, 68 * MERC_PER_DEGREE]


class ChunkFormat:
    def __init__(self, min_len_bytes, max_len_bytes, names, numpy_format):
        self.min_len_bytes = min_len_bytes
        self.max_len_bytes = max_len_bytes
        assert numpy_format.count(",") + 1 == len(names), "length mismatch"
        self.names = names
        self.numpy_format = numpy_format
        np.dtype(self.numpy_format)  # dry run, fail early

    def decode(self, bytebuf):
        assert self.min_len_bytes <= len(bytebuf) <= self.max_len_bytes
        assert self.min_len_bytes == self.max_len_bytes, "truncation not implemented"
        values = np.frombuffer(bytebuf, dtype=np.dtype(self.numpy_format))[0]
        assert len(values) <= len(self.names)
        if len(bytebuf) == self.max_len_bytes:
            assert len(values) == len(self.names)
        return list(values)


SHAPEFILE_HEADER = ChunkFormat(
    100,
    100,
    [
        # Heavily inspired by https://en.wikipedia.org/wiki/Shapefile#Shapefile_shape_format_(.shp)
        "File code (magic)",
        "unused1", "unused2", "unused3", "unused4", "unused5",
        "file length (shorts)",
        "version",
        "shape type",
        "min x", "min y", "max x", "max y",
        "min z", "max z",
        "min m", "max m",
    ],
    # "<" is little endian, ">" is big endian
    ">u4,>u4,>u4,>u4,>u4,>u4,>u4,<u4,<u4,<f8,<f8,<f8,<f8,<f8,<f8,<f8,<f8",
)

RECORD_HEADER = ChunkFormat(
    12,
    12,
    [
        # Heavily inspired by https://en.wikipedia.org/wiki/Shapefile#Shapefile_shape_format_(.shp)
        "record number (1-based)",
        "record length (shorts)",
        "shape type",
    ],
    ">u4,>u4,<u4",
)

POLYGON_HEADER = ChunkFormat(
    40,
    40,
    [
        # Heavily inspired by https://en.wikipedia.org/wiki/Shapefile#Shapefile_shape_format_(.shp)
        "min x", "min y", "max x", "max y",
        "number of parts",
        "number of points",
    ],
    "<f8,<f8,<f8,<f8,<u4,<u4",
)


def check_header_data(header_data):
    assert header_data[0] == 0x0000270a
    print(f"Shape has {2 * header_data[6]} bytes, 'version' {header_data[7]}, bbox [{header_data[9]},{header_data[10]}] to [{header_data[11]},{header_data[12]}].")
    assert header_data[1] == 0
    assert header_data[2] == 0
    assert header_data[3] == 0
    assert header_data[4] == 0
    assert header_data[5] == 0
    assert header_data[6] == header_data[6]  # TODO: Check file length
    assert header_data[7] == 1000  # Dunno what version 1000 means, but it sounds magic.
    assert header_data[8] == 5  # Can only deal with polygon for now
    # ignore 9, 10, 11, 12 (bbox)
    assert header_data[13] == 0.0
    assert header_data[14] == 0.0
    assert header_data[15] == 0.0
    assert header_data[16] == 0.0
    assert len(header_data) == 17


assert RELEVANT_BBOX[0] < RELEVANT_BBOX[2] and RELEVANT_BBOX[1] < RELEVANT_BBOX[3]


def is_bbox_visible(bbox):
    # min x, min y, max x, max y
    x_visible = bbox[0] <= RELEVANT_BBOX[2] and bbox[2] >= RELEVANT_BBOX[0]
    y_visible = bbox[1] <= RELEVANT_BBOX[3] and bbox[3] >= RELEVANT_BBOX[1]
    return x_visible and y_visible


class Consumer:
    def __init__(self):
        self.fp = None
        self.entered = False
        min_x, min_y, max_x, max_y = RELEVANT_BBOX
        largest_dimension = max(max_x - min_x, max_y - min_y)
        self.offset_x = min_x
        self.offset_y = max_y  # Need to invert y-axis!
        self.px_per_unit = FILENAME_MAX_DIM / largest_dimension
        self.width = (max_x - min_x) * self.px_per_unit
        self.height = (max_y - min_y) * self.px_per_unit
        print(f"{self.offset_x=} {self.offset_y=} {self.px_per_unit=} {self.width=} {self.height=}")

    def __enter__(self):
        assert self.fp is None
        assert not self.entered
        self.fp = open(FILENAME_OUTPUT, "w")
        self.fp.write(f'<svg width="{self.width}" height="{self.height}">\n')
        self.entered = True
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        assert self.fp is not None
        assert self.entered
        self.fp.write("</svg>\n")
        self.fp.flush()  # Probably unnecessary?
        self.fp.close()
        self.fp = None

    def emit_polygon(self, polygon_points):
        assert self.entered
        self.fp.write('<polygon points="')
        for x, y in polygon_points:
            self.fp.write(f" {(x - self.offset_x) * self.px_per_unit},{(self.offset_y - y) * self.px_per_unit}")
        self.fp.write(f'" style="fill:rgb({random.randint(0, 255)},{random.randint(0, 255)},{random.randint(0, 255)})" />\n')


def run():
    with open(FILENAME_INPUT, "rb") as fp:
        header_bytes = fp.read(SHAPEFILE_HEADER.max_len_bytes)
        header_data = SHAPEFILE_HEADER.decode(header_bytes)
        check_header_data(header_data)
        last_index = 0
        with Consumer() as consumer:
            while True:
                header_bytes = fp.read(RECORD_HEADER.max_len_bytes)
                if header_bytes == b"":
                    break
                i, shorts, shape_type = RECORD_HEADER.decode(header_bytes)
                assert i == last_index + 1, f"Unexpected record #{i}"
                last_index = i
                body_len = shorts * 2 - 4  # Shape type is technically part of the record, we treat it as part of the record header.
                body_bytes = fp.read(body_len)
                print(f"#{i:08}: type {shape_type}, {body_len:7} bytes for record body, pos {fp.tell()}")
                assert len(body_bytes) == body_len
                assert shape_type == 5, f"Can only handle polygons (5), not {shape_type=}."
                polygon_header_bytes, polygon_body_bytes = body_bytes[:POLYGON_HEADER.max_len_bytes], body_bytes[POLYGON_HEADER.max_len_bytes:]
                polygon_header_data = POLYGON_HEADER.decode(polygon_header_bytes)
                polygon_bbox = polygon_header_data[0:4]
                is_visible = is_bbox_visible(polygon_bbox)
                print(f"    {polygon_bbox=} --> {is_visible=}")
                if not is_visible:
                    continue
                num_parts = polygon_header_data[4]
                num_points = polygon_header_data[5]
                polygon_parts_bytes, polygon_points_bytes = polygon_body_bytes[:num_parts * 4], polygon_body_bytes[num_parts * 4:]
                polygon_parts = list(np.frombuffer(polygon_parts_bytes, dtype=np.dtype("<u4")))
                # Note: polygon_points can be very large
                polygon_points = np.frombuffer(polygon_points_bytes, dtype=np.dtype("<f8,<f8"))
                polygon_parts.append(len(polygon_points))
                for part_index in range(len(polygon_parts) - 1):
                    first_point_index = polygon_parts[part_index]
                    last_point_index = polygon_parts[part_index + 1]
                    assert first_point_index < last_point_index
                    consumer.emit_polygon(polygon_points[first_point_index:last_point_index])


if __name__ == "__main__":
    run()
